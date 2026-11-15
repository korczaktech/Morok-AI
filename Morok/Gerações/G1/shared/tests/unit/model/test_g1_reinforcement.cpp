#include "morok/model/learn/g1_bptt.hpp"
#include "morok/model/learn/optimizer.hpp"
#include "morok/model/parameters/g1_parameters.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {
morok::model::G1Parameters updated(const morok::model::G1Parameters& initial,
                                   const morok::model::G1Gradients& source,
                                   double reward) {
    auto parameters = initial;
    auto gradients = source;
    for (double& value : gradients.values) value *= reward;
    auto config = morok::model::G1OptimizerConfig::sgd_momentum();
    config.learning_rate = 0.00001;
    config.momentum = 0.0;
    config.weight_decay = 0.0;
    config.max_gradient_norm = 0.25;
    morok::model::G1OptimizerState state = morok::model::G1SGDMomentumState{};
    const auto step = morok::model::apply_g1_optimizer_step(
        parameters, gradients, config, state);
    assert(step.step == 1);
    return parameters;
}
}

int main() {
    using namespace morok::model;
    G1Parameters initial{};
    initialize_g1_parameters(initial, 20261114);

    const std::string prompt = "Pergunta: Que cor e a grama?\nResposta: ";
    const std::string response = "verde";
    G1State state{};
    bool has_previous = false;
    std::uint8_t previous = 0;
    for (std::size_t i = 0; i + 1 < prompt.size(); ++i) {
        const auto byte = static_cast<std::uint8_t>(
            static_cast<unsigned char>(prompt[i]));
        const bool separator = is_g1_token_separator(byte);
        const bool token_start = !separator &&
            (!has_previous || is_g1_token_separator(previous));
        advance_g1_state(initial, state, byte, token_start, separator);
        previous = byte;
        has_previous = true;
    }

    std::vector<std::uint8_t> sequence;
    sequence.push_back(static_cast<std::uint8_t>(
        static_cast<unsigned char>(prompt.back())));
    for (const unsigned char byte : response) sequence.push_back(byte);

    const auto previous_context = static_cast<std::uint8_t>(
        static_cast<unsigned char>(prompt[prompt.size() - 2]));
    const auto result = compute_g1_bptt_gradients(
        initial, sequence, 64, state, prompt.size() - 1, previous_context);
    assert(result.target_byte_count == response.size());
    assert(std::isfinite(result.gradients.global_norm()));
    assert(result.gradients.global_norm() > 0.0);

    const auto positive = updated(initial, result.gradients, 1.0);
    const auto negative = updated(initial, result.gradients, -1.0);
    const auto original_values = initial.flatten();
    const auto positive_values = positive.flatten();
    const auto negative_values = negative.flatten();

    bool changed = false;
    for (std::size_t i = 0; i < kG1ParameterCount; ++i) {
        assert(std::isfinite(positive_values[i]));
        assert(std::isfinite(negative_values[i]));
        changed = changed || positive_values[i] != original_values[i];
        const double midpoint =
            (static_cast<double>(positive_values[i]) + negative_values[i]) / 2.0;
        assert(std::abs(midpoint - original_values[i]) < 1.0e-6);
    }
    assert(changed);
    assert(G1Parameters::parameter_count() == 1000);
    return 0;
}
