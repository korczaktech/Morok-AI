#include "morok/model/g1_forward.hpp"

#include "morok/model/math/primitives.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace morok::model {
namespace {

constexpr std::size_t kDimension = kG1InputDimension;

void require_finite_state(const G1State& state) {
    for (const float value : state.fast) {
        if (!std::isfinite(value)) {
            throw std::domain_error("G1 fast state contains a non-finite value");
        }
    }
    for (const float value : state.slow) {
        if (!std::isfinite(value)) {
            throw std::domain_error("G1 slow state contains a non-finite value");
        }
    }
}

std::array<double, kG1CombinedStateDimension>
combine_states(const G1State& state) {
    std::array<double, kG1CombinedStateDimension> combined{};
    for (std::size_t i = 0; i < kDimension; ++i) {
        combined[i] = static_cast<double>(state.fast[i]);
        combined[kDimension + i] = static_cast<double>(state.slow[i]);
    }
    return combined;
}

std::array<double, kG1PredictionBits>
compute_base_logits(const G1Parameters& parameters, const G1State& state) {
    require_finite_state(state);
    const auto combined = combine_states(state);
    std::array<double, kG1PredictionBits> logits{};

    for (std::size_t bit = 0; bit < kG1PredictionBits; ++bit) {
        double value = static_cast<double>(parameters.c[bit]);
        for (std::size_t column = 0; column < kG1CombinedStateDimension; ++column) {
            value += static_cast<double>(
                parameters.O[bit * kG1CombinedStateDimension + column]) * combined[column];
        }
        if (!std::isfinite(value)) {
            throw std::domain_error("G1 prediction head produced a non-finite logit");
        }
        logits[bit] = value;
    }
    return logits;
}

double conditional_logit(const G1Parameters& parameters,
                         const std::array<double, kG1PredictionBits>& base_logits,
                         std::size_t bit,
                         std::uint8_t prefix_byte) {
    double value = base_logits[bit];
    for (std::size_t previous = 0; previous < bit; ++previous) {
        const std::size_t coefficient_index = bit * (bit - 1U) / 2U + previous;
        const bool previous_bit =
            (prefix_byte & static_cast<std::uint8_t>(1U << (7U - previous))) != 0;
        const double signed_bit = previous_bit ? 1.0 : -1.0;
        value += static_cast<double>(parameters.A[coefficient_index]) * signed_bit;
    }
    if (!std::isfinite(value)) {
        throw std::domain_error("G1 conditional prediction produced a non-finite logit");
    }
    return value;
}

} // namespace

std::array<float, kG1InputDimension> encode_g1_byte(std::uint8_t byte) noexcept {
    std::array<float, kG1InputDimension> features{};
    unsigned int ones = 0;

    for (std::size_t bit = 0; bit < 8; ++bit) {
        const bool set = (byte & static_cast<std::uint8_t>(1U << (7U - bit))) != 0;
        features[bit] = set ? 1.0F : -1.0F;
        ones += set ? 1U : 0U;
    }

    features[8] = (ones % 2U == 0U) ? 1.0F : -1.0F;
    features[9] = static_cast<float>(ones) / 4.0F - 1.0F;
    features[10] = static_cast<float>(byte) / 127.5F - 1.0F;
    return features;
}

void advance_g1_state(const G1Parameters& parameters, G1State& state,
                      std::uint8_t observed_byte) {
    require_finite_state(state);
    const auto input = encode_g1_byte(observed_byte);

    std::array<double, kDimension> next_fast{};
    std::array<double, kDimension> next_slow{};

    for (std::size_t row = 0; row < kDimension; ++row) {
        double gate_logit = static_cast<double>(parameters.b_a[row]);
        double candidate_logit = static_cast<double>(parameters.b_c[row]);

        for (std::size_t column = 0; column < kDimension; ++column) {
            const std::size_t index = row * kDimension + column;
            gate_logit += static_cast<double>(parameters.W_ax[index]) * input[column];
            gate_logit += static_cast<double>(parameters.W_ah[index]) * state.fast[column];
            candidate_logit += static_cast<double>(parameters.W_cx[index]) * input[column];
            candidate_logit += static_cast<double>(parameters.W_ch[index]) * state.fast[column];
        }

        const double gate = math::sigmoid(gate_logit);
        const double candidate = math::tanh_activation(candidate_logit);
        next_fast[row] = gate * static_cast<double>(state.fast[row]) +
                         (1.0 - gate) * candidate;
        if (!std::isfinite(next_fast[row])) {
            throw std::domain_error("G1 fast state update became non-finite");
        }
    }

    for (std::size_t row = 0; row < kDimension; ++row) {
        double gate_logit = static_cast<double>(parameters.b_g[row]);
        for (std::size_t column = 0; column < kDimension; ++column) {
            const std::size_t index = row * kDimension + column;
            gate_logit += static_cast<double>(parameters.W_gh[index]) * next_fast[column];
            gate_logit += static_cast<double>(parameters.W_gs[index]) * state.slow[column];
        }

        const double gate = math::sigmoid(gate_logit);
        next_slow[row] = gate * static_cast<double>(state.slow[row]) +
                         (1.0 - gate) * next_fast[row];
        if (!std::isfinite(next_slow[row])) {
            throw std::domain_error("G1 slow state update became non-finite");
        }
    }

    // Commit only after the complete update succeeds; state remains unchanged on failure.
    for (std::size_t i = 0; i < kDimension; ++i) {
        state.fast[i] = static_cast<float>(next_fast[i]);
        state.slow[i] = static_cast<float>(next_slow[i]);
    }
    require_finite_state(state);
}

G1Prediction predict_g1_next_byte(const G1Parameters& parameters,
                                  const G1State& state) {
    G1Prediction prediction{};
    prediction.base_logits = compute_base_logits(parameters, state);
    std::uint8_t generated = 0;

    for (std::size_t bit = 0; bit < kG1PredictionBits; ++bit) {
        const double logit = conditional_logit(parameters, prediction.base_logits,
                                               bit, generated);
        const double probability = math::sigmoid(logit);
        prediction.conditional_logits[bit] = logit;
        prediction.bit_probabilities[bit] = probability;
        if (logit >= 0.0) {
            generated = static_cast<std::uint8_t>(
                generated | static_cast<std::uint8_t>(1U << (7U - bit)));
        }
    }

    prediction.greedy_byte = generated;
    return prediction;
}

G1Evaluation evaluate_g1_next_byte(const G1Parameters& parameters,
                                   const G1State& state,
                                   std::uint8_t target_byte) {
    G1Evaluation evaluation{};
    const auto base_logits = compute_base_logits(parameters, state);

    for (std::size_t bit = 0; bit < kG1PredictionBits; ++bit) {
        const double logit = conditional_logit(parameters, base_logits, bit, target_byte);
        const double target =
            (target_byte & static_cast<std::uint8_t>(1U << (7U - bit))) != 0 ? 1.0 : 0.0;
        evaluation.conditional_logits[bit] = logit;
        evaluation.negative_log_likelihood_nats +=
            math::binary_cross_entropy_with_logits(logit, target);
    }

    if (!std::isfinite(evaluation.negative_log_likelihood_nats)) {
        throw std::domain_error("G1 byte loss became non-finite");
    }
    return evaluation;
}

} // namespace morok::model
