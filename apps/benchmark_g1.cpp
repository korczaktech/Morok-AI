#include "morok/model/learn/g1_benchmark.hpp"
#include "morok/model/learn/g1_evaluation.hpp"
#include "morok/model/learn/g1_training.hpp"
#include "morok/model/parameters/g1_parameters.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <array>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace {
std::string json_escape(const std::string& value) {
    std::string out;
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(static_cast<char>(c)); }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) out += '?';
        else out.push_back(static_cast<char>(c));
    }
    return out;
}
long peak_rss_bytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("getrusage failed");
#if defined(__APPLE__)
    return usage.ru_maxrss;
#else
    return usage.ru_maxrss * 1024L;
#endif
}
std::string machine_name() {
    utsname info{};
    if (uname(&info) != 0) return "unknown";
    return std::string(info.sysname) + " " + info.release + " " + info.machine;
}
std::string cpu_name() {
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line))
        if (line.rfind("model name", 0) == 0) {
            const auto pos = line.find(':');
            if (pos != std::string::npos) return line.substr(pos + 2);
        }
    return "unknown";
}
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: morok_benchmark <corpus-dir> <output.jsonl> [epochs=3] [seed=42] [license=UNKNOWN] [source=UNSPECIFIED] [repetitions=1]\n";
        return 2;
    }
    try {
        const std::filesystem::path corpus = argv[1], output = argv[2];
        const std::size_t epochs = argc > 3 ? std::stoull(argv[3]) : 3;
        const std::uint64_t seed = argc > 4 ? std::stoull(argv[4]) : 42;
        const std::string license = argc > 5 ? argv[5] : "UNKNOWN";
        const std::string source = argc > 6 ? argv[6] : "UNSPECIFIED";
        const std::size_t repetitions = argc > 7 ? std::stoull(argv[7]) : 1;
        if (epochs == 0 || repetitions == 0 || repetitions > 20 || license == "UNKNOWN" || source == "UNSPECIFIED")
            throw std::invalid_argument("provide positive epochs and explicit corpus license/source provenance");
        std::ofstream log(output, std::ios::binary | std::ios::app);
        if (!log) throw std::runtime_error("cannot open benchmark JSONL output");
        const std::string commit = std::getenv("GITHUB_SHA") ? std::getenv("GITHUB_SHA") : "local";
        const std::string compiler =
#if defined(__clang__)
            "clang " __clang_version__;
#elif defined(__GNUC__)
            "gcc " __VERSION__;
#else
            "unknown";
#endif
        struct Experiment {
            std::size_t truncation;
            morok::model::G1OptimizerConfig optimizer;
            const char* group;
        };
        auto low_lr = morok::model::G1OptimizerConfig::adamw();
        low_lr.learning_rate = 0.0003;
        auto high_lr = morok::model::G1OptimizerConfig::adamw();
        high_lr.learning_rate = 0.003;
        const std::array<Experiment, 6> experiments{{
            {32, morok::model::G1OptimizerConfig::adamw(), "bptt_sweep"},
            {64, morok::model::G1OptimizerConfig::adamw(), "reference"},
            {128, morok::model::G1OptimizerConfig::adamw(), "bptt_sweep"},
            {64, morok::model::G1OptimizerConfig::sgd_momentum(), "optimizer_comparison"},
            {64, low_lr, "learning_rate_sweep"},
            {64, high_lr, "learning_rate_sweep"}
        }};
        for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
            const std::uint64_t run_seed = seed + repetition;
            const auto baseline = morok::model::evaluate_g1_softmax256_unigram_baseline(corpus, run_seed);
            for (const auto& experiment : experiments) {
                const auto truncation = experiment.truncation;
                const auto kind = experiment.optimizer.kind;
                morok::model::G1TrainingConfig config;
                config.corpus_directory = corpus;
                config.max_epochs = epochs;
                config.seed = run_seed;
                config.truncation_length = truncation;
                config.optimizer = experiment.optimizer;
                morok::model::G1Parameters parameters;
                morok::model::initialize_g1_parameters(parameters, run_seed);
                const auto started = std::chrono::steady_clock::now();
                const auto report = morok::model::train_g1_corpus(config, parameters);
                if (report.epochs.empty() ||
                    baseline.unique_documents != report.unique_documents ||
                    baseline.test_target_bytes != report.test_target_bytes ||
                    baseline.training_target_bytes != report.epochs.front().training_target_bytes)
                    throw std::runtime_error("softmax baseline and G1 trainer produced different document splits");
                const auto elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started).count();
                std::uint64_t trained_targets = 0, updates = 0;
                for (const auto& epoch : report.epochs) {
                    trained_targets += epoch.training_target_bytes;
                    updates += epoch.optimizer_updates;
                }
                const double throughput = elapsed > 0.0
                    ? static_cast<double>(trained_targets) / elapsed : 0.0;
                const auto parameter_stats = morok::model::evaluate_g1_parameters(parameters);
                double weighted_gradient_norm = 0.0;
                double maximum_gradient_norm = 0.0;
                double total_epoch_seconds = 0.0;
                double maximum_epoch_seconds = 0.0;
                std::uint64_t gradient_updates = 0;
                for (const auto& epoch : report.epochs) {
                    weighted_gradient_norm += epoch.mean_gradient_norm *
                        static_cast<double>(epoch.optimizer_updates);
                    gradient_updates += epoch.optimizer_updates;
                    maximum_gradient_norm = std::max(maximum_gradient_norm, epoch.max_gradient_norm);
                    total_epoch_seconds += epoch.wall_seconds;
                    maximum_epoch_seconds = std::max(maximum_epoch_seconds, epoch.wall_seconds);
                }
                if (gradient_updates)
                    weighted_gradient_norm /= static_cast<double>(gradient_updates);
                const double training_bpb = report.epochs.back().training_bpb;
                const char* optimizer = kind == morok::model::G1OptimizerKind::AdamW
                    ? "AdamW" : "SGD_Momentum";
                log << std::setprecision(17)
                    << "{\"schema\":1,\"event\":\"g1_benchmark\","
                    << "\"commit\":\"" << json_escape(commit) << "\","
                    << "\"compiler\":\"" << json_escape(compiler) << "\","
                    << "\"machine\":\"" << json_escape(machine_name()) << "\","
                    << "\"cpu\":\"" << json_escape(cpu_name()) << "\","
                    << "\"corpus_source\":\"" << json_escape(source) << "\","
                    << "\"corpus_license\":\"" << json_escape(license) << "\","
                    << "\"corpus_hash\":\"" << report.corpus_hash << "\","
                    << "\"seed\":" << run_seed << ",\"epochs_requested\":" << epochs << ','
                    << "\"epochs_completed\":" << report.epochs.size() << ','
                    << "\"experiment_group\":\"" << experiment.group << "\","
                    << "\"truncation_bytes\":" << truncation << ','
                    << "\"optimizer\":\"" << optimizer << "\","
                    << "\"learning_rate\":" << config.optimizer.learning_rate << ','
                    << "\"trainable_parameters\":" << parameter_stats.trainable_parameter_count << ','
                    << "\"parameters_finite\":" << (parameter_stats.all_finite ? "true" : "false") << ','
                    << "\"parameter_mean\":" << parameter_stats.all.mean << ','
                    << "\"parameter_stddev\":" << parameter_stats.all.standard_deviation << ','
                    << "\"parameter_min\":" << parameter_stats.all.minimum << ','
                    << "\"parameter_max\":" << parameter_stats.all.maximum << ','
                    << "\"fast_parameter_mean\":" << parameter_stats.fast.mean << ','
                    << "\"fast_parameter_stddev\":" << parameter_stats.fast.standard_deviation << ','
                    << "\"slow_parameter_mean\":" << parameter_stats.slow.mean << ','
                    << "\"slow_parameter_stddev\":" << parameter_stats.slow.standard_deviation << ','
                    << "\"prediction_parameter_mean\":" << parameter_stats.prediction.mean << ','
                    << "\"prediction_parameter_stddev\":" << parameter_stats.prediction.standard_deviation << ','
                    << "\"segmenter_parameter_mean\":" << parameter_stats.segmenter.mean << ','
                    << "\"segmenter_parameter_stddev\":" << parameter_stats.segmenter.standard_deviation << ','
                    << "\"training_byte_perplexity\":" << std::exp2(training_bpb) << ','
                    << "\"validation_byte_perplexity\":" << std::exp2(report.best_validation_bpb) << ','
                    << "\"test_byte_perplexity\":" << std::exp2(report.test_bpb) << ','
                    << "\"test_bpb_delta_vs_unigram\":" << (report.test_bpb - baseline.softmax256_unigram_bpb) << ','
                    << "\"test_bpb_ratio_vs_unigram\":" << (report.test_bpb / baseline.softmax256_unigram_bpb) << ','
                    << "\"gradient_norm_mean\":" << weighted_gradient_norm << ','
                    << "\"gradient_norm_max\":" << maximum_gradient_norm << ','
                    << "\"mean_epoch_seconds\":" << (report.epochs.empty() ? 0.0 : total_epoch_seconds / static_cast<double>(report.epochs.size())) << ','
                    << "\"max_epoch_seconds\":" << maximum_epoch_seconds << ','
                    << "\"documents\":" << report.unique_documents << ','
                    << "\"corpus_bytes\":" << report.corpus_bytes << ','
                    << "\"training_targets\":" << trained_targets << ','
                    << "\"optimizer_updates\":" << updates << ','
                    << "\"validation_bpb\":" << report.best_validation_bpb << ','
                    << "\"test_bpb\":" << report.test_bpb << ','
                    << "\"softmax256_unigram_test_bpb\":" << baseline.softmax256_unigram_bpb << ','
                    << "\"test_targets\":" << report.test_target_bytes << ','
                    << "\"inference_test_seconds\":" << report.test_evaluation_seconds << ','
                    << "\"inference_test_bytes_per_second\":" << report.test_evaluation_bytes_per_second << ','
                    << "\"inference_ns_per_byte\":" << (report.test_target_bytes ? report.test_evaluation_seconds * 1.0e9 / static_cast<double>(report.test_target_bytes) : 0.0) << ','
                    << "\"training_throughput_bytes_per_second\":" << throughput << ','
                    << "\"wall_seconds\":" << elapsed << ','
                    << "\"peak_rss_bytes\":" << peak_rss_bytes() << ','
                    << "\"early_stopped\":" << (report.stopped_early ? "true" : "false") << "}\n";
                log.flush();
                if (!log) throw std::runtime_error("failed writing benchmark JSONL output");
                std::cout << experiment.group << " " << optimizer << " BPTT=" << truncation
                          << " LR=" << config.optimizer.learning_rate
                          << " validation_BPB=" << report.best_validation_bpb
                          << " test_BPB=" << report.test_bpb
                          << " softmax256_unigram_BPB=" << baseline.softmax256_unigram_bpb
                          << " throughput_Bps=" << throughput
                          << " wall_s=" << elapsed << " peak_RSS_B=" << peak_rss_bytes() << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok_benchmark: " << error.what() << '\n';
        return 1;
    }
}
