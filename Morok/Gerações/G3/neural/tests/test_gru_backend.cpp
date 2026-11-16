#include "morok/g3/neural/gru_backend.hpp"
#include "morok/model/model_session.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace morok;
using namespace morok::g3::neural;

namespace {

void test_exact_parameter_budget_and_descriptor() {
    G3Backend backend;
    const auto descriptor = backend.descriptor();
    assert(descriptor.generation_id == 3U);
    assert(descriptor.architecture_id == kArchitectureId);
    assert(descriptor.vocabulary_size == kVocabularySize);
    assert(descriptor.state_size_bytes == 660U);
    assert(descriptor.state_alignment_bytes == alignof(float));
    assert(descriptor.supports_incremental_state);
    assert(count_parameters(backend.weights()) == 100000U);
    assert(backend.parameter_count() == 100000U);
    assert(descriptor.parameter_count == 100000U);

    auto& weights = backend.mutable_weights();
    weights.output_bias.pop_back();
    assert(backend.parameter_count() == 99999U);
    assert(backend.descriptor().parameter_count == 99999U);
}

void test_tensor_shapes_initialization_and_seed() {
    const G3Config config{123456U, 1.0e-5F};
    G3Backend first(config), second(config), different(G3Config{123457U, 1.0e-5F});
    const auto& a = first.weights();
    const auto& b = second.weights();
    const auto& c = different.weights();

    assert(a.embedding.size() == 261U * 60U);
    assert(a.layer1_input.size() == 3U * 97U * 60U);
    assert(a.layer1_recurrent.size() == 3U * 97U * 97U);
    assert(a.layer1_bias.size() == 3U * 97U);
    assert(a.layer2_input.size() == 3U * 68U * 97U);
    assert(a.layer2_recurrent.size() == 3U * 68U * 68U);
    assert(a.layer2_bias.size() == 3U * 68U);
    assert(a.rms_scale.size() == 97U);
    assert(a.projection.size() == 60U * 68U);
    assert(a.projection_bias.size() == 60U);
    assert(a.output_bias.size() == 261U);

    assert(first.initialization_seed() == 123456U);
    assert(first.initialization_method() == "xavier_glorot+orthogonal_per_gate_gram_schmidt");
    assert(a.embedding == b.embedding);
    assert(a.layer1_recurrent == b.layer1_recurrent);
    assert(a.embedding != c.embedding);
    for (float value : a.rms_scale) assert(value == 1.0F);
    for (std::size_t i = 0; i < 97U; ++i) assert(a.layer1_bias[i] == 1.0F);
    for (std::size_t i = 97U; i < 291U; ++i) assert(a.layer1_bias[i] == 0.0F);
    for (std::size_t i = 0; i < 68U; ++i) assert(a.layer2_bias[i] == 1.0F);
    for (std::size_t i = 68U; i < 204U; ++i) assert(a.layer2_bias[i] == 0.0F);
    for (float value : a.projection_bias) assert(value == 0.0F);
    for (float value : a.output_bias) assert(value == 0.0F);
    for (const auto* tensor : {&a.embedding, &a.layer1_input, &a.layer1_recurrent,
             &a.layer1_bias, &a.layer2_input, &a.layer2_recurrent,
             &a.layer2_bias, &a.rms_scale, &a.projection,
             &a.projection_bias, &a.output_bias})
        for (float value : *tensor) assert(std::isfinite(value));
}

void verify_orthogonal(const std::vector<float>& weights, std::size_t hidden) {
    for (std::size_t gate = 0; gate < 3U; ++gate) {
        const std::size_t base = gate * hidden * hidden;
        for (std::size_t left = 0; left < hidden; ++left) {
            for (std::size_t right = 0; right < hidden; ++right) {
                double dot = 0.0;
                for (std::size_t row = 0; row < hidden; ++row)
                    dot += static_cast<double>(weights[base + row * hidden + left]) *
                           weights[base + row * hidden + right];
                assert(std::abs(dot - (left == right ? 1.0 : 0.0)) < 2.0e-4);
            }
        }
    }
}

void test_recurrent_orthogonality() {
    G3Backend backend(G3Config{77U, 1.0e-5F});
    verify_orthogonal(backend.weights().layer1_recurrent, 97U);
    verify_orthogonal(backend.weights().layer2_recurrent, 68U);
}

void test_forward_probabilities_and_session_isolation() {
    G3Backend backend(G3Config{9U, 1.0e-5F});
    std::vector<std::byte> first_state(660U), second_state(660U);
    model::ModelSession first(backend, first_state), second(backend, second_state);
    assert(first.reset() && second.reset());
    assert(first.consume_byte(static_cast<std::uint8_t>('O')));
    assert(first.consume_byte(static_cast<std::uint8_t>('i')));
    assert(second.consume_byte(static_cast<std::uint8_t>('X')));

    std::array<float, 261> first_output{}, second_output{};
    assert(first.next_token_log_probabilities(first_output));
    assert(second.next_token_log_probabilities(second_output));
    double sum = 0.0;
    for (float value : first_output) {
        assert(std::isfinite(value));
        sum += std::exp(static_cast<double>(value));
    }
    assert(std::abs(sum - 1.0) < 1.0e-5);
    assert(first_output != second_output);
    assert(first_state != second_state);
}

void test_weight_tying_uses_embedding_storage() {
    G3Backend backend(G3Config{42U, 1.0e-5F});
    std::vector<std::byte> state(660U);
    assert(backend.reset_state(state));
    assert(backend.consume_byte(state, static_cast<std::uint8_t>('a')));
    std::array<float, 261> before{}, after{};
    assert(backend.next_token_log_probabilities(state, before));
    backend.mutable_weights().embedding[static_cast<std::size_t>('Z') * 60U] += 2.0F;
    assert(backend.next_token_log_probabilities(state, after));
    assert(std::abs(before[static_cast<std::size_t>('Z')] -
                    after[static_cast<std::size_t>('Z')]) > 1.0e-6F);
}

void test_invalid_shapes_and_configuration() {
    G3Backend backend;
    std::array<std::byte, 16> short_state{};
    assert(!backend.reset_state(short_state));
    std::vector<std::byte> state(660U);
    assert(backend.reset_state(state));
    std::array<float, 260> short_output{};
    assert(!backend.next_token_log_probabilities(state, short_output));
    assert(backend.consume_token(state, 260U));
    assert(!backend.consume_token(state, 261U));
    bool rejected = false;
    try { (void)G3Backend(G3Config{1U, 0.0F}); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}

} // namespace

int main() {
    test_exact_parameter_budget_and_descriptor();
    test_tensor_shapes_initialization_and_seed();
    test_recurrent_orthogonality();
    test_forward_probabilities_and_session_isolation();
    test_weight_tying_uses_embedding_storage();
    test_invalid_shapes_and_configuration();
    return 0;
}
