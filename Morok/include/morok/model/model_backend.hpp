#pragma once

#include "morok/common/result.hpp"
#include "morok/common/error.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace morok::model {

// Stable, generation-neutral description of a neural backend.
// Architecture-specific weights and kernels remain owned by their generation.
struct ModelDescriptor final {
    std::uint16_t generation_id{0};
    std::uint16_t architecture_id{0};
    std::uint32_t vocabulary_size{0};
    std::uint64_t parameter_count{0};
    std::uint32_t state_size_bytes{0};
    std::uint32_t state_alignment_bytes{1};
    bool supports_incremental_state{false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        const bool alignment_is_power_of_two =
            state_alignment_bytes != 0 &&
            (state_alignment_bytes & (state_alignment_bytes - 1U)) == 0;
        return generation_id != 0 && architecture_id != 0 &&
               vocabulary_size > 1 && parameter_count != 0 &&
               state_size_bytes != 0 && alignment_is_power_of_two;
    }
};

// Shared inference contract. The caller owns session storage; a backend must
// not allocate, retain, or share that storage between independent sessions.
// State is opaque to Core and interpreted only by the backend that created it.
class ModelBackend {
public:
    virtual ~ModelBackend() = default;

    [[nodiscard]] virtual ModelDescriptor descriptor() const noexcept = 0;

    // state must contain exactly descriptor().state_size_bytes bytes.
    [[nodiscard]] virtual common::Status reset_state(
        std::span<std::byte> state) const = 0;

    // Consume one observed byte and update only the supplied session state.
    [[nodiscard]] virtual common::Status consume_byte(
        std::span<std::byte> state, std::uint8_t byte) const = 0;

    // Consume a vocabulary token. Byte-only backends retain compatibility;
    // generation-specific backends override this to support control tokens.
    [[nodiscard]] virtual common::Status consume_token(
        std::span<std::byte> state, std::uint16_t token_id) const {
        if (token_id <= 255U)
            return consume_byte(state, static_cast<std::uint8_t>(token_id));
        return common::Status::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "Backend does not support non-byte control tokens",
            "model", "consume_token"));
    }

    // Fill one log-probability per vocabulary entry. Unsupported entries must
    // be represented by negative infinity. The output span must match vocab size.
    [[nodiscard]] virtual common::Status next_token_log_probabilities(
        std::span<const std::byte> state,
        std::span<float> log_probabilities) const = 0;
};

} // namespace morok::model
