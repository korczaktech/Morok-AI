#include "morok/security/security_subsystem.hpp"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

int main() {
    using namespace morok;

    std::vector<security::SecurityAuditRecord> events;
    security::SecuritySubsystem security_system({}, [&events](
        const security::SecurityAuditRecord& record) {
        events.push_back(record);
        return common::Status::success();
    });
    core::Runtime runtime(core::RuntimeConfig{});
    assert(runtime.initialize(
        {security_system.initializer()},
        {security_system.shutdown_callback()}));
    assert(security_system.initialized());

    auto session = common::SessionId::generate();
    auto other_session = common::SessionId::generate();
    assert(session && other_session);
    security::SecurityPrincipal principal{"local-user-1", *session.value_if()};
    security::SecurityPrincipal other_principal{"local-user-1", *other_session.value_if()};

    const auto user_content = security::classify_content(
        security::ContentOrigin::user_input, "request-body");
    const auto memory_content = security::classify_content(
        security::ContentOrigin::retrieved_memory, "memory-record");
    const auto tool_content = security::classify_content(
        security::ContentOrigin::tool_output, "tool-response");
    assert(user_content.is_untrusted());
    assert(memory_content.is_untrusted());
    assert(tool_content.is_untrusted());
    assert(user_content.origin() == security::ContentOrigin::user_input);
    assert(memory_content.source_id() == "memory-record");

    auto denied = security_system.authorize_tool(
        principal, "read_file", tools::ToolCapability::filesystem_read);
    assert(!denied && denied.error_if()->code == common::ErrorCode::permission_denied);

    assert(security_system.grant_tool(
        principal, "read_file", tools::ToolCapability::filesystem_read));
    assert(security_system.grant_tool(
        principal, "send_notice", tools::ToolCapability::network));
    assert(security_system.grant_count() == 2);
    assert(security_system.authorize_tool(
        principal, "read_file", tools::ToolCapability::filesystem_read));
    auto isolated = security_system.authorize_tool(
        other_principal, "read_file", tools::ToolCapability::filesystem_read);
    assert(!isolated && isolated.error_if()->code == common::ErrorCode::permission_denied);

    auto policy = security_system.tool_policy(principal);
    assert(policy);
    assert(policy.value_if()->allowed_tools.size() == 2);
    assert(policy.value_if()->granted_capabilities.size() == 2);
    assert(policy.value_if()->exact_grants.size() == 2);

    // A pair-based policy must not accidentally grant the Cartesian product
    // of tool names and capabilities.
    tools::ToolSubsystem tool_subsystem({}, [](const tools::ToolAuditRecord&) {
        return common::Status::success();
    });
    assert(tool_subsystem.initialize(core::RuntimeConfig{}));
    tools::ToolDefinition mismatched;
    mismatched.name = "read_file";
    mismatched.description = "Capability mismatch regression test.";
    mismatched.capability = tools::ToolCapability::network;
    mismatched.handler = [](const tools::ToolExecutionContext&, const tools::ToolArguments&) {
        return common::Result<std::string>::success("should not run");
    };
    assert(tool_subsystem.register_tool(std::move(mismatched)));
    auto mismatch_request = core::RequestContext::create(*session.value_if());
    assert(mismatch_request);
    auto cross_grant = tool_subsystem.invoke(
        *mismatch_request.value_if(), "read_file", {}, *policy.value_if());
    assert(!cross_grant &&
           cross_grant.error_if()->code == common::ErrorCode::permission_denied);
    assert(security_system.grant_tool(
        principal, "publish", tools::ToolCapability::external_write));
    tools::ToolDefinition publish;
    publish.name = "publish";
    publish.description = "Publish content to an external destination.";
    publish.capability = tools::ToolCapability::external_write;
    publish.requires_confirmation = true;
    publish.handler = [](const tools::ToolExecutionContext&, const tools::ToolArguments&) {
        return common::Result<std::string>::success("published");
    };
    assert(tool_subsystem.register_tool(std::move(publish)));
    security::SecurityAction publish_action{
        "publish", "publish", "public-feed", "Publish the approved message"};
    auto publish_ticket = security_system.request_confirmation(principal, publish_action);
    assert(publish_ticket);
    auto publish_approval = security_system.approve_confirmation(
        principal, *publish_ticket.value_if(), publish_action, true);
    assert(publish_approval);
    auto publish_result = security_system.invoke_tool(
        tool_subsystem, *mismatch_request.value_if(), principal, "publish", {},
        &publish_action, &*publish_approval.value_if());
    assert(publish_result && *publish_result.value_if() == "published");
    auto publish_replay = security_system.invoke_tool(
        tool_subsystem, *mismatch_request.value_if(), principal, "publish", {},
        &publish_action, &*publish_approval.value_if());
    assert(!publish_replay &&
           publish_replay.error_if()->code == common::ErrorCode::confirmation_required);
    assert(tool_subsystem.shutdown());

    security::SecurityAction action{
        "read_file", "read", "/workspace/report.txt", "Read one report file"};
    auto ticket = security_system.request_confirmation(principal, action);
    assert(ticket && security_system.pending_confirmations() == 1);

    auto altered_action = action;
    altered_action.target = "/workspace/secrets.txt";
    auto mismatch = security_system.approve_confirmation(
        principal, *ticket.value_if(), altered_action, true);
    assert(!mismatch && mismatch.error_if()->code == common::ErrorCode::permission_denied);

    auto wrong_session = security_system.approve_confirmation(
        other_principal, *ticket.value_if(), action, true);
    assert(!wrong_session && wrong_session.error_if()->code == common::ErrorCode::permission_denied);

    auto approved = security_system.approve_confirmation(
        principal, *ticket.value_if(), action, true);
    assert(approved);
    auto wrong_consume = security_system.consume_confirmation(
        principal, *approved.value_if(), altered_action);
    assert(!wrong_consume && wrong_consume.error_if()->code == common::ErrorCode::permission_denied);

    assert(security_system.consume_confirmation(principal, *approved.value_if(), action));
    assert(security_system.pending_confirmations() == 0);
    auto replay = security_system.consume_confirmation(principal, *approved.value_if(), action);
    assert(!replay && replay.error_if()->code == common::ErrorCode::confirmation_required);

    auto rejected_ticket = security_system.request_confirmation(principal, action);
    assert(rejected_ticket);
    auto rejected = security_system.approve_confirmation(
        principal, *rejected_ticket.value_if(), action, false);
    assert(!rejected && rejected.error_if()->code == common::ErrorCode::permission_denied);

    assert(security_system.revoke_tool(
        principal, "read_file", tools::ToolCapability::filesystem_read));
    assert(security_system.revoke_tool(
        principal, "send_notice", tools::ToolCapability::network));
    assert(security_system.revoke_tool(
        principal, "publish", tools::ToolCapability::external_write));
    assert(security_system.grant_count() == 0);
    auto revoked = security_system.authorize_tool(
        principal, "read_file", tools::ToolCapability::filesystem_read);
    assert(!revoked && revoked.error_if()->code == common::ErrorCode::permission_denied);

    assert(!events.empty());
    bool saw_denial = false;
    bool saw_grant = false;
    bool saw_confirmation = false;
    bool saw_consumed = false;
    for (const auto& event : events) {
        saw_denial = saw_denial ||
            event.type == security::SecurityEventType::authorization_denied;
        saw_grant = saw_grant ||
            event.type == security::SecurityEventType::grant_added;
        saw_confirmation = saw_confirmation ||
            event.type == security::SecurityEventType::confirmation_approved;
        saw_consumed = saw_consumed ||
            event.type == security::SecurityEventType::confirmation_consumed;
    }
    assert(saw_denial && saw_grant && saw_confirmation && saw_consumed);

    security::SecuritySubsystem missing_audit;
    assert(!missing_audit.initialize(core::RuntimeConfig{}));

    security::SecuritySubsystem failing_audit({}, [](const security::SecurityAuditRecord&) {
        return common::Status::failure(common::Error::make(
            common::ErrorCode::tool_failure, "audit unavailable", "test", "audit"));
    });
    assert(failing_audit.initialize(core::RuntimeConfig{}));
    auto audit_failure = failing_audit.grant_tool(
        principal, "sensitive_action", tools::ToolCapability::external_write);
    assert(!audit_failure);
    assert(failing_audit.grant_count() == 0);
    assert(failing_audit.shutdown());

    assert(runtime.shutdown());
    assert(!security_system.initialized());
    return 0;
}
