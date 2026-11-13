#include "morok/model/learn/g1_evaluation.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace morok::model {

G1DistributionStats summarize_g1_values(const float* values, std::size_t count) {
    if (values == nullptr || count == 0)
        throw std::invalid_argument("parameter statistics require a non-empty range");

    G1DistributionStats result;
    result.count = count;
    result.minimum = std::numeric_limits<double>::infinity();
    result.maximum = -std::numeric_limits<double>::infinity();

    // Welford's online algorithm avoids a cancellation-prone sum-of-squares formula.
    double mean = 0.0;
    double m2 = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double value = static_cast<double>(values[i]);
        if (!std::isfinite(value)) {
            result.all_finite = false;
            continue;
        }
        result.minimum = value < result.minimum ? value : result.minimum;
        result.maximum = value > result.maximum ? value : result.maximum;
        if (value == 0.0) ++result.zero_count;
        const double delta = value - mean;
        mean += delta / static_cast<double>(i + 1);
        const double delta_after = value - mean;
        m2 += delta * delta_after;
    }

    if (!result.all_finite)
        throw std::domain_error("cannot summarize non-finite model parameters");
    result.mean = mean;
    result.standard_deviation = std::sqrt(m2 / static_cast<double>(count));
    return result;
}

G1ParameterEvaluation evaluate_g1_parameters(const G1Parameters& parameters) {
    const auto flat = parameters.flatten();
    G1ParameterEvaluation result;
    result.all = summarize_g1_values(flat.data(), flat.size());
    result.fast = summarize_g1_values(flat.data(), 506);
    result.slow = summarize_g1_values(flat.data() + 506, 253);
    result.prediction = summarize_g1_values(flat.data() + 759, 212);
    result.segmenter = summarize_g1_values(flat.data() + 971, 29);
    result.all_finite = result.all.all_finite && result.fast.all_finite &&
        result.slow.all_finite && result.prediction.all_finite &&
        result.segmenter.all_finite;
    return result;
}

G1LossMetrics summarize_g1_loss(double total_nats, std::uint64_t target_count) {
    if (!std::isfinite(total_nats) || total_nats < 0.0 || target_count == 0)
        throw std::invalid_argument("loss summary requires finite non-negative loss and targets");
    constexpr double kLn2 = 0.693147180559945309417232121458176568;
    G1LossMetrics result;
    result.target_count = target_count;
    result.mean_nats_per_byte = total_nats / static_cast<double>(target_count);
    result.bits_per_byte = result.mean_nats_per_byte / kLn2;
    result.byte_perplexity = std::exp2(result.bits_per_byte);
    if (!std::isfinite(result.byte_perplexity))
        throw std::overflow_error("byte perplexity overflow");
    return result;
}

} // namespace morok::model
