#pragma once

#include "morok/g3/neural/gru_backend.hpp"
#include "morok/g3/training/dataset.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace morok::g3::training {

struct PerturbationConfig final {
    double probability{0.45};
    double swap_adjacent_weight{0.20};
    double omission_weight{0.20};
    double nearby_key_weight{0.20};
    double accent_case_punctuation_weight{0.25};
    double repetition_spacing_weight{0.15};
};

struct TrainingConfig final {
    std::size_t epochs{10};
    std::size_t bptt_steps{128};
    std::size_t maximum_variants_per_example{4};
    double learning_rate{1.0e-3};
    double minimum_learning_rate{1.0e-5};
    double warmup_fraction{0.03};
    double weight_decay{1.0e-3};
    double beta1{0.9};
    double beta2{0.999};
    double adam_epsilon{1.0e-8};
    double label_smoothing{0.05};
    double variational_dropout{0.10};
    double maximum_gradient_norm{1.0};
    std::uint64_t seed{42U};
    PerturbationConfig perturbation{};
    std::filesystem::path metrics_jsonl;
};

struct EpochMetrics final {
    std::size_t epoch{0};
    double training_loss{0.0};
    double validation_loss{0.0};
    std::size_t training_examples{0};
    std::size_t validation_examples{0};
    std::size_t optimizer_steps{0};
    double last_gradient_norm{0.0};
    double maximum_gradient_norm{0.0};
};

struct TrainingReport final {
    std::string run_id;
    std::string dataset_fingerprint;
    DatasetSplits splits;
    std::vector<EpochMetrics> epochs;
    std::size_t examples_seen{0};
    std::size_t augmented_examples{0};
    std::size_t optimizer_steps{0};
};

[[nodiscard]] std::string perturb_question(
    const std::string& question, const PerturbationConfig& config,
    std::mt19937_64& random);

[[nodiscard]] double causal_cross_entropy(
    const std::vector<float>& logits, std::uint16_t target,
    double label_smoothing, std::vector<float>* gradient = nullptr);

[[nodiscard]] TrainingReport train(
    neural::G3Backend& backend, const Dataset& dataset,
    const TrainingConfig& config = {});

} // namespace morok::g3::training
