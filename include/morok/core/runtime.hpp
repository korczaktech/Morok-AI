#pragma once

#include "morok/common/error.hpp"
#include "morok/common/result.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/health.hpp"
#include "morok/core/lifecycle.hpp"

#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace morok::core {

class Runtime final {
public:
    using Initializer = std::function<common::Status(const RuntimeConfig&)>;
    using Shutdown = std::function<void()>;

    explicit Runtime(RuntimeConfig config)
        : config_(std::move(config)) {}

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    [[nodiscard]] common::Status initialize(
        const std::vector<Initializer>& initializers,
        const std::vector<Shutdown>& shutdowns = {}) {
        std::lock_guard lock(mutex_);
        if (state_ != LifecycleState::created) {
            return failure(common::ErrorCode::invalid_argument,
                           "runtime can only be initialized once");
        }
        state_ = LifecycleState::initializing;

        const auto valid = config_.validate();
        if (!valid) {
            state_ = LifecycleState::failed;
            return common::Status::failure(*valid.error_if());
        }

        shutdowns_ = shutdowns;
        for (const auto& initializer : initializers) {
            if (!initializer) {
                rollback_locked();
                state_ = LifecycleState::failed;
                return failure(common::ErrorCode::invalid_argument,
                               "empty subsystem initializer");
            }
            const auto result = initializer(config_);
            if (!result) {
                rollback_locked();
                state_ = LifecycleState::failed;
                return common::Status::failure(*result.error_if());
            }
            ++initialized_count_;
        }
        state_ = LifecycleState::ready;
        return common::Status::success();
    }

    [[nodiscard]] common::Status shutdown() {
        std::lock_guard lock(mutex_);
        if (state_ == LifecycleState::stopped) {
            return common::Status::success();
        }
        if (state_ != LifecycleState::ready && state_ != LifecycleState::failed &&
            state_ != LifecycleState::created) {
            return failure(common::ErrorCode::invalid_argument,
                           "runtime cannot shut down from current state");
        }
        state_ = LifecycleState::stopping;
        rollback_locked();
        state_ = LifecycleState::stopped;
        return common::Status::success();
    }

    [[nodiscard]] LifecycleState state() const noexcept {
        std::lock_guard lock(mutex_);
        return state_;
    }

    [[nodiscard]] bool accepts_requests() const noexcept {
        return core::accepts_requests(state());
    }

    [[nodiscard]] const RuntimeConfig& config() const noexcept {
        return config_;
    }

private:
    [[nodiscard]] common::Status failure(
        common::ErrorCode code, std::string message) const {
        return common::Status::failure(common::Error::make(
            code, std::move(message), "core", "runtime"));
    }

    void rollback_locked() noexcept {
        while (initialized_count_ > 0) {
            --initialized_count_;
            const auto index = initialized_count_;
            if (index < shutdowns_.size() && shutdowns_[index]) {
                try { shutdowns_[index](); } catch (...) {}
            }
        }
    }

    RuntimeConfig config_;
    mutable std::mutex mutex_;
    LifecycleState state_{LifecycleState::created};
    std::vector<Shutdown> shutdowns_;
    std::size_t initialized_count_{0};
};

} // namespace morok::core
