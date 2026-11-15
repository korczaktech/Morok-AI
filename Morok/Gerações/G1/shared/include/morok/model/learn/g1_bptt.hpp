#pragma once

#include "morok/model/g1_forward.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>

namespace morok::model {

inline constexpr std::size_t kG1MaxBpttTruncationLength = 512;

struct G1Gradients {
    std::array<double, kG1ParameterCount> values{};

    void reset() noexcept { values.fill(0.0); }
    [[nodiscard]] double global_norm() const noexcept;
};

struct G1BpttResult {
    double mean_loss_nats{0.0};
    std::size_t target_byte_count{0};
    G1Gradients gradients{};
    G1State final_state{};
};

struct G1EosResult {
    double loss_nats{0.0};
    G1Gradients gradients{};
};

// initial_state is the state immediately before bytes[0] is consumed.
// final_state is immediately before consuming the final target byte; overlap
// that target as the next window's first context byte to preserve continuity.
// The maximum window is bounded to 512 target bytes to cap the temporary tape.
// previous_context_byte supplies the byte immediately before bytes[0] when
// continuing a document, so the first lexical boundary feature remains exact.
[[nodiscard]] G1BpttResult compute_g1_bptt_gradients(
    const G1Parameters& parameters,
    std::span<const std::uint8_t> bytes,
    std::size_t truncation_length,
    const G1State& initial_state = {},
    std::size_t context_offset = 0,
    std::optional<std::uint8_t> previous_context_byte = std::nullopt);

// Computes the learned stop-head loss for a document boundary or continuation.
// The recurrent state is treated as input; this helper updates only EOS values.
[[nodiscard]] G1EosResult compute_g1_eos_gradients(
    const G1Parameters& parameters, const G1State& state,
    std::size_t context_length, bool should_stop);

} // namespace morok::model
