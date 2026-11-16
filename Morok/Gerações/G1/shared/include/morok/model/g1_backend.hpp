#pragma once

#include "morok/model/g1_forward.hpp"
#include "morok/model/model_backend.hpp"

namespace morok::model {

// Adapter for the existing G1 motor. It does not own or copy model weights.
// The referenced G1Parameters object must outlive this adapter and remain
// immutable while the adapter is being used.
class G1ModelBackend final : public ModelBackend {
public:
    explicit G1ModelBackend(const G1Parameters& parameters) noexcept
        : parameters_(parameters) {}

    [[nodiscard]] ModelDescriptor descriptor() const noexcept override;
    [[nodiscard]] common::Status reset_state(
        std::span<std::byte> state) const override;
    [[nodiscard]] common::Status consume_byte(
        std::span<std::byte> state, std::uint8_t byte) const override;
    [[nodiscard]] common::Status next_token_log_probabilities(
        std::span<const std::byte> state,
        std::span<float> log_probabilities) const override;

private:
    const G1Parameters& parameters_;
};

} // namespace morok::model
