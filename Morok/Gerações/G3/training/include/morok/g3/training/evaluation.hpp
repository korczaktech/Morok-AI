#pragma once

#include "morok/g3/neural/gru_backend.hpp"
#include "morok/g3/training/dataset.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace morok::g3::training {

enum class RobustnessCondition {
    clean,
    typographical_error,
    missing_accents,
    punctuation,
    irregular_spacing,
    unicode_and_emoji
};

struct RobustnessMetrics final {
    std::string condition;
    std::size_t examples{0};
    std::size_t target_tokens{0};
    double mean_loss{0.0};
    double token_accuracy{0.0};
    double loss_delta_vs_clean{0.0};
    double accuracy_delta_vs_clean{0.0};
};

struct GeneralizationReport final {
    std::string dataset_fingerprint;
    std::uint64_t split_seed{42U};
    std::size_t parameter_count{0};
    std::size_t training_examples{0};
    std::size_t validation_examples{0};
    std::size_t test_examples{0};
    std::size_t training_groups{0};
    std::size_t validation_groups{0};
    std::size_t test_groups{0};
    double training_loss{0.0};
    double validation_loss{0.0};
    double clean_test_loss{0.0};
    double validation_minus_training_loss{0.0};
    double test_minus_training_loss{0.0};
    double test_to_training_loss_ratio{0.0};
    bool overfit_warning{false};
    std::vector<RobustnessMetrics> robustness;
};

struct EvaluationConfig final {
    std::uint64_t split_seed{42U};
    double overfit_warning_relative_gap{0.25};
};

[[nodiscard]] std::string robustness_condition_name(RobustnessCondition condition);
[[nodiscard]] std::string make_robustness_variant(
    const std::string& question, RobustnessCondition condition);
[[nodiscard]] GeneralizationReport evaluate_generalization(
    const neural::G3Backend& backend, const Dataset& dataset,
    const DatasetSplits& splits, const EvaluationConfig& config = {});
[[nodiscard]] bool load_fp32_model_weights(
    const std::filesystem::path& model_path, neural::G3Backend& backend);
void write_generalization_report(
    const std::filesystem::path& path, const GeneralizationReport& report);

} // namespace morok::g3::training
