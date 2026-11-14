#include "morok/tools/tool_subsystem.hpp"

#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
morok::core::RequestContext make_request(const morok::common::SessionId& session,
                                         morok::core::CancellationToken token = {}) {
    auto result = morok::core::RequestContext::create(session, std::move(token));
    assert(result);
    return std::move(*result.value_if());
}

morok::tools::ToolDefinition echo_tool() {
    using namespace morok::tools;
    ToolDefinition tool;
    tool.name = "echo";
    tool.description = "Returns the supplied text.";
    tool.schema.fields.push_back(ToolField{"text", ToolValueType::string, true, 32});
    tool.capability = ToolCapability::read_only;
    tool.timeout_ms = 100;
    tool.handler = [](const ToolExecutionContext& context, const ToolArguments& args) {
        if (context.is_cancelled())
            return morok::common::Result<std::string>::failure(
                morok::common::Error::make(morok::common::ErrorCode::cancelled,
                    "cancelled", "test", "handler"));
        return morok::common::Result<std::string>::success(
            std::get<std::string>(args.at("text")));
    };
    return tool;
}
}

int main() {
    using namespace morok;
    std::vector<tools::ToolAuditRecord> audit;
    tools::ToolSubsystem subsystem({}, [&audit](const tools::ToolAuditRecord& record) {
        audit.push_back(record);
        return common::Status::success();
    });
    core::Runtime runtime(core::RuntimeConfig{});
    assert(runtime.initialize({subsystem.initializer()}, {subsystem.shutdown_callback()}));
    assert(subsystem.initialized());

    auto session = common::SessionId::generate();
    assert(session);
    auto request = make_request(*session.value_if());
    tools::ToolAccessPolicy policy;
    policy.allowed_tools = {"echo"};
    policy.granted_capabilities = {tools::ToolCapability::read_only};

    assert(subsystem.register_tool(echo_tool()));
    assert(subsystem.registered_tools() == 1);
    auto duplicate = subsystem.register_tool(echo_tool());
    assert(!duplicate);

    auto visible = subsystem.discover(request, policy);
    assert(visible && visible.value_if()->size() == 1);
    auto hidden = subsystem.discover(request, {});
    assert(hidden && hidden.value_if()->empty());

    auto echoed = subsystem.invoke(request, "echo", {{"text", std::string("Morok")}}, policy);
    assert(echoed && *echoed.value_if() == "Morok");

    auto missing = subsystem.invoke(request, "echo", {}, policy);
    assert(!missing && missing.error_if()->code == common::ErrorCode::invalid_argument);
    auto wrong_type = subsystem.invoke(request, "echo", {{"text", 42.0}}, policy);
    assert(!wrong_type && wrong_type.error_if()->code == common::ErrorCode::invalid_argument);
    auto unknown = subsystem.invoke(request, "echo",
        {{"text", std::string("ok")}, {"extra", true}}, policy);
    assert(!unknown && unknown.error_if()->code == common::ErrorCode::invalid_argument);
    auto too_large = subsystem.invoke(request, "echo",
        {{"text", std::string(33, 'x')}}, policy);
    assert(!too_large && too_large.error_if()->code == common::ErrorCode::invalid_argument);

    auto denied = subsystem.invoke(request, "echo", {{"text", std::string("x")}}, {});
    assert(!denied && denied.error_if()->code == common::ErrorCode::permission_denied);

    auto confirmed_tool = echo_tool();
    confirmed_tool.name = "write";
    confirmed_tool.capability = tools::ToolCapability::external_write;
    confirmed_tool.requires_confirmation = true;
    assert(subsystem.register_tool(std::move(confirmed_tool)));
    tools::ToolAccessPolicy write_policy{{"write"}, {tools::ToolCapability::external_write}};
    auto needs_confirmation = subsystem.invoke(request, "write",
        {{"text", std::string("x")}}, write_policy);
    assert(!needs_confirmation &&
           needs_confirmation.error_if()->code == common::ErrorCode::confirmation_required);
    auto confirmed = subsystem.invoke(request, "write",
        {{"text", std::string("x")}}, write_policy, true);
    assert(confirmed && *confirmed.value_if() == "x");

    auto slow = echo_tool();
    slow.name = "slow";
    slow.timeout_ms = 5;
    slow.handler = [](const tools::ToolExecutionContext&, const tools::ToolArguments&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return common::Result<std::string>::success("late");
    };
    assert(subsystem.register_tool(std::move(slow)));
    tools::ToolAccessPolicy slow_policy{{"slow"}, {tools::ToolCapability::read_only}};
    auto timed_out = subsystem.invoke(request, "slow", {{"text", std::string("x")}}, slow_policy);
    assert(!timed_out && timed_out.error_if()->code == common::ErrorCode::tool_timeout);

    auto broken = echo_tool();
    broken.name = "broken";
    broken.handler = [](const tools::ToolExecutionContext&, const tools::ToolArguments&)
        -> common::Result<std::string> { throw 1; };
    assert(subsystem.register_tool(std::move(broken)));
    tools::ToolAccessPolicy broken_policy{{"broken"}, {tools::ToolCapability::read_only}};
    auto failed = subsystem.invoke(request, "broken", {{"text", std::string("x")}}, broken_policy);
    assert(!failed && failed.error_if()->code == common::ErrorCode::tool_failure);

    auto oversized = echo_tool();
    oversized.name = "oversized";
    oversized.max_output_bytes = 2;
    oversized.handler = [](const tools::ToolExecutionContext&, const tools::ToolArguments&) {
        return common::Result<std::string>::success("long");
    };
    assert(subsystem.register_tool(std::move(oversized)));
    tools::ToolAccessPolicy oversized_policy{{"oversized"}, {tools::ToolCapability::read_only}};
    auto output_limit = subsystem.invoke(request, "oversized",
        {{"text", std::string("x")}}, oversized_policy);
    assert(!output_limit && output_limit.error_if()->code == common::ErrorCode::resource_limit);

    core::CancellationSource cancellation;
    assert(cancellation.cancel());
    auto cancelled_request = make_request(*session.value_if(), cancellation.token());
    auto cancelled = subsystem.invoke(cancelled_request, "echo",
        {{"text", std::string("x")}}, policy);
    assert(!cancelled && cancelled.error_if()->code == common::ErrorCode::cancelled);

    assert(!audit.empty());
    bool saw_started = false;
    bool saw_success = false;
    bool saw_denied = false;
    bool saw_timeout = false;
    for (const auto& event : audit) {
        saw_started = saw_started || event.type == tools::ToolAuditEventType::started;
        saw_success = saw_success || event.type == tools::ToolAuditEventType::succeeded;
        saw_denied = saw_denied || event.type == tools::ToolAuditEventType::denied;
        saw_timeout = saw_timeout || event.type == tools::ToolAuditEventType::timed_out;
    }
    assert(saw_started && saw_success && saw_denied && saw_timeout);
    assert(subsystem.unregister_tool("echo"));
    assert(subsystem.shutdown());
    assert(!subsystem.initialized());
    assert(runtime.shutdown());
    return 0;
}
