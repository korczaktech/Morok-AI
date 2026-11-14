#pragma once

#include "morok/common/error.hpp"
#include "morok/common/ids.hpp"
#include "morok/common/result.hpp"
#include "morok/common/timestamp.hpp"

#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>

namespace morok::core {

struct SessionInfo final {
    common::SessionId id;
    common::Timestamp created_at;
};

class SessionRegistry final {
public:
    explicit SessionRegistry(std::size_t maximum_sessions)
        : maximum_sessions_(maximum_sessions) {}

    [[nodiscard]] common::Result<common::SessionId> create() {
        if (maximum_sessions_ == 0) {
            return common::Result<common::SessionId>::failure(common::Error::make(
                common::ErrorCode::resource_limit,
                "session capacity is zero", "core", "session.create"));
        }

        auto generated = common::SessionId::generate();
        if (!generated) {
            return common::Result<common::SessionId>::failure(
                *generated.error_if());
        }
        auto id = *generated.value_if();

        std::lock_guard lock(mutex_);
        if (sessions_.size() >= maximum_sessions_) {
            return common::Result<common::SessionId>::failure(common::Error::make(
                common::ErrorCode::resource_limit,
                "maximum active sessions reached", "core", "session.create"));
        }
        sessions_.emplace(id.to_string(), SessionInfo{id, common::Timestamp::now()});
        return common::Result<common::SessionId>::success(std::move(id));
    }

    [[nodiscard]] common::Status close(const common::SessionId& id) {
        std::lock_guard lock(mutex_);
        const auto erased = sessions_.erase(id.to_string());
        if (erased == 0) {
            return common::Status::failure(common::Error::make(
                common::ErrorCode::invalid_argument,
                "session does not exist", "core", "session.close"));
        }
        return common::Status::success();
    }

    [[nodiscard]] bool contains(const common::SessionId& id) const {
        std::lock_guard lock(mutex_);
        return sessions_.contains(id.to_string());
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(mutex_);
        return sessions_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return maximum_sessions_;
    }

private:
    const std::size_t maximum_sessions_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionInfo> sessions_;
};

} // namespace morok::core
