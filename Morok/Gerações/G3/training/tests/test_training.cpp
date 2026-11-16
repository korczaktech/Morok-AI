#include "morok/g3/training/trainer.hpp"

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

void test_dataset_loader_and_group_split() {
    const auto path = temporary("morok-g3-training-fixture.jsonl");
    {
        std::ofstream out(path);
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
    }
    const auto dataset = load_jsonl(path);
    assert(dataset.examples.size() == 20U);
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

} // namespace

int main() {
    test_dataset_loader_and_group_split();
    test_cross_entropy_and_perturbations();
    test_training_updates_model_and_logs_per_example();
    return 0;
}
