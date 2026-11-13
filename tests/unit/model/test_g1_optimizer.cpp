#include "morok/model/learn/optimizer.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_default_configs_and_state_types() {
    using namespace morok::model;
    const auto adam = G1OptimizerConfig::adamw();
    require(adam.kind == G1OptimizerKind::AdamW, "AdamW must be the default optimizer");
    require(adam.learning_rate == 0.001 && adam.beta1 == 0.9 &&
            adam.beta2 == 0.999 && adam.epsilon == 1.0e-8 &&
            adam.weight_decay == 0.0001 && adam.max_gradient_norm == 1.0,
            "AdamW defaults do not match the G1 specification");
    const auto sgd = G1OptimizerConfig::sgd_momentum();
    require(sgd.kind == G1OptimizerKind::SGDMomentum &&
            sgd.learning_rate == 0.01 && sgd.momentum == 0.9 &&
            sgd.weight_decay == 0.0001,
            "SGD momentum defaults do not match the G1 specification");
    require(std::holds_alternative<G1AdamWState>(make_g1_optimizer_state(adam.kind)),
            "AdamW factory returned the wrong state");
    require(std::holds_alternative<G1SGDMomentumState>(make_g1_optimizer_state(sgd.kind)),
            "SGD factory returned the wrong state");
}

void test_weight_decay_mask() {
    using namespace morok::model;
    for (std::size_t i = 0; i < 1000; ++i) {
        const bool expected = i < 484 || (i >= 506 && i < 748) ||
            (i >= 759 && i < 935) || (i >= 943 && i < 971);
        require(g1_parameter_uses_weight_decay(i) == expected,
                "weight-decay mask does not match canonical parameter groups");
    }
    require(!g1_parameter_uses_weight_decay(1000),
            "out-of-range parameter index must not use weight decay");
}

void test_adamw_update_and_bias_exclusion() {
    using namespace morok::model;
    G1Parameters parameters{};
    parameters.W_ax[0] = 2.0F;
    parameters.b_a[0] = 2.0F;
    parameters.v[0] = 2.0F;
    parameters.theta_len[0] = 2.0F;
    G1Gradients gradients{};
    G1OptimizerConfig config = G1OptimizerConfig::adamw();
    G1OptimizerState state = make_g1_optimizer_state(config.kind);

    const auto result = apply_g1_optimizer_step(parameters, gradients, config, state);
    require(result.step == 1 && !result.gradients_clipped,
            "AdamW first-step metadata is incorrect");
    require(std::abs(parameters.W_ax[0] - 2.0F * (1.0F - 0.001F * 0.0001F)) < 1e-6F,
            "AdamW must decouple weight decay for matrices");
    require(parameters.b_a[0] == 2.0F && parameters.theta_len[0] == 2.0F,
            "AdamW must not decay biases");
    require(parameters.v[0] == 2.0F,
            "the segmenter must remain outside the B0 optimizer update");
    const auto& adam = std::get<G1AdamWState>(state);
    require(adam.step == 1 && adam.first_moment[0] == 0.0F &&
            adam.second_moment[0] == 0.0F,
            "AdamW moments or step count are incorrect");
}

void test_adamw_bias_correction_and_clipping() {
    using namespace morok::model;
    G1Parameters parameters{};
    G1Gradients gradients{};
    gradients.values[0] = 3.0;
    gradients.values[1] = 4.0;
    auto config = G1OptimizerConfig::adamw();
    G1OptimizerState state = make_g1_optimizer_state(config.kind);
    const auto result = apply_g1_optimizer_step(parameters, gradients, config, state);
    require(std::abs(result.gradient_norm_before_clipping - 5.0) < 1e-12,
            "global gradient norm must be measured before clipping");
    require(std::abs(result.gradient_norm_after_clipping - 1.0) < 1e-12 &&
            result.gradients_clipped,
            "global clipping must cap the gradient norm at one");
    require(std::abs(parameters.W_ax[0] + 0.001F) < 1e-6F &&
            std::abs(parameters.W_ax[1] + 0.001F) < 1e-6F,
            "AdamW must use the clipped gradient and first-step bias correction");
    const auto& adam = std::get<G1AdamWState>(state);
    require(std::abs(adam.first_moment[0] - 0.06F) < 1e-7F &&
            std::abs(adam.second_moment[0] - 0.00036F) < 1e-7F,
            "AdamW moment estimates are incorrect");
}

void test_sgd_momentum_and_decay_policy() {
    using namespace morok::model;
    G1Parameters parameters{};
    parameters.W_ax[0] = 2.0F;
    parameters.b_a[0] = 2.0F;
    G1Gradients gradients{};
    gradients.values[0] = 2.0;
    gradients.values[484] = 2.0;
    auto config = G1OptimizerConfig::sgd_momentum();
    config.learning_rate = 0.1;
    config.weight_decay = 0.01;
    config.max_gradient_norm = 10.0;
    G1OptimizerState state = make_g1_optimizer_state(config.kind);
    const auto result = apply_g1_optimizer_step(parameters, gradients, config, state);
    require(std::abs(result.gradient_norm_before_clipping - std::sqrt(8.0)) < 1e-12,
            "SGD gradient norm is incorrect");
    const double expected_weight = 2.0 - 0.1 * (2.0 + 0.01 * 2.0);
    const double expected_bias = 2.0 - 0.1 * 2.0;
    require(std::abs(parameters.W_ax[0] - expected_weight) < 1e-6,
            "SGD must apply L2 decay to weights");
    require(std::abs(parameters.b_a[0] - expected_bias) < 1e-6,
            "SGD must not apply decay to biases");
    const auto& sgd = std::get<G1SGDMomentumState>(state);
    require(sgd.step == 1 && std::abs(sgd.velocity[0] - 2.02F) < 1e-6F,
            "SGD momentum buffer is incorrect");
}

void test_optimizer_state_isolation_and_mismatch() {
    using namespace morok::model;
    G1Parameters parameters{};
    G1Gradients gradients{};
    auto adam_config = G1OptimizerConfig::adamw();
    auto sgd_state = make_g1_optimizer_state(G1OptimizerKind::SGDMomentum);
    bool rejected = false;
    try { (void)apply_g1_optimizer_step(parameters, gradients, adam_config, sgd_state); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "optimizer must reject state belonging to another optimizer");

    auto sgd_config = G1OptimizerConfig::sgd_momentum();
    auto adam_state = make_g1_optimizer_state(G1OptimizerKind::AdamW);
    rejected = false;
    try { (void)apply_g1_optimizer_step(parameters, gradients, sgd_config, adam_state); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "SGD must reject AdamW state");
}

void test_invalid_gradient_is_transactional() {
    using namespace morok::model;
    G1Parameters parameters{};
    parameters.W_ax[0] = 0.25F;
    G1Gradients gradients{};
    gradients.values[17] = std::numeric_limits<double>::quiet_NaN();
    auto config = G1OptimizerConfig::adamw();
    auto state = make_g1_optimizer_state(config.kind);
    const auto before = parameters.flatten();
    bool rejected = false;
    try { (void)apply_g1_optimizer_step(parameters, gradients, config, state); }
    catch (const std::domain_error&) { rejected = true; }
    require(rejected, "optimizer must reject NaN gradients");
    require(parameters.flatten() == before,
            "rejected update must not modify model parameters");
    require(std::get<G1AdamWState>(state).step == 0,
            "rejected update must not modify optimizer state");
}

void test_invalid_config_and_overflow_are_transactional() {
    using namespace morok::model;
    G1Parameters parameters{};
    parameters.W_ax[0] = 0.5F;
    G1Gradients gradients{};
    gradients.values[0] = 1.0;
    auto config = G1OptimizerConfig::adamw();
    config.beta1 = 1.0;
    auto state = make_g1_optimizer_state(config.kind);
    const auto before = parameters.flatten();
    bool rejected = false;
    try { (void)apply_g1_optimizer_step(parameters, gradients, config, state); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && parameters.flatten() == before,
            "invalid optimizer config must not mutate parameters");

    config = G1OptimizerConfig::adamw();
    config.learning_rate = 3.5e38;
    rejected = false;
    try { (void)apply_g1_optimizer_step(parameters, gradients, config, state); }
    catch (const std::domain_error&) { rejected = true; }
    require(rejected && parameters.flatten() == before &&
            std::get<G1AdamWState>(state).step == 0,
            "overflowing update must be rejected transactionally");
}

} // namespace

int main() {
    try {
        test_default_configs_and_state_types();
        test_weight_decay_mask();
        test_adamw_update_and_bias_exclusion();
        test_adamw_bias_correction_and_clipping();
        test_sgd_momentum_and_decay_policy();
        test_optimizer_state_isolation_and_mismatch();
        test_invalid_gradient_is_transactional();
        test_invalid_config_and_overflow_are_transactional();
        std::cout << "G1 optimizer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "G1 optimizer test failed: " << error.what() << '\n';
        return 1;
    }
}
