#pragma once

#include "morok/g2/engine/bptt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace morok::model {

enum class G2ExpertOptimizerKind : std::uint8_t {
    AdamW = 1,
    SGDMomentum = 2
};

struct G2ExpertOptimizerConfig {
    G2ExpertOptimizerKind kind{G2ExpertOptimizerKind::AdamW};
    double learning_rate{0.001};
    double beta1{0.9};
    double beta2{0.999};
    double epsilon{1.0e-8};
    double momentum{0.9};
    double weight_decay{0.0001};
    double max_gradient_norm{1.0};

    [[nodiscard]] static G2ExpertOptimizerConfig adamw() noexcept;
    [[nodiscard]] static G2ExpertOptimizerConfig sgd_momentum() noexcept;
};

struct G2ExpertAdamWState {
    std::array<float, kG2ExpertParameterCount> first_moment{};
    std::array<float, kG2ExpertParameterCount> second_moment{};
    std::uint64_t step{0};
};

struct G2ExpertSGDMomentumState {
    std::array<float, kG2ExpertParameterCount> velocity{};
    std::uint64_t step{0};
};

using G2ExpertOptimizerState = std::variant<G2ExpertAdamWState, G2ExpertSGDMomentumState>;

struct G2ExpertOptimizerStepResult {
    std::uint64_t step{0};
    double gradient_norm_before_clipping{0.0};
    double gradient_norm_after_clipping{0.0};
    bool gradients_clipped{false};
};

[[nodiscard]] G2ExpertOptimizerState make_g2_expert_optimizer_state(G2ExpertOptimizerKind kind);

// Applies global-norm clipping and one optimizer update transactionally.
// AdamW uses decoupled decay; SGD momentum uses L2 decay inside its gradient.
// Decay is applied to matrices/weights only, never to biases.
[[nodiscard]] G2ExpertOptimizerStepResult apply_g2_expert_optimizer_step(
    G2ExpertParameters& parameters,
    const G2ExpertGradients& gradients,
    const G2ExpertOptimizerConfig& config,
    G2ExpertOptimizerState& state);

[[nodiscard]] bool g2_expert_parameter_uses_weight_decay(std::size_t canonical_index) noexcept;

} // namespace morok::model
