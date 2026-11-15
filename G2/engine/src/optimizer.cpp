#include "morok/g2/engine/optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace morok::model {
namespace {

void validate_config(const G2ExpertOptimizerConfig& c) {
    if (!std::isfinite(c.learning_rate) || c.learning_rate <= 0.0 ||
        !std::isfinite(c.weight_decay) || c.weight_decay < 0.0 ||
        !std::isfinite(c.max_gradient_norm) || c.max_gradient_norm <= 0.0) {
        throw std::invalid_argument("optimizer learning rate, weight decay or clipping limit is invalid");
    }
    if (c.kind == G2ExpertOptimizerKind::AdamW) {
        if (!std::isfinite(c.beta1) || c.beta1 < 0.0 || c.beta1 >= 1.0 ||
            !std::isfinite(c.beta2) || c.beta2 < 0.0 || c.beta2 >= 1.0 ||
            !std::isfinite(c.epsilon) || c.epsilon <= 0.0) {
            throw std::invalid_argument("AdamW beta values and epsilon are invalid");
        }
    } else if (c.kind == G2ExpertOptimizerKind::SGDMomentum) {
        if (!std::isfinite(c.momentum) || c.momentum < 0.0 || c.momentum >= 1.0) {
            throw std::invalid_argument("SGD momentum must be finite and in [0, 1)");
        }
    } else {
        throw std::invalid_argument("unknown G2Expert optimizer kind");
    }
}

double norm_of(const G2ExpertGradients& gradients) {
    double norm = 0.0;
    for (const double value : gradients.values) {
        if (!std::isfinite(value)) {
            throw std::domain_error("optimizer received a non-finite gradient");
        }
        norm = std::hypot(norm, value);
    }
    return norm;
}

bool state_matches(G2ExpertOptimizerKind kind, const G2ExpertOptimizerState& state) noexcept {
    return (kind == G2ExpertOptimizerKind::AdamW && std::holds_alternative<G2ExpertAdamWState>(state)) ||
           (kind == G2ExpertOptimizerKind::SGDMomentum && std::holds_alternative<G2ExpertSGDMomentumState>(state));
}

void require_finite_parameters(const G2ExpertParameters& parameters) {
    const auto flat = parameters.flatten();
    for (const float value : flat) {
        if (!std::isfinite(value)) {
            throw std::domain_error("optimizer received non-finite model parameters");
        }
    }
}

void require_finite_state(const G2ExpertAdamWState& state) {
    for (std::size_t i = 0; i < kG2ExpertParameterCount; ++i) {
        if (!std::isfinite(state.first_moment[i]) ||
            !std::isfinite(state.second_moment[i]) ||
            state.second_moment[i] < 0.0F) {
            throw std::domain_error("AdamW state is non-finite or has a negative second moment");
        }
    }
}

void require_finite_state(const G2ExpertSGDMomentumState& state) {
    for (const float value : state.velocity) {
        if (!std::isfinite(value)) {
            throw std::domain_error("SGD momentum state is non-finite");
        }
    }
}

} // namespace

G2ExpertOptimizerConfig G2ExpertOptimizerConfig::adamw() noexcept {
    return G2ExpertOptimizerConfig{};
}

G2ExpertOptimizerConfig G2ExpertOptimizerConfig::sgd_momentum() noexcept {
    G2ExpertOptimizerConfig config{};
    config.kind = G2ExpertOptimizerKind::SGDMomentum;
    config.learning_rate = 0.01;
    config.momentum = 0.9;
    config.weight_decay = 0.0001;
    return config;
}

G2ExpertOptimizerState make_g2_expert_optimizer_state(G2ExpertOptimizerKind kind) {
    if (kind == G2ExpertOptimizerKind::AdamW) return G2ExpertAdamWState{};
    if (kind == G2ExpertOptimizerKind::SGDMomentum) return G2ExpertSGDMomentumState{};
    throw std::invalid_argument("unknown G2Expert optimizer kind");
}

bool g2_expert_parameter_uses_weight_decay(std::size_t index) noexcept {
    if (index >= kG2ExpertParameterCount) return false;
    return index < 484U || (index >= 506U && index < 748U) ||
           (index >= 759U && index < 935U) ||
           (index >= 943U && index < 971U) ||
           (index >= 971U && index < 993U);
}

G2ExpertOptimizerStepResult apply_g2_expert_optimizer_step(
    G2ExpertParameters& parameters, const G2ExpertGradients& gradients,
    const G2ExpertOptimizerConfig& config, G2ExpertOptimizerState& state) {
    validate_config(config);
    if (!state_matches(config.kind, state)) {
        throw std::invalid_argument("optimizer state kind does not match optimizer configuration");
    }
    require_finite_parameters(parameters);

    const double original_norm = norm_of(gradients);
    const double clip_scale = original_norm > config.max_gradient_norm
        ? config.max_gradient_norm / original_norm : 1.0;
    const double clipped_norm = original_norm * clip_scale;

    auto candidate_parameters = parameters.flatten();
    G2ExpertOptimizerState candidate_state = state;
    std::uint64_t next_step = 0;

    if (config.kind == G2ExpertOptimizerKind::AdamW) {
        auto& adam = std::get<G2ExpertAdamWState>(candidate_state);
        require_finite_state(adam);
        if (adam.step == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("AdamW step counter overflow");
        }
        next_step = adam.step + 1U;
        const double correction1 = 1.0 - std::pow(config.beta1, static_cast<double>(next_step));
        const double correction2 = 1.0 - std::pow(config.beta2, static_cast<double>(next_step));

        for (std::size_t i = 0; i < kG2ExpertParameterCount; ++i) {
            const double g = gradients.values[i] * clip_scale;
            const double m = config.beta1 * adam.first_moment[i] + (1.0 - config.beta1) * g;
            const double v = config.beta2 * adam.second_moment[i] + (1.0 - config.beta2) * g * g;
            const double m_hat = m / correction1;
            const double v_hat = v / correction2;
            double updated = candidate_parameters[i];
            if (g2_expert_parameter_uses_weight_decay(i)) {
                updated *= 1.0 - config.learning_rate * config.weight_decay;
            }
            updated -= config.learning_rate * m_hat / (std::sqrt(v_hat) + config.epsilon);
            const float stored_m = static_cast<float>(m);
            const float stored_v = static_cast<float>(v);
            const float stored_parameter = static_cast<float>(updated);
            if (!std::isfinite(stored_m) || !std::isfinite(stored_v) ||
                !std::isfinite(stored_parameter) || stored_v < 0.0F) {
                throw std::domain_error("AdamW update produced a non-finite value");
            }
            adam.first_moment[i] = stored_m;
            adam.second_moment[i] = stored_v;
            candidate_parameters[i] = stored_parameter;
        }
        adam.step = next_step;
    } else {
        auto& sgd = std::get<G2ExpertSGDMomentumState>(candidate_state);
        require_finite_state(sgd);
        if (sgd.step == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("SGD step counter overflow");
        }
        next_step = sgd.step + 1U;
        for (std::size_t i = 0; i < kG2ExpertParameterCount; ++i) {
            double g = gradients.values[i] * clip_scale;
            if (g2_expert_parameter_uses_weight_decay(i)) {
                g += config.weight_decay * candidate_parameters[i];
            }
            const double velocity = config.momentum * sgd.velocity[i] + g;
            const double updated = candidate_parameters[i] - config.learning_rate * velocity;
            const float stored_velocity = static_cast<float>(velocity);
            const float stored_parameter = static_cast<float>(updated);
            if (!std::isfinite(stored_velocity) || !std::isfinite(stored_parameter)) {
                throw std::domain_error("SGD momentum update produced a non-finite value");
            }
            sgd.velocity[i] = stored_velocity;
            candidate_parameters[i] = stored_parameter;
        }
        sgd.step = next_step;
    }

    // Commit only after every parameter and optimizer-state element is valid.
    if (!parameters.assign(candidate_parameters)) {
        throw std::domain_error("optimizer candidate parameters failed validation");
    }
    state = std::move(candidate_state);

    return G2ExpertOptimizerStepResult{
        next_step, original_norm, clipped_norm,
        original_norm > config.max_gradient_norm
    };
}

} // namespace morok::model
