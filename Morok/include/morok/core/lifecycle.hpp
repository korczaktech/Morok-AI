#pragma once

#include <cstdint>
#include <string_view>

namespace morok::core {

enum class LifecycleState : std::uint8_t {
    created,
    initializing,
    ready,
    stopping,
    stopped,
    failed
};

[[nodiscard]] constexpr std::string_view to_string(LifecycleState state) noexcept {
    switch (state) {
    case LifecycleState::created: return "created";
    case LifecycleState::initializing: return "initializing";
    case LifecycleState::ready: return "ready";
    case LifecycleState::stopping: return "stopping";
    case LifecycleState::stopped: return "stopped";
    case LifecycleState::failed: return "failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr bool accepts_requests(LifecycleState state) noexcept {
    return state == LifecycleState::ready;
}

} // namespace morok::core
