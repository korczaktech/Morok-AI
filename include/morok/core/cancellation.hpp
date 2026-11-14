#pragma once

#include <atomic>
#include <memory>

namespace morok::core {

class CancellationToken final {
public:
    CancellationToken() = default;

    [[nodiscard]] bool is_cancelled() const noexcept {
        return state_ && state_->load(std::memory_order_acquire);
    }

private:
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> state) noexcept
        : state_(std::move(state)) {}

    std::shared_ptr<std::atomic_bool> state_{};

    friend class CancellationSource;
};

class CancellationSource final {
public:
    CancellationSource()
        : state_(std::make_shared<std::atomic_bool>(false)) {}

    [[nodiscard]] CancellationToken token() const noexcept {
        return CancellationToken(state_);
    }

    [[nodiscard]] bool cancel() noexcept {
        bool expected = false;
        return state_->compare_exchange_strong(
            expected, true, std::memory_order_acq_rel);
    }

private:
    std::shared_ptr<std::atomic_bool> state_;
};

} // namespace morok::core
