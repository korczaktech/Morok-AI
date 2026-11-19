#pragma once

#include "morok/model/model_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace morok::model {

// Non-owning session facade over an existing ModelBackend.
// The caller owns the state buffer and must keep both backend and storage alive.
// Separate ModelSession instances must use separate state buffers.
class ModelSession final {
public:
    ModelSession(const ModelBackend& backend,
                 std::span<std::byte> state_storage) noexcept
        : backend_(backend),
          state_storage_(state_storage),
          descriptor_(backend.descriptor()) {}

    ModelSession(const ModelSession&) = delete;
    ModelSession& operator=(const ModelSession&) = delete;
    ModelSession(ModelSession&&) = delete;
    ModelSession& operator=(ModelSession&&) = delete;

    [[nodiscard]] common::Status reset() {
        initialized_ = false;
        if (!descriptor_.valid()) {
            return failure(common::ErrorCode::invalid_argument,
                           "model.session.reset",
                           "backend descriptor is invalid");
        }
        if (state_storage_.size() != descriptor_.state_size_bytes) {
            return failure(common::ErrorCode::invalid_argument,
                           "model.session.reset",
                           "session state buffer has an invalid size");
        }
        const auto address =
            reinterpret_cast<std::uintptr_t>(state_storage_.data());
        if (address % descriptor_.state_alignment_bytes != 0) {
            return failure(common::ErrorCode::invalid_argument,
                           "model.session.reset",
                           "session state buffer does not satisfy backend alignment");
        }

        const auto result = backend_.reset_state(state_storage_);
        initialized_ = static_cast<bool>(result);
        return result;
    }

    // Marks caller-owned state as restored from a prior turn without resetting it.
    // The caller is responsible for supplying a previously initialized state buffer.
    [[nodiscard]] common::Status resume_existing_state() {
        initialized_ = false;
        if (!descriptor_.valid() ||
            state_storage_.size() != descriptor_.state_size_bytes) {
            return failure(common::ErrorCode::invalid_argument,
                           "model.session.resume",
                           "backend descriptor or restored state size is invalid");
        }
        const auto address =
            reinterpret_cast<std::uintptr_t>(state_storage_.data());
        if (address % descriptor_.state_alignment_bytes != 0) {
            return failure(common::ErrorCode::invalid_argument,
                           "model.session.resume",
                           "restored state buffer does not satisfy backend alignment");
        }
        initialized_ = true;
        return common::Status::success();
    }

    [[nodiscard]] common::Status consume_byte(std::uint8_t byte) {
        if (!initialized_) {
            return failure(common::ErrorCode::model_not_loaded,
                           "model.session.consume",
                           "session must be reset before consuming input");
        }
        return backend_.consume_byte(state_storage_, byte);
    }

    [[nodiscard]] common::Status consume_token(std::uint16_t token_id) {
        if (!initialized_) {
            return failure(common::ErrorCode::model_not_loaded,
                           "model.session.consume_token",
                           "session must be reset before consuming input");
        }
        return backend_.consume_token(state_storage_, token_id);
    }

    [[nodiscard]] common::Status next_token_log_probabilities(
        std::span<float> output) const {
        if (!initialized_) {
            return failure(common::ErrorCode::model_not_loaded,
                           "model.session.predict",
                           "session must be reset before prediction");
        }
        if (output.size() != descriptor_.vocabulary_size) {
            return failure(common::ErrorCode::invalid_argument,
                           "model.session.predict",
                           "output buffer size does not match backend vocabulary");
        }
        return backend_.next_token_log_probabilities(state_storage_, output);
    }

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }

    [[nodiscard]] const ModelDescriptor& descriptor() const noexcept {
        return descriptor_;
    }

private:
    [[nodiscard]] static common::Status failure(
        common::ErrorCode code, const char* stage, const char* message) {
        return common::Status::failure(
            common::Error::make(code, message, "model", stage));
    }

    const ModelBackend& backend_;
    std::span<std::byte> state_storage_;
    ModelDescriptor descriptor_{};
    bool initialized_{false};
};

} // namespace morok::model
