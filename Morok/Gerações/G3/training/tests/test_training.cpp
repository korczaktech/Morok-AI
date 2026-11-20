#include "morok/g3/training/trainer.hpp"
#include "morok/g3/training/checkpoint.hpp"
#include "morok/g3/training/system_prompt.hpp"
#include "morok/lang/byte_tokenizer.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace morok::g3::training;

namespace {

std::filesystem::path temporary(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

void test_master_system_prompt_contract() {
    std::vector<morok::lang::TokenId> encoded;
    append_master_system_prompt(encoded);
    std::string decoded;
    decoded.reserve(encoded.size());
    for (const auto token : encoded) {
        assert(token < 256U);
        decoded.push_back(static_cast<char>(token));
    }
    assert(decoded == kMasterSystemPrompt);
    assert(kMasterSystemPromptVersion == "morok-g3-master-v1");
}

void test_dataset_loader_and_group_split() {
    const auto path = temporary("morok-g3-training-fixture.jsonl");
    {
        std::ofstream out(path);
        out << " \\t\\r\\n";
        for (int i = 0; i < 20; ++i) {
            out << "{\"example_id\":\"ex-" << i
                << "\",\"group_id\":\"family-" << (i / 2)
                << "\",\"category\":\"general\",\"question\":\"Como {0} {1} " << i
                << "\",\"answer\":\"Oi\",\"source\":\"fixture\","
                   "\"language\":\"pt-BR\",\"dataset_version\":\"test-v1\","
                   "\"quality\":1.0,\"sensitive\":false,"
                   "\"question_variants\":[\"Olá " << i << "\",\"E aí " << i << "\"],"
                   "\"expansion_axes\":[[\"fazer\",\"realizar\"],[\"isso\",\"isto\"]]}\n";
        }
        out << R"({"example_id":"quoted-key","group_id":"quoted-key-family","category":"general","question":"Como escrever \"quality\": 0.2 no texto?","answer":"A palavra quality","source":"fixture","language":"pt-BR","dataset_version":"test-v1"})" << '\n';
    }
    const auto dataset = load_jsonl(path);
    assert(dataset.examples.size() == 21U);
    assert(dataset.examples.back().quality == 1.0);
    assert(dataset.examples.back().question.find("\"quality\": 0.2") != std::string::npos);
    assert(!dataset.fingerprint.empty());
    const auto splits = split_by_group_id(dataset, SplitConfig{0.85, 0.10, 0.05, 7U});
    assert(!splits.train.empty() && !splits.validation.empty() && !splits.test.empty());
    auto group_set = [](const Dataset& data, const std::vector<std::size_t>& indices) {
        std::vector<std::string> groups;
        for (auto i : indices) groups.push_back(data.examples[i].group_id);
        return groups;
    };
    const auto train = group_set(dataset, splits.train);
    const auto valid = group_set(dataset, splits.validation);
    const auto test = group_set(dataset, splits.test);
    for (const auto& group : train) {
        assert(std::find(valid.begin(), valid.end(), group) == valid.end());
        assert(std::find(test.begin(), test.end(), group) == test.end());
    }
    for (const auto& group : valid)
        assert(std::find(test.begin(), test.end(), group) == test.end());
    const auto expanded = expand_controlled(dataset.examples.front(), 2U, 42U);
    assert(expanded.size() == 2U);
    assert(expanded[0].group_id == expanded[1].group_id);
    assert(expanded[0].example_id != expanded[1].example_id);
    DatasetExample combinatorial;
    combinatorial.example_id = "template-1";
    combinatorial.group_id = "same-family";
    combinatorial.question = "Como {0} {1}?";
    combinatorial.answer = "Resposta";
    combinatorial.expansion_axes = {{"fazer", "realizar"}, {"isso", "isto"}};
    const auto combinations = expand_controlled(combinatorial, 4U, 123U);
    assert(combinations.size() == 4U);
    for (const auto& item : combinations) {
        assert(item.question.find('{') == std::string::npos);
        assert(item.group_id == "same-family");
    }
    std::filesystem::remove(path);
}

void test_dataset_loader_rejects_malformed_json() {
    const auto path = temporary("morok-g3-invalid-dataset.jsonl");
    const std::vector<std::string> invalid_lines{
        R"({"example_id":"a","example_id":"b","group_id":"g","category":"general","question":"Q","answer":"A","source":"test","language":"pt-BR","dataset_version":"v1"})",
        R"({"example_id":"a","group_id":"g","category":"general","question":"Q","answer":"A","source":"test","language":"pt-BR","dataset_version":"v1",})",
        R"({"example_id":"a","group_id":"g","category":"general","question":"Q","answer":"A","source":"test","language":"pt-BR","dataset_version":"v1","quality":1.0junk})"
    };
    for (const auto& line : invalid_lines) {
        {
            std::ofstream out(path, std::ios::trunc);
            out << line << '\n';
        }
        bool rejected = false;
        try { (void)load_jsonl(path); }
        catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
    }
    std::filesystem::remove(path);
}

void test_dataset_fingerprint_preserves_quality_precision() {
    const auto first_path = temporary("morok-g3-quality-a.jsonl");
    const auto second_path = temporary("morok-g3-quality-b.jsonl");
    const auto write = [](const std::filesystem::path& path, const char* quality) {
        std::ofstream out(path, std::ios::trunc);
        out << "{\"example_id\":\"same\",\"group_id\":\"same-group\","
               "\"category\":\"general\",\"question\":\"Pergunta\","
               "\"answer\":\"Resposta\",\"source\":\"test\","
               "\"language\":\"pt-BR\",\"dataset_version\":\"v1\","
               "\"quality\":" << quality << "}\n";
    };
    write(first_path, "0.8000001");
    write(second_path, "0.8000002");
    const auto first = load_jsonl(first_path);
    const auto second = load_jsonl(second_path);
    assert(first.fingerprint != second.fingerprint);
    std::filesystem::remove(first_path);
    std::filesystem::remove(second_path);
}

void test_cross_entropy_and_perturbations() {
    std::vector<float> logits(261U, 0.0F), gradient;
    const double loss = causal_cross_entropy(logits, 65U, 0.05, &gradient);
    assert(std::isfinite(loss) && loss > 0.0);
    assert(gradient.size() == 261U);
    double sum = 0.0;
    for (float value : gradient) { assert(std::isfinite(value)); sum += value; }
    assert(std::abs(sum) < 1.0e-5);
    const double special_loss = causal_cross_entropy(logits, 258U, 0.05, &gradient);
    assert(std::isfinite(special_loss));
    assert(gradient[258U] < 0.0F);
    const double pad_loss = causal_cross_entropy(logits, 256U, 0.05, &gradient);
    assert(pad_loss == 0.0);
    for (float value : gradient) assert(value == 0.0F);
    PerturbationConfig config;
    config.probability = 1.0;
    std::mt19937_64 random(99U);
    const auto changed = perturb_question("voce esta aqui?", config, random);
    assert(!changed.empty());
}

void test_training_updates_model_and_logs_per_example() {
    const auto path = temporary("morok-g3-training-small.jsonl");
    const auto metrics = temporary("morok-g3-training-metrics.jsonl");
    {
        std::ofstream out(path);
        for (int i = 0; i < 3; ++i) {
            out << "{\"example_id\":\"one-" << i
                << "\",\"group_id\":\"intent-" << i
                << "\",\"category\":\"general\",\"question\":\"Oi\","
                   "\"answer\":\"Olá\",\"source\":\"unit-test\","
                   "\"language\":\"pt-BR\",\"dataset_version\":\"v1\"}\n";
        }
    }
    std::filesystem::remove(metrics);
    auto dataset = load_jsonl(path);
    morok::g3::neural::G3Backend backend(morok::g3::neural::G3Config{123U, 1.0e-5F});
    const auto before = backend.weights().embedding;
    TrainingConfig config;
    config.epochs = 1U;
    config.bptt_steps = 128U;
    config.maximum_variants_per_example = 1U;
    config.learning_rate = 1.0e-3;
    config.minimum_learning_rate = 1.0e-5;
    config.variational_dropout = 0.0;
    config.label_smoothing = 0.0;
    config.perturbation.probability = 0.0;
    config.metrics_jsonl = metrics;
    auto sensitive_dataset = dataset;
    sensitive_dataset.examples.front().sensitive = true;
    bool sensitive_rejected = false;
    try { (void)train(backend, sensitive_dataset, config); }
    catch (const std::invalid_argument&) { sensitive_rejected = true; }
    assert(sensitive_rejected);
    const auto report = train(backend, dataset, config);
    assert(report.epochs.size() == 1U);
    assert(report.optimizer_steps == report.epochs.front().training_examples);
    assert(backend.parameter_count() == 100000U);
    assert(backend.weights().embedding != before);
    assert(std::filesystem::exists(metrics));
    std::ifstream in(metrics);
    std::string content((std::istreambuf_iterator<char>(in)), {});
    assert(content.find("\"question_loss\"") != std::string::npos);
    assert(content.find("\"example_loss\"") != std::string::npos);
    assert(content.find("\"validation_loss\"") != std::string::npos);
    std::filesystem::remove(path);
    std::filesystem::remove(metrics);
}

void test_epoch_boundary_checkpoint_resume_matches_uninterrupted_training() {
    const auto dataset_path = temporary("morok-g3-resume-dataset.jsonl");
    const auto baseline_metrics = temporary("morok-g3-resume-baseline.jsonl");
    const auto split_metrics = temporary("morok-g3-resume-split.jsonl");
    const auto checkpoint_path = temporary("morok-g3-resume.checkpoint");
    {
        std::ofstream out(dataset_path);
        for (int i = 0; i < 12; ++i) {
            out << "{\"example_id\":\"resume-" << i
                << "\",\"group_id\":\"resume-family-" << i
                << "\",\"category\":\"general\",\"question\":\"Pergunta " << i
                << "\",\"answer\":\"Resposta " << i
                << "\",\"source\":\"resume-test\",\"language\":\"pt-BR\","
                   "\"dataset_version\":\"resume-v1\"}\n";
        }
    }
    std::filesystem::remove(baseline_metrics);
    std::filesystem::remove(split_metrics);
    std::filesystem::remove(checkpoint_path);
    const auto dataset = load_jsonl(dataset_path);

    TrainingConfig baseline_config;
    baseline_config.epochs = 2U;
    baseline_config.seed = 551U;
    baseline_config.maximum_variants_per_example = 1U;
    baseline_config.variational_dropout = 0.0;
    baseline_config.perturbation.probability = 0.0;
    baseline_config.label_smoothing = 0.0;
    baseline_config.metrics_jsonl = baseline_metrics;
    morok::g3::neural::G3Backend uninterrupted(
        morok::g3::neural::G3Config{551U, 1.0e-5F});
    const auto baseline_report = train(uninterrupted, dataset, baseline_config);
    assert(baseline_report.completed_epochs == 2U);
    const auto baseline_weights = morok::g3::training::capture_g3_model_weights(uninterrupted);

    auto first_part_config = baseline_config;
    first_part_config.metrics_jsonl = split_metrics;
    first_part_config.checkpoint_path = checkpoint_path;
    first_part_config.max_epochs_this_run = 1U;
    morok::g3::neural::G3Backend first_part(
        morok::g3::neural::G3Config{551U, 1.0e-5F});
    const auto first_report = train(first_part, dataset, first_part_config);
    assert(first_report.completed_epochs == 1U);
    assert(!first_report.resumed);
    assert(std::filesystem::exists(checkpoint_path));

    auto resumed_config = baseline_config;
    resumed_config.metrics_jsonl = split_metrics;
    resumed_config.resume_from_checkpoint = checkpoint_path;
    morok::g3::neural::G3Backend resumed(
        morok::g3::neural::G3Config{999U, 1.0e-5F});
    const auto resumed_report = train(resumed, dataset, resumed_config);
    assert(resumed_report.resumed);
    assert(resumed_report.completed_epochs == 2U);
    assert(resumed_report.epochs.size() == 1U);
    assert(morok::g3::training::capture_g3_model_weights(resumed) == baseline_weights);

    std::filesystem::remove(dataset_path);
    std::filesystem::remove(baseline_metrics);
    std::filesystem::remove(split_metrics);
    std::filesystem::remove(checkpoint_path);
}

} // namespace

int main() {
    test_master_system_prompt_contract();
    test_dataset_loader_and_group_split();
    test_dataset_loader_rejects_malformed_json();
    test_dataset_fingerprint_preserves_quality_precision();
    test_cross_entropy_and_perturbations();
    test_training_updates_model_and_logs_per_example();
    test_epoch_boundary_checkpoint_resume_matches_uninterrupted_training();
    return 0;
}
