#include "morok/model/learn/optimizer.hpp"

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

void validate_config(const G1OptimizerConfig& c) {
    if (!std::isfinite(c.learning_rate) || c.learning_rate <= 0.0 ||
        !std::isfinite(c.weight_decay) || c.weight_decay < 0.0 ||
        !std::isfinite(c.max_gradient_norm) || c.max_gradient_norm <= 0.0) {
        throw std::invalid_argument("optimizer learning rate, weight decay or clipping limit is invalid");
    }
    if (c.kind == G1OptimizerKind::AdamW) {
        if (!std::isfinite(c.beta1) || c.beta1 < 0.0 || c.beta1 >= 1.0 ||
            !std::isfinite(c.beta2) || c.beta2 < 0.0 || c.beta2 >= 1.0 ||
            !std::isfinite(c.epsilon) || c.epsilon <= 0.0) {
            throw std::invalid_argument("AdamW beta values and epsilon are invalid");
        }
    } else if (c.kind == G1OptimizerKind::SGDMomentum) {
        if (!std::isfinite(c.momentum) || c.momentum < 0.0 || c.momentum >= 1.0) {
            throw std::invalid_argument("SGD momentum must be finite and in [0, 1)");
        }
    } else {
        throw std::invalid_argument("unknown G1 optimizer kind");
    }
}

double norm_of(const G1Gradients& gradients) {
    double norm = 0.0;
    for (const double value : gradients.values) {
        if (!std::isfinite(value)) {
            throw std::domain_error("optimizer received a non-finite gradient");
        }
        norm = std::hypot(norm, value);
    }
    return norm;
}

bool state_matches(G1OptimizerKind kind, const G1OptimizerState& state) noexcept {
    return (kind == G1OptimizerKind::AdamW && std::holds_alternative<G1AdamWState>(state)) ||
           (kind == G1OptimizerKind::SGDMomentum && std::holds_alternative<G1SGDMomentumState>(state));
}

void require_finite_parameters(const G1Parameters& parameters) {
    const auto flat = parameters.flatten();
    for (const float value : flat) {
        if (!std::isfinite(value)) {
            throw std::domain_error("optimizer received non-finite model parameters");
        }
    }
}

void require_finite_state(const G1AdamWState& state) {
    for (std::size_t i = 0; i < kG1ParameterCount; ++i) {
        if (!std::isfinite(state.first_moment[i]) ||
            !std::isfinite(state.second_moment[i]) ||
            state.second_moment[i] < 0.0F) {
            throw std::domain_error("AdamW state is non-finite or has a negative second moment");
        }
    }
}

void require_finite_state(const G1SGDMomentumState& state) {
    for (const float value : state.velocity) {
        if (!std::isfinite(value)) {
            throw std::domain_error("SGD momentum state is non-finite");
        }
    }
}

} // namespace

G1OptimizerConfig G1OptimizerConfig::adamw() noexcept {
    return G1OptimizerConfig{};
}

G1OptimizerConfig G1OptimizerConfig::sgd_momentum() noexcept {
    G1OptimizerConfig config{};
    config.kind = G1OptimizerKind::SGDMomentum;
    config.learning_rate = 0.01;
    config.momentum = 0.9;
    config.weight_decay = 0.0001;
    return config;
}

G1OptimizerState make_g1_optimizer_state(G1OptimizerKind kind) {
    if (kind == G1OptimizerKind::AdamW) return G1AdamWState{};
    if (kind == G1OptimizerKind::SGDMomentum) return G1SGDMomentumState{};
    throw std::invalid_argument("unknown G1 optimizer kind");
}

bool g1_parameter_uses_weight_decay(std::size_t index) noexcept {
    if (index >= kG1ParameterCount) return false;
    return index < 484U || (index >= 506U && index < 748U) ||
           (index >= 759U && index < 935U) ||
           (index >= 943U && index < 971U);
}

G1OptimizerStepResult apply_g1_optimizer_step(
    G1Parameters& parameters, const G1Gradients& gradients,
    const G1OptimizerConfig& config, G1OptimizerState& state) {
    validate_config(config);
    if (!state_matches(config.kind, state)) {
        throw std::invalid_argument("optimizer state kind does not match optimizer configuration");
    }
    for (std::size_t i = kG1ParameterCount - kG1SegmenterParameterCount;
         i < kG1ParameterCount; ++i) {
        if (gradients.values[i] != 0.0) {
            throw std::invalid_argument("B0 optimizer cannot update segmenter gradients");
        }
    }
    require_finite_parameters(parameters);

    const double original_norm = norm_of(gradients);
    const double clip_scale = original_norm > config.max_gradient_norm
        ? config.max_gradient_norm / original_norm : 1.0;
    const double clipped_norm = original_norm * clip_scale;

    auto candidate_parameters = parameters.flatten();
    G1OptimizerState candidate_state = state;
    std::uint64_t next_step = 0;

    if (config.kind == G1OptimizerKind::AdamW) {
        auto& adam = std::get<G1AdamWState>(candidate_state);
        require_finite_state(adam);
        if (adam.step == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("AdamW step counter overflow");
        }
        next_step = adam.step + 1U;
        const double correction1 = 1.0 - std::pow(config.beta1, static_cast<double>(next_step));
        const double correction2 = 1.0 - std::pow(config.beta2, static_cast<double>(next_step));

        for (std::size_t i = 0; i < kG1ParameterCount; ++i) {
            const double g = gradients.values[i] * clip_scale;
            const double m = config.beta1 * adam.first_moment[i] + (1.0 - config.beta1) * g;
            const double v = config.beta2 * adam.second_moment[i] + (1.0 - config.beta2) * g * g;
            const double m_hat = m / correction1;
            const double v_hat = v / correction2;
            double updated = candidate_parameters[i];
            if (g1_parameter_uses_weight_decay(i)) {
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
        auto& sgd = std::get<G1SGDMomentumState>(candidate_state);
        require_finite_state(sgd);
        if (sgd.step == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("SGD step counter overflow");
        }
        next_step = sgd.step + 1U;
        for (std::size_t i = 0; i < kG1ParameterCount; ++i) {
            double g = gradients.values[i] * clip_scale;
            if (g1_parameter_uses_weight_decay(i)) {
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

    return G1OptimizerStepResult{
        next_step, original_norm, clipped_norm,
        original_norm > config.max_gradient_norm
    };
}

} // namespace morok::model
