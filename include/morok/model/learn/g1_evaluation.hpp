#pragma once

#include "morok/model/parameters/g1_parameters.hpp"

#include <cstddef>
#include <cstdint>

namespace morok::model {

struct G1DistributionStats {
    std::size_t count{0};
    std::size_t zero_count{0};
    double minimum{0.0};
    double maximum{0.0};
    double mean{0.0};
    double standard_deviation{0.0};
    bool all_finite{true};
};

struct G1ParameterEvaluation {
    std::size_t architecture_parameter_count{kG1ParameterCount};
    std::size_t b0_updated_parameter_count{kG1ParameterCount - kG1SegmenterParameterCount};
    bool all_finite{true};
    G1DistributionStats all{};
    G1DistributionStats fast{};
    G1DistributionStats slow{};
    G1DistributionStats prediction{};
    G1DistributionStats segmenter{};
};

struct G1LossMetrics {
    std::uint64_t target_count{0};
    double mean_nats_per_byte{0.0};
    double bits_per_byte{0.0};
    double byte_perplexity{0.0};
};

[[nodiscard]] G1DistributionStats summarize_g1_values(
    const float* values, std::size_t count);

[[nodiscard]] G1ParameterEvaluation evaluate_g1_parameters(
    const G1Parameters& parameters);

[[nodiscard]] G1LossMetrics summarize_g1_loss(
    double total_negative_log_likelihood_nats,
    std::uint64_t target_count);

} // namespace morok::model
