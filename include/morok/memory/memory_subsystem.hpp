#pragma once

#include "morok/core/configuration.hpp"
#include "morok/core/session_registry.hpp"
#include "morok/core/runtime.hpp"
#include "morok/memory/memory_store.hpp"

#include <string>
#include <utility>

namespace morok::memory {

// IA-CORE lifecycle adapter for IA-MEM and per-session working-memory cleanup.
class MemorySubsystem final {
public:
    MemorySubsystem(std::string database_path, MemoryLimits limits = {})
        : database_path_(std::move(database_path)), limits_(limits) {}

    [[nodiscard]] common::Status initialize(const core::RuntimeConfig& config) {
        if (initialized_) return common::Status::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "IA-MEM is already initialized", "memory", "initialize"));
        const auto valid = config.validate();
        if (!valid) return common::Status::failure(*valid.error_if());
        if (limits_.max_database_bytes > config.resources.hard_memory_bytes ||
            limits_.max_record_bytes > config.resources.max_input_bytes) {
            return common::Status::failure(common::Error::make(
                common::ErrorCode::resource_limit,
                "IA-MEM limits exceed IA-CORE resource policy",
                "memory", "initialize"));
        }
        const auto result = store_.open(database_path_, limits_);
        if (result) initialized_ = true;
        return result;
    }

    [[nodiscard]] common::Status shutdown() noexcept {
        if (!initialized_) return common::Status::success();
        const auto result = store_.close();
        if (result) initialized_ = false;
        return result;
    }

    [[nodiscard]] common::Status close_session(
        core::SessionRegistry& sessions,
        const common::SessionId& session_id) {
        if (!initialized_ || !sessions.contains(session_id))
            return common::Status::failure(common::Error::make(
                common::ErrorCode::invalid_argument,
                "IA-MEM or session is not active", "memory", "close_session"));
        const auto cleared = store_.clear_working(session_id);
        if (!cleared) return common::Status::failure(*cleared.error_if());
        return sessions.close(session_id);
    }

    [[nodiscard]] core::Runtime::Initializer initializer() {
        return [this](const core::RuntimeConfig& config) { return initialize(config); };
    }

    [[nodiscard]] core::Runtime::Shutdown shutdown_callback() {
        return [this] { (void)shutdown(); };
    }

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] MemoryStore& store() noexcept { return store_; }
    [[nodiscard]] const MemoryStore& store() const noexcept { return store_; }

private:
    std::string database_path_;
    MemoryLimits limits_;
    MemoryStore store_;
    bool initialized_{false};
};

} // namespace morok::memory
