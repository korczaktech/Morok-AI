#pragma once

#include "morok/core/lifecycle.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace morok::core {

enum class HealthLevel : std::uint8_t { healthy, degraded, unhealthy };

struct SubsystemHealth final {
    std::string name;
    bool required{true};
    bool initialized{false};
    bool healthy{false};
    std::string detail;
};

struct HealthReport final {
    LifecycleState lifecycle{LifecycleState::created};
    HealthLevel level{HealthLevel::unhealthy};
    std::vector<SubsystemHealth> subsystems;

    [[nodiscard]] bool is_ready() const noexcept {
        return lifecycle == LifecycleState::ready &&
            level != HealthLevel::unhealthy;
    }
};

[[nodiscard]] inline HealthReport evaluate_health(
    LifecycleState lifecycle,
    std::vector<SubsystemHealth> subsystems) {
    HealthReport report;
    report.lifecycle = lifecycle;
    report.subsystems = std::move(subsystems);

    bool required_failure = false;
    bool optional_failure = false;
    for (const auto& subsystem : report.subsystems) {
        if (!subsystem.initialized || !subsystem.healthy) {
            if (subsystem.required) required_failure = true;
            else optional_failure = true;
        }
    }

    if (lifecycle == LifecycleState::failed || required_failure ||
        lifecycle != LifecycleState::ready) {
        report.level = HealthLevel::unhealthy;
    } else if (optional_failure) {
        report.level = HealthLevel::degraded;
    } else {
        report.level = HealthLevel::healthy;
    }
    return report;
}

} // namespace morok::core
