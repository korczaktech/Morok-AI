#pragma once
#include "morok/model/g1_forward.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace morok::model {
struct G1Gradients { std::array<double,kG1ParameterCount> values{}; void reset() noexcept {values.fill(0.0);} [[nodiscard]] double global_norm() const noexcept; };
struct G1BpttResult { double mean_loss_nats{0}; std::size_t target_byte_count{0}; G1Gradients gradients{}; G1State final_state{}; };
// initial_state is the state before bytes[0]. final_state is before consuming the final target byte;
// overlap that target as the next window's first context byte to preserve causal continuity.
[[nodiscard]] G1BpttResult compute_g1_bptt_gradients(const G1Parameters&,std::span<const std::uint8_t>,std::size_t,const G1State& initial_state = {});
} // namespace morok::model
