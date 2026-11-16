#include "morok/model/g1_backend.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

double probability_sum(const std::array<float, 256>& log_probabilities) {
    double total = 0.0;
    for (const float value : log_probabilities) {
        require(std::isfinite(value), "G1 log probabilities must be finite");
        require(value <= 1.0e-6F, "log probabilities cannot be positive");
        total += std::exp(static_cast<double>(value));
    }
    return total;
}

void test_descriptor_and_reset() {
    using namespace morok::model;
    G1Parameters parameters{};
    initialize_g1_parameters(parameters, 17);
    G1ModelBackend backend(parameters);
    const ModelDescriptor descriptor = backend.descriptor();

    require(descriptor.valid(), "G1 descriptor must be valid");
    require(descriptor.generation_id == 1, "G1 descriptor must identify generation 1");
    require(descriptor.architecture_id == 0x0101, "G1 architecture ID must be stable");
    require(descriptor.vocabulary_size == 256, "G1 vocabulary must remain byte-level");
    require(descriptor.parameter_count == kG1ParameterCount,
            "G1 descriptor must report the existing parameter count");
    require(descriptor.state_size_bytes == sizeof(G1State),
            "G1 descriptor must expose its actual state size");
    require(descriptor.supports_incremental_state,
            "G1 backend must support incremental state");

    std::vector<std::byte> state(descriptor.state_size_bytes);
    require(static_cast<bool>(backend.reset_state(state)),
            "G1 state reset must succeed");
    G1State reset{};
    std::memcpy(&reset, state.data(), sizeof(reset));
    require(reset.fast == G1State{}.fast && reset.slow == G1State{}.slow,
            "reset must clear both recurrent states");
}

void test_incremental_prediction_distribution() {
    using namespace morok::model;
    G1Parameters parameters{};
    initialize_g1_parameters(parameters, 42);
    G1ModelBackend backend(parameters);
    const auto descriptor = backend.descriptor();
    std::vector<std::byte> state(descriptor.state_size_bytes);
    require(static_cast<bool>(backend.reset_state(state)),
            "initial state reset must succeed");

    std::array<float, 256> before{};
    require(static_cast<bool>(backend.next_token_log_probabilities(state, before)),
            "prediction from reset state must succeed");
    require(std::abs(probability_sum(before) - 1.0) < 1.0e-5,
            "G1 byte log probabilities must normalize to one");

    for (const std::uint8_t byte : std::array<std::uint8_t, 5>{'M', 'o', 'r', 'o', 'k'}) {
        require(static_cast<bool>(backend.consume_byte(state, byte)),
                "incremental byte consumption must succeed");
    }

    std::array<float, 256> after{};
    require(static_cast<bool>(backend.next_token_log_probabilities(state, after)),
            "prediction after consuming bytes must succeed");
    require(std::abs(probability_sum(after) - 1.0) < 1.0e-5,
            "updated G1 byte log probabilities must normalize to one");
    require(before != after,
            "consuming a sequence must update the next-byte distribution");
}

void test_invalid_buffers_are_rejected_without_partial_output() {
    using namespace morok::model;
    G1Parameters parameters{};
    initialize_g1_parameters(parameters, 99);
    G1ModelBackend backend(parameters);

    std::array<std::byte, 1> invalid_state{};
    require(!backend.reset_state(invalid_state),
            "wrong-sized state buffer must be rejected");
    require(!backend.consume_byte(invalid_state, 'x'),
            "wrong-sized state buffer must not be consumed");

    std::vector<std::byte> state(backend.descriptor().state_size_bytes);
    require(static_cast<bool>(backend.reset_state(state)),
            "valid state reset must succeed");
    std::array<float, 255> invalid_output{};
    require(!backend.next_token_log_probabilities(state, invalid_output),
            "wrong-sized output buffer must be rejected");
}

} // namespace

int main() {
    try {
        test_descriptor_and_reset();
        test_incremental_prediction_distribution();
        test_invalid_buffers_are_rejected_without_partial_output();
    } catch (const std::exception& error) {
        std::cerr << "G1 backend contract test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
