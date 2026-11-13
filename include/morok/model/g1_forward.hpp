#pragma once

#include "morok/model/parameters/g1_parameters.hpp"

#include <array>
#include <cstdint>

namespace morok::model {

struct G1State {
    std::array<float, kG1FastStateDimension> fast{};
    std::array<float, kG1SlowStateDimension> slow{};

    void reset() noexcept {
        fast.fill(0.0F);
        slow.fill(0.0F);
    }
};

struct G1Prediction {
    std::array<double, kG1PredictionBits> base_logits{};
    std::array<double, kG1PredictionBits> conditional_logits{};
    std::array<double, kG1PredictionBits> bit_probabilities{};
    std::uint8_t greedy_byte{0};
};

struct G1Evaluation {
    // Sum of the eight conditional BCE terms, measured in nats per target byte.
    double negative_log_likelihood_nats{0.0};
    std::array<double, kG1PredictionBits> conditional_logits{};
};

// Eight MSB-first bit features, even/odd parity, normalized popcount and byte value.
[[nodiscard]] std::array<float, kG1InputDimension>
encode_g1_byte(std::uint8_t byte) noexcept;

// Consume one observed byte and update both recurrent states.
// The caller must reset state at the start of each independent document/session.
void advance_g1_state(const G1Parameters& parameters, G1State& state,
                      std::uint8_t observed_byte);

// Predict the next byte from the current state using greedy conditional bit decoding.
[[nodiscard]] G1Prediction predict_g1_next_byte(const G1Parameters& parameters,
                                                const G1State& state);

// Score a known next byte without leaking its bits into the recurrent state.
// The target bits are used only as prefixes for their later conditional bit logits.
[[nodiscard]] G1Evaluation evaluate_g1_next_byte(const G1Parameters& parameters,
                                                  const G1State& state,
                                                  std::uint8_t target_byte);

} // namespace morok::model
