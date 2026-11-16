#include "morok/g3/training/evaluation.hpp"

#include "morok/g3/inference/model_artifact.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace morok::g3::training;

namespace {

void test_robustness_transformations() {
    const auto typo = make_robustness_variant(
        "Como você está?", RobustnessCondition::typographical_error);
    const auto no_accents = make_robustness_variant(
        "ação, você", RobustnessCondition::missing_accents);
    const auto punctuation = make_robustness_variant(
        "Olá, tudo bem?", RobustnessCondition::punctuation);
    const auto spacing = make_robustness_variant(
        "Como você está?", RobustnessCondition::irregular_spacing);
    const auto unicode = make_robustness_variant(
        "Como você está?", RobustnessCondition::unicode_and_emoji);
    assert(typo != "Como você está?");
    assert(no_accents.find("ç") == std::string::npos);
    assert(no_accents.find("ã") == std::string::npos);
    assert(punctuation.back() == '?');
    assert(spacing.find("  ") != std::string::npos);
    assert(unicode.find("😀") != std::string::npos);
    assert(unicode.find("東京") != std::string::npos);
}

void test_real_backend_generalization_report() {
    const auto dataset_path = std::filesystem::temp_directory_path() /
                              "morok-g3-evaluation-fixture.jsonl";
    const auto report_path = std::filesystem::temp_directory_path() /
                             "morok-g3-evaluation-report.json";
    {
        std::ofstream out(dataset_path);
        for (int i = 0; i < 30; ++i) {
            out << "{\"example_id\":\"eval-" << i
                << "\",\"group_id\":\"intent-" << i
                << "\",\"category\":\"general\",\"question\":\"Como voce esta " << i
                << "?\",\"answer\":\"Estou bem " << i
                << ".\",\"source\":\"fixture\",\"language\":\"pt-BR\","
                   "\"dataset_version\":\"eval-v1\"}\n";
        }
    }
    const auto dataset = load_jsonl(dataset_path);
    constexpr std::uint64_t seed = 19U;
    const auto splits = split_by_group_id(
        dataset, SplitConfig{0.85, 0.10, 0.05, seed});
    morok::g3::neural::G3Backend backend(
        morok::g3::neural::G3Config{seed, 1.0e-5F});
    const auto report = evaluate_generalization(
        backend, dataset, splits, EvaluationConfig{seed, 0.25});
    assert(report.parameter_count == 100000U);
    assert(report.training_examples == splits.train.size());
    assert(report.validation_examples == splits.validation.size());
    assert(report.test_examples == splits.test.size());
    assert(report.training_groups > 0U && report.validation_groups > 0U &&
           report.test_groups > 0U);
    assert(report.robustness.size() == 6U);
    assert(report.robustness.front().condition == "clean");
    assert(report.robustness.back().condition == "unicode_and_emoji");
    assert(std::isfinite(report.training_loss));
    assert(std::isfinite(report.validation_loss));
    assert(std::isfinite(report.clean_test_loss));
    for (const auto& metric : report.robustness) {
        assert(metric.examples == report.test_examples);
        assert(metric.target_tokens > 0U);
        assert(std::isfinite(metric.mean_loss));
        assert(std::isfinite(metric.token_accuracy));
        assert(std::isfinite(metric.loss_delta_vs_clean));
    }
    write_generalization_report(report_path, report);
    std::ifstream in(report_path);
    const std::string json((std::istreambuf_iterator<char>(in)), {});
    assert(json.find("morok-g3-generalization-v1") != std::string::npos);
    assert(json.find("100000") != std::string::npos);
    assert(json.find("typographical_error") != std::string::npos);
    assert(json.find("unicode_and_emoji") != std::string::npos);
    std::filesystem::remove(dataset_path);
    std::filesystem::remove(report_path);
}

} // namespace

int main() {
    test_robustness_transformations();
    test_real_backend_generalization_report();
    return 0;
}
