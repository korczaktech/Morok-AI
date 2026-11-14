#include "morok/core/runtime.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
    using namespace morok;
    core::Runtime runtime(core::RuntimeConfig{});
    std::vector<std::string> events;

    const auto result = runtime.initialize(
        {
            [&](const core::RuntimeConfig&) {
                events.push_back("init-1");
                return common::Status::success();
            },
            [&](const core::RuntimeConfig&) {
                events.push_back("init-2");
                return common::Status::success();
            },
            [&](const core::RuntimeConfig&) {
                events.push_back("init-3-fail");
                return common::Status::failure(common::Error::make(
                    common::ErrorCode::internal_error, "injected failure"));
            }
        },
        {
            [&] { events.push_back("stop-1"); },
            [&] { events.push_back("stop-2"); },
            [&] { events.push_back("stop-3"); }
        });

    assert(!result);
    assert(runtime.state() == core::LifecycleState::failed);
    assert((events == std::vector<std::string>{
        "init-1", "init-2", "init-3-fail", "stop-2", "stop-1"}));
    assert(runtime.shutdown());
    assert(runtime.state() == core::LifecycleState::stopped);
    return 0;
}
