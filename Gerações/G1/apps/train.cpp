#include "morok/model/learn/g1_training.hpp"
#include "morok/model/parameters/g1_parameters.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::uint64_t parse_unsigned(std::string_view value, std::string_view name,
                             std::uint64_t minimum, std::uint64_t maximum) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size() ||
        result < minimum || result > maximum)
        throw std::invalid_argument(std::string(name) + " is outside its valid range");
    return result;
}
double parse_positive_double(const std::string& value, std::string_view name) {
    std::size_t consumed = 0;
    const double parsed = std::stod(value, &consumed);
    if (consumed != value.size() || !std::isfinite(parsed) || parsed <= 0.0)
        throw std::invalid_argument(std::string(name) + " must be a finite positive number");
    return parsed;
}
void usage() {
    std::cout
        << "Usage: morok-train <corpus-directory> [epochs=50] [seed=42] [options]\n"
        << "Options:\n"
        << "  --checkpoint <path>    Save resumable checkpoint after epochs\n"
        << "  --resume <path>        Resume from a compatible checkpoint\n"
        << "  --log <path>           Append epoch metrics as JSONL\n"
        << "  --export-best <path>   Export the best validation checkpoint\n"
        << "  --patience <n>         Early-stopping patience (1..10000)\n"
        << "  --truncation <n>       BPTT window in bytes (1..128)\n"
        << "  --optimizer <name>     adamw or sgd\n"
        << "  --learning-rate <n>    Positive finite learning rate\n";
}
}
int main(int argc, char** argv) {
    if (argc == 2 && (std::string_view(argv[1]) == "--help" ||
                      std::string_view(argv[1]) == "-h")) {
        usage();
        return 0;
    }
    if (argc < 2) {
        usage();
        return 2;
    }
    try {
        morok::model::G1TrainingConfig config;
        config.corpus_directory = argv[1];
        int index = 2;
        if (index < argc && argv[index][0] != '-')
            config.max_epochs = static_cast<std::size_t>(
                parse_unsigned(argv[index++], "epochs", 1, 100000));
        if (index < argc && argv[index][0] != '-')
            config.seed = parse_unsigned(argv[index++], "seed", 0,
                                          std::numeric_limits<std::uint64_t>::max());

        while (index < argc) {
            const std::string option = argv[index++];
            if (index >= argc) throw std::invalid_argument("missing value for " + option);
            const std::string value = argv[index++];
            if (option == "--checkpoint") config.checkpoint_path = value;
            else if (option == "--resume") config.resume_from_checkpoint = value;
            else if (option == "--log") config.metrics_log_path = value;
            else if (option == "--export-best") config.best_model_path = value;
            else if (option == "--patience")
                config.patience = static_cast<std::size_t>(
                    parse_unsigned(value, "patience", 1, 10000));
            else if (option == "--truncation")
                config.truncation_length = static_cast<std::size_t>(
                    parse_unsigned(value, "truncation", 1, 128));
            else if (option == "--learning-rate")
                config.optimizer.learning_rate = parse_positive_double(value, "learning-rate");
            else if (option == "--optimizer") {
                const double selected_learning_rate = config.optimizer.learning_rate;
                if (value == "adamw")
                    config.optimizer = morok::model::G1OptimizerConfig::adamw();
                else if (value == "sgd")
                    config.optimizer = morok::model::G1OptimizerConfig::sgd_momentum();
                else throw std::invalid_argument("optimizer must be adamw or sgd");
                config.optimizer.learning_rate = selected_learning_rate;
            } else {
                throw std::invalid_argument("unknown option: " + option);
            }
        }
        if (!config.resume_from_checkpoint.empty() && config.checkpoint_path.empty())
            config.checkpoint_path = config.resume_from_checkpoint;

        morok::model::G1Parameters parameters;
        if (config.resume_from_checkpoint.empty())
            morok::model::initialize_g1_parameters(parameters, config.seed);
        const auto report = morok::model::train_g1_corpus(config, parameters);

        std::cout << std::setprecision(17);
        for (const auto& epoch : report.epochs) {
            std::cout << "{\"type\":\"epoch\",\"epoch\":" << epoch.epoch
                      << ",\"train_bpb\":" << epoch.training_bpb
                      << ",\"validation_bpb\":" << epoch.validation_bpb
                      << ",\"updates\":" << epoch.optimizer_updates
                      << ",\"seconds\":" << epoch.wall_seconds << "}\n";
        }
        std::cout << "{\"type\":\"summary\",\"documents\":" << report.unique_documents
                  << ",\"train\":" << report.training_documents
                  << ",\"validation\":" << report.validation_documents
                  << ",\"test\":" << report.test_documents
                  << ",\"corpus_bytes\":" << report.corpus_bytes
                  << ",\"corpus_hash\":\"" << report.corpus_hash
                  << "\",\"best_epoch\":" << report.best_epoch
                  << ",\"validation_bpb\":" << report.best_validation_bpb
                  << ",\"test_bpb\":" << report.test_bpb
                  << ",\"resumed\":" << (report.resumed ? "true" : "false")
                  << ",\"stopped_early\":" << (report.stopped_early ? "true" : "false")
                  << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok-train: " << error.what() << '\n';
        return 1;
    }
}
