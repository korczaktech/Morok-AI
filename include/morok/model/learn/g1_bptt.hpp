#pragma once

#include "morok/model/g1_forward.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace morok::model {

// Gradients follow the canonical 1,000-parameter serialization order.
// The final 29 segmenter entries remain zero in G1 B0.
struct G1Gradients {
    std::array<double, kG1ParameterCount> values{};

    void reset() noexcept { values.fill(0.0); }
    [[nodiscard]] double global_norm() const noexcept;
};

// Mean next-byte negative log-likelihood and its gradient over bytes[1..N-1].
// The recurrent state is carried numerically between truncation blocks, while
// the reverse graph is cut at every block boundary.
struct G1BpttResult {
    double mean_loss_nats{0.0};
    std::size_t target_byte_count{0};
    G1Gradients gradients{};
    G1State final_state{};
};

[[nodiscard]] G1BpttResult compute_g1_bptt_gradients(
    const G1Parameters& parameters,
    std::span<const std::uint8_t> bytes,
    std::size_t truncation_length);

} // namespace morok::model
