#pragma once

#include "morok/model/learn/g1_bptt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace morok::model {

enum class G1OptimizerKind : std::uint8_t {
    AdamW = 1,
    SGDMomentum = 2
};

struct G1OptimizerConfig {
    G1OptimizerKind kind{G1OptimizerKind::AdamW};
    double learning_rate{0.001};
    double beta1{0.9};
    double beta2{0.999};
    double epsilon{1.0e-8};
    double momentum{0.9};
    double weight_decay{0.0001};
    double max_gradient_norm{1.0};

    [[nodiscard]] static G1OptimizerConfig adamw() noexcept;
    [[nodiscard]] static G1OptimizerConfig sgd_momentum() noexcept;
};

struct G1AdamWState {
    std::array<float, kG1ParameterCount> first_moment{};
    std::array<float, kG1ParameterCount> second_moment{};
    std::uint64_t step{0};
};

struct G1SGDMomentumState {
    std::array<float, kG1ParameterCount> velocity{};
    std::uint64_t step{0};
};

using G1OptimizerState = std::variant<G1AdamWState, G1SGDMomentumState>;

struct G1OptimizerStepResult {
    std::uint64_t step{0};
    double gradient_norm_before_clipping{0.0};
    double gradient_norm_after_clipping{0.0};
    bool gradients_clipped{false};
};

[[nodiscard]] G1OptimizerState make_g1_optimizer_state(G1OptimizerKind kind);

// Applies global-norm clipping and one optimizer update transactionally.
// AdamW uses decoupled decay; SGD momentum uses L2 decay inside its gradient.
// Decay is applied to matrices/weights only, never to biases.
[[nodiscard]] G1OptimizerStepResult apply_g1_optimizer_step(
    G1Parameters& parameters,
    const G1Gradients& gradients,
    const G1OptimizerConfig& config,
    G1OptimizerState& state);

[[nodiscard]] bool g1_parameter_uses_weight_decay(std::size_t canonical_index) noexcept;

} // namespace morok::model
