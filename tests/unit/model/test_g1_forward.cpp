#include "morok/model/g1_forward.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(double left, double right, double tolerance = 1e-6) {
    return std::abs(left - right) <= tolerance;
}

void test_byte_encoding() {
    using namespace morok::model;
    const auto zero = encode_g1_byte(0x00);
    for (std::size_t i = 0; i < 8; ++i) {
        require(zero[i] == -1.0F, "zero byte bits must encode as -1");
    }
    require(zero[8] == 1.0F, "zero has even parity");
    require(zero[9] == -1.0F, "zero popcount normalization");
    require(zero[10] == -1.0F, "zero byte value normalization");

    const auto high_bit = encode_g1_byte(0x80);
    require(high_bit[0] == 1.0F && high_bit[1] == -1.0F,
            "byte bits must be encoded most-significant first");
    require(high_bit[8] == -1.0F, "one set bit has odd parity");
    require(high_bit[9] == -0.75F, "one set bit popcount normalization");
    require(high_bit[10] > -1.0F && high_bit[10] < 1.0F,
            "byte value feature must be normalized");
}

void test_zero_parameters_produce_uniform_distribution() {
    using namespace morok::model;
    G1Parameters parameters{};
    G1State state{};
    const auto prediction = predict_g1_next_byte(parameters, state);
    require(prediction.greedy_byte == 0xFF, "zero logits must choose one for every bit under the >= 0 tie rule");
    for (std::size_t bit = 0; bit < 8; ++bit) {
        require(close(prediction.bit_probabilities[bit], 0.5),
                "zero logits must yield probability 0.5 regardless of tie-breaking");
    }

    double total_probability = 0.0;
    for (unsigned int value = 0; value < 256; ++value) {
        const auto score = evaluate_g1_next_byte(
            parameters, state, static_cast<std::uint8_t>(value));
        total_probability += std::exp(-score.negative_log_likelihood_nats);
        require(close(score.negative_log_likelihood_nats, 8.0 * std::log(2.0)),
                "uniform byte loss must equal eight bits in nats");
    }
    require(close(total_probability, 1.0, 1e-10),
            "joint probabilities over all 256 bytes must sum to one");
}

void test_state_update_and_prediction_are_deterministic() {
    using namespace morok::model;
    G1Parameters parameters{};
    initialize_g1_parameters(parameters, 42);
    G1State first{};
    G1State second{};

    for (const std::uint8_t byte : std::array<std::uint8_t, 5>{'M', 'o', 'r', 'o', 'k'}) {
        advance_g1_state(parameters, first, byte);
        advance_g1_state(parameters, second, byte);
    }

    require(first.fast == second.fast && first.slow == second.slow,
            "same parameters and byte sequence must produce identical states");
    const auto prediction_a = predict_g1_next_byte(parameters, first);
    const auto prediction_b = predict_g1_next_byte(parameters, second);
    require(prediction_a.greedy_byte == prediction_b.greedy_byte,
            "same state must produce same greedy next byte");
    require(prediction_a.base_logits == prediction_b.base_logits,
            "same state must produce identical logits");
    for (double probability : prediction_a.bit_probabilities) {
        require(std::isfinite(probability) && probability >= 0.0 && probability <= 1.0,
                "bit probabilities must be finite and in [0,1]");
    }
}

void test_causality_and_target_does_not_mutate_state() {
    using namespace morok::model;
    G1Parameters parameters{};
    initialize_g1_parameters(parameters, 123);
    G1State context{};
    advance_g1_state(parameters, context, 'A');

    const auto before = predict_g1_next_byte(parameters, context);
    const auto state_before = context;
    static_cast<void>(evaluate_g1_next_byte(parameters, context, 0xF3));
    require(context.fast == state_before.fast && context.slow == state_before.slow,
            "scoring a target must not mutate recurrent state");

    G1State with_future = context;
    advance_g1_state(parameters, with_future, 'Z');
    const auto after = predict_g1_next_byte(parameters, state_before);
    require(before.base_logits == after.base_logits,
            "future bytes must not alter an earlier prediction");
}

void test_reset_and_non_finite_state_rejection() {
    using namespace morok::model;
    G1Parameters parameters{};
    initialize_g1_parameters(parameters, 9);
    G1State state{};
    advance_g1_state(parameters, state, 'x');
    state.reset();
    for (float value : state.fast) {
        require(value == 0.0F, "reset must clear fast state");
    }
    for (float value : state.slow) {
        require(value == 0.0F, "reset must clear slow state");
    }

    state.fast[0] = std::numeric_limits<float>::quiet_NaN();
    bool rejected = false;
    try {
        advance_g1_state(parameters, state, 'y');
    } catch (const std::domain_error&) {
        rejected = true;
    }
    require(rejected, "non-finite recurrent state must be rejected");
}

void test_recurrent_state_changes_with_input() {
    using namespace morok::model;
    G1Parameters parameters{};
    initialize_g1_parameters(parameters, 77);
    G1State zeros{};
    G1State text{};
    advance_g1_state(parameters, text, 'a');
    require(zeros.fast != text.fast || zeros.slow != text.slow,
            "nonzero initialized network should react to a nonzero input");
}

} // namespace

int main() {
    try {
        test_byte_encoding();
        test_zero_parameters_produce_uniform_distribution();
        test_state_update_and_prediction_are_deterministic();
        test_causality_and_target_does_not_mutate_state();
        test_reset_and_non_finite_state_rejection();
        test_recurrent_state_changes_with_input();
    } catch (const std::exception& error) {
        std::cerr << "G1 forward test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
