#include "morok/model/learn/g1_evaluation.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void test_parameter_inventory_and_groups() {
    morok::model::G1Parameters parameters{};
    const auto result = morok::model::evaluate_g1_parameters(parameters);
    require(result.architecture_parameter_count == 1000, "trainable count must remain exactly 1000");
    require(result.b0_updated_parameter_count == 971, "B0 updated parameter count mismatch");
    require(result.all.count == 1000, "all-parameter statistics count mismatch");
    require(result.fast.count == 506, "fast group count mismatch");
    require(result.slow.count == 253, "slow group count mismatch");
    require(result.prediction.count == 212, "prediction group count mismatch");
    require(result.segmenter.count == 29, "segmenter group count mismatch");
    require(result.all.zero_count == 1000, "zero initialization stats mismatch");
}
void test_distribution_statistics() {
    const std::array<float, 4> values{-2.0F, 0.0F, 2.0F, 4.0F};
    const auto stats = morok::model::summarize_g1_values(values.data(), values.size());
    require(stats.count == 4 && stats.zero_count == 1, "distribution counts mismatch");
    require(stats.minimum == -2.0 && stats.maximum == 4.0, "distribution bounds mismatch");
    require(std::abs(stats.mean - 1.0) < 1e-12, "distribution mean mismatch");
    require(std::abs(stats.standard_deviation - std::sqrt(5.0)) < 1e-12,
            "population standard deviation mismatch");
}
void test_loss_and_byte_perplexity() {
    constexpr double ln2 = 0.693147180559945309417232121458176568;
    const auto metrics = morok::model::summarize_g1_loss(8.0 * ln2, 8);
    require(metrics.target_count == 8, "loss target count mismatch");
    require(std::abs(metrics.bits_per_byte - 1.0) < 1e-12, "BPB mismatch");
    require(std::abs(metrics.byte_perplexity - 2.0) < 1e-12, "byte perplexity mismatch");
}
void test_invalid_metrics_rejected() {
    bool rejected = false;
    try { static_cast<void>(morok::model::summarize_g1_loss(1.0, 0)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "zero target count must be rejected");
    const std::array<float, 1> invalid{std::numeric_limits<float>::infinity()};
    rejected = false;
    try { static_cast<void>(morok::model::summarize_g1_values(invalid.data(), invalid.size())); }
    catch (const std::domain_error&) { rejected = true; }
    require(rejected, "non-finite parameter must be rejected");
}
}
int main() {
    try {
        test_parameter_inventory_and_groups();
        test_distribution_statistics();
        test_loss_and_byte_perplexity();
        test_invalid_metrics_rejected();
    } catch (const std::exception& error) {
        std::cerr << "G1 evaluation test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
