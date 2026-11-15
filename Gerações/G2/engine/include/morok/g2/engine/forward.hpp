#pragma once

#include "morok/g2/engine/parameters.hpp"

#include <array>
#include <cstdint>

namespace morok::g2::engine {

struct G2ExpertState {
    std::array<float, kG2ExpertFastStateDimension> fast{};
    std::array<float, kG2ExpertSlowStateDimension> slow{};

    void reset() noexcept {
        fast.fill(0.0F);
        slow.fill(0.0F);
    }
};

struct G2ExpertPrediction {
    std::array<double, kG2ExpertPredictionBits> base_logits{};
    std::array<double, kG2ExpertPredictionBits> conditional_logits{};
    std::array<double, kG2ExpertPredictionBits> bit_probabilities{};
    std::uint8_t greedy_byte{0};
};

struct G2ExpertEvaluation {
    // Sum of the eight conditional BCE terms, measured in nats per target byte.
    double negative_log_likelihood_nats{0.0};
    std::array<double, kG2ExpertPredictionBits> conditional_logits{};
};

// Eight MSB-first bit features, even/odd parity, normalized popcount and byte value.
[[nodiscard]] std::array<float, kG2ExpertInputDimension>
encode_g2_expert_byte(std::uint8_t byte) noexcept;

// Consume one observed byte and update both recurrent states.
// The caller must reset state at the start of each independent document/session.
void advance_g2_expert_state(const G2ExpertParameters& parameters, G2ExpertState& state,
                      std::uint8_t observed_byte);

// Predict the next byte from the current state using greedy conditional bit decoding.
[[nodiscard]] G2ExpertPrediction predict_g2_expert_next_byte(const G2ExpertParameters& parameters,
                                                const G2ExpertState& state);

// Score a known next byte without leaking its bits into the recurrent state.
// The target bits are used only as prefixes for their later conditional bit logits.
[[nodiscard]] G2ExpertEvaluation evaluate_g2_expert_next_byte(const G2ExpertParameters& parameters,
                                                  const G2ExpertState& state,
                                                  std::uint8_t target_byte);

} // namespace morok::g2::engine
