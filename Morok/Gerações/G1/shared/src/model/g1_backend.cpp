#include "morok/model/g1_backend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <type_traits>

namespace morok::model {
namespace {

static_assert(std::is_trivially_copyable_v<G1State>);
static_assert(kG1PredictionBits == 8);

constexpr std::uint16_t kG1ArchitectureId = 0x0101;

common::Status backend_error(common::ErrorCode code,
                             const char* stage,
                             const char* message) {
    return common::Status::failure(
        common::Error::make(code, message, "model", stage));
}

bool valid_state_size(std::size_t size) noexcept {
    return size == sizeof(G1State);
}

} // namespace

ModelDescriptor G1ModelBackend::descriptor() const noexcept {
    return ModelDescriptor{
        1,
        kG1ArchitectureId,
        256,
        static_cast<std::uint64_t>(kG1ParameterCount),
        static_cast<std::uint32_t>(sizeof(G1State)),
        static_cast<std::uint32_t>(alignof(G1State)),
        true
    };
}

common::Status G1ModelBackend::reset_state(
    std::span<std::byte> state) const {
    if (!valid_state_size(state.size())) {
        return backend_error(common::ErrorCode::invalid_argument,
                             "model.reset_state",
                             "G1 state buffer has an invalid size");
    }

    const G1State initial{};
    std::memcpy(state.data(), &initial, sizeof(initial));
    return common::Status::success();
}

common::Status G1ModelBackend::consume_byte(
    std::span<std::byte> state, std::uint8_t byte) const {
    if (!valid_state_size(state.size())) {
        return backend_error(common::ErrorCode::invalid_argument,
                             "model.consume_byte",
                             "G1 state buffer has an invalid size");
    }

    G1State candidate{};
    std::memcpy(&candidate, state.data(), sizeof(candidate));
    try {
        advance_g1_state(parameters_, candidate, byte);
    } catch (const std::exception&) {
        return backend_error(common::ErrorCode::internal_error,
                             "model.consume_byte",
                             "G1 motor rejected the state transition");
    } catch (...) {
        return backend_error(common::ErrorCode::internal_error,
                             "model.consume_byte",
                             "G1 motor failed during the state transition");
    }

    // Commit only after the complete transition succeeds.
    std::memcpy(state.data(), &candidate, sizeof(candidate));
    return common::Status::success();
}

common::Status G1ModelBackend::next_token_log_probabilities(
    std::span<const std::byte> state,
    std::span<float> log_probabilities) const {
    if (!valid_state_size(state.size())) {
        return backend_error(common::ErrorCode::invalid_argument,
                             "model.predict",
                             "G1 state buffer has an invalid size");
    }
    if (log_probabilities.size() != 256) {
        return backend_error(common::ErrorCode::invalid_argument,
                             "model.predict",
                             "G1 output buffer must contain exactly 256 entries");
    }

    G1State current{};
    std::memcpy(&current, state.data(), sizeof(current));
    std::array<float, 256> candidate{};

    try {
        for (std::size_t byte = 0; byte < candidate.size(); ++byte) {
            const auto evaluation = evaluate_g1_next_byte(
                parameters_, current, static_cast<std::uint8_t>(byte));
            const double log_probability =
                -evaluation.negative_log_likelihood_nats;
            if (!std::isfinite(log_probability)) {
                return backend_error(common::ErrorCode::internal_error,
                                     "model.predict",
                                     "G1 motor produced a non-finite log probability");
            }
            candidate[byte] = static_cast<float>(log_probability);
        }
    } catch (const std::exception&) {
        return backend_error(common::ErrorCode::internal_error,
                             "model.predict",
                             "G1 motor failed while scoring the next byte");
    } catch (...) {
        return backend_error(common::ErrorCode::internal_error,
                             "model.predict",
                             "G1 motor failed while scoring the next byte");
    }

    for (std::size_t i = 0; i < candidate.size(); ++i) {
        if (!std::isfinite(candidate[i])) {
            return backend_error(common::ErrorCode::internal_error,
                                 "model.predict",
                                 "G1 output contains a non-finite value");
        }
    }
    std::copy(candidate.begin(), candidate.end(), log_probabilities.begin());
    return common::Status::success();
}

} // namespace morok::model
