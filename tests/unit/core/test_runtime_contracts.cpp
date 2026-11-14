#include "morok/core/cancellation.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/health.hpp"
#include "morok/core/request_context.hpp"
#include "morok/core/resource_limits.hpp"
#include "morok/core/runtime.hpp"

#include <cassert>
#include <utility>
#include <vector>

int main() {
    using namespace morok;

    core::ResourceLimits limits;
    assert(limits.validate());
    assert(limits.permits_input(1024));
    assert(!limits.permits_input(limits.max_input_bytes + 1));
    assert(limits.permits_generation(256));
    assert(!limits.permits_generation(257));
    limits.target_memory_bytes = limits.hard_memory_bytes + 1;
    assert(!limits.validate());

    core::CancellationSource cancellation;
    const auto token = cancellation.token();
    assert(!token.is_cancelled());
    assert(cancellation.cancel());
    assert(token.is_cancelled());
    assert(!cancellation.cancel());

    auto session = common::SessionId::generate();
    assert(session);
    auto request = core::RequestContext::create(
        std::move(*session.value_if()), token);
    assert(request);
    assert(request.value_if()->is_cancelled());
    assert(!request.value_if()->request_id().is_nil());

    const auto healthy = core::evaluate_health(
        core::LifecycleState::ready,
        {{"IA-LANG", true, true, true, {}}});
    assert(healthy.level == core::HealthLevel::healthy);
    assert(healthy.is_ready());

    const auto degraded = core::evaluate_health(
        core::LifecycleState::ready,
        {{"IA-MEM", false, false, false, "disabled"}});
    assert(degraded.level == core::HealthLevel::degraded);
    assert(degraded.is_ready());

    const auto unhealthy = core::evaluate_health(
        core::LifecycleState::ready,
        {{"IA-LANG", true, false, false, "failed"}});
    assert(unhealthy.level == core::HealthLevel::unhealthy);
    assert(!unhealthy.is_ready());

    int initialized = 0;
    int shut_down = 0;
    core::Runtime runtime(core::RuntimeConfig{});
    const std::vector<core::Runtime::Initializer> initializers{
        [&](const core::RuntimeConfig&) {
            ++initialized;
            return common::Status::success();
        },
        [&](const core::RuntimeConfig&) {
            ++initialized;
            return common::Status::success();
        }};
    const std::vector<core::Runtime::Shutdown> shutdowns{
        [&] { ++shut_down; },
        [&] { ++shut_down; }};
    assert(runtime.initialize(initializers, shutdowns));
    assert(runtime.state() == core::LifecycleState::ready);
    assert(runtime.accepts_requests());
    assert(initialized == 2);
    assert(runtime.shutdown());
    assert(runtime.state() == core::LifecycleState::stopped);
    assert(!runtime.accepts_requests());
    assert(shut_down == 2);
    assert(runtime.shutdown());

    int rollback_count = 0;
    core::Runtime failing_runtime(core::RuntimeConfig{});
    const std::vector<core::Runtime::Initializer> failing_initializers{
        [](const core::RuntimeConfig&) { return common::Status::success(); },
        [](const core::RuntimeConfig&) {
            return common::Status::failure(common::Error::make(
                common::ErrorCode::internal_error,
                "controlled failure", "test", "initialize"));
        }};
    const std::vector<core::Runtime::Shutdown> rollback_actions{
        [&] { ++rollback_count; }};
    assert(!failing_runtime.initialize(failing_initializers, rollback_actions));
    assert(failing_runtime.state() == core::LifecycleState::failed);
    assert(!failing_runtime.accepts_requests());
    assert(rollback_count == 1);
    assert(failing_runtime.shutdown());
    assert(failing_runtime.state() == core::LifecycleState::stopped);

    return 0;
}
