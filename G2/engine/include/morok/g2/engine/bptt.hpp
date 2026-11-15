#pragma once

#include "morok/g2/engine/forward.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace morok::model {

inline constexpr std::size_t kG2ExpertMaxBpttTruncationLength = 128;

struct G2ExpertGradients {
    std::array<double, kG2ExpertParameterCount> values{};

    void reset() noexcept { values.fill(0.0); }
    [[nodiscard]] double global_norm() const noexcept;
};

struct G2ExpertBpttResult {
    double mean_loss_nats{0.0};
    std::size_t target_byte_count{0};
    G2ExpertGradients gradients{};
    G2ExpertState final_state{};
};

struct G2ExpertEosResult {
    double loss_nats{0.0};
    G2ExpertGradients gradients{};
};

// initial_state is the state immediately before bytes[0] is consumed.
// final_state is immediately before consuming the final target byte; overlap
// that target as the next window's first context byte to preserve continuity.
// The maximum window is bounded to 128 target bytes to cap the temporary tape.
[[nodiscard]] G2ExpertBpttResult compute_g2_expert_bptt_gradients(
    const G2ExpertParameters& parameters,
    std::span<const std::uint8_t> bytes,
    std::size_t truncation_length,
    const G2ExpertState& initial_state = {},
    std::size_t context_offset = 0);

// Computes the learned stop-head loss for a document boundary or continuation.
// The recurrent state is treated as input; this helper updates only EOS values.
[[nodiscard]] G2ExpertEosResult compute_g2_expert_eos_gradients(
    const G2ExpertParameters& parameters, const G2ExpertState& state,
    std::size_t context_length, bool should_stop);

} // namespace morok::model
