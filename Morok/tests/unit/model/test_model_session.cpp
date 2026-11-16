#include "morok/model/g1_backend.hpp"
#include "morok/model/model_session.hpp"

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

double probability_sum(const std::array<float, 256>& values) {
    double total = 0.0;
    for (const float value : values) {
        require(std::isfinite(value), "session output must be finite");
        total += std::exp(static_cast<double>(value));
    }
    return total;
}

void test_session_lifecycle_and_isolation() {
    using namespace morok::model;
    G1Parameters parameters{};
    initialize_g1_parameters(parameters, 2026);
    G1ModelBackend backend(parameters);

    std::vector<std::byte> storage_a(backend.descriptor().state_size_bytes);
    std::vector<std::byte> storage_b(backend.descriptor().state_size_bytes);
    ModelSession session_a(backend, storage_a);
    ModelSession session_b(backend, storage_b);

    std::array<float, 256> output_a{};
    require(!session_a.next_token_log_probabilities(output_a),
            "prediction before reset must be rejected");
    require(!session_a.consume_byte('x'),
            "input before reset must be rejected");

    require(static_cast<bool>(session_a.reset()), "first session reset must succeed");
    require(static_cast<bool>(session_b.reset()), "second session reset must succeed");
    require(session_a.initialized() && session_b.initialized(),
            "both sessions must become initialized after reset");

    require(static_cast<bool>(session_a.next_token_log_probabilities(output_a)),
            "initialized session must predict");
    require(std::abs(probability_sum(output_a) - 1.0) < 1.0e-5,
            "session output must be a normalized byte distribution");

    require(static_cast<bool>(session_a.consume_byte('a')),
            "first session must consume its byte");
    require(static_cast<bool>(session_b.consume_byte('z')),
            "second session must consume its independent byte");

    std::array<float, 256> after_a{};
    std::array<float, 256> after_b{};
    require(static_cast<bool>(session_a.next_token_log_probabilities(after_a)),
            "first session prediction must succeed");
    require(static_cast<bool>(session_b.next_token_log_probabilities(after_b)),
            "second session prediction must succeed");
    require(after_a != after_b,
            "independent session histories must produce independent states");

    require(static_cast<bool>(session_a.reset()),
            "resetting one session must succeed");
    std::array<float, 256> reset_a{};
    require(static_cast<bool>(session_a.next_token_log_probabilities(reset_a)),
            "prediction after reset must succeed");
    require(reset_a == output_a,
            "reset must restore the initial session distribution");
}

void test_session_rejects_invalid_storage_and_output() {
    using namespace morok::model;
    G1Parameters parameters{};
    initialize_g1_parameters(parameters, 7);
    G1ModelBackend backend(parameters);

    std::array<std::byte, 1> invalid_storage{};
    ModelSession invalid_session(backend, invalid_storage);
    require(!invalid_session.reset(),
            "session must reject a state buffer with the wrong size");
    require(!invalid_session.initialized(),
            "failed reset must not mark a session initialized");

    std::vector<std::byte> valid_storage(backend.descriptor().state_size_bytes);
    ModelSession session(backend, valid_storage);
    require(static_cast<bool>(session.reset()), "valid session reset must succeed");
    std::array<float, 255> invalid_output{};
    require(!session.next_token_log_probabilities(invalid_output),
            "session must reject an output buffer with the wrong vocabulary size");
}

} // namespace

int main() {
    try {
        test_session_lifecycle_and_isolation();
        test_session_rejects_invalid_storage_and_output();
    } catch (const std::exception& error) {
        std::cerr << "Model session test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
