#include "morok/security/security_subsystem.hpp"

#include <cassert>
#include <string>
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
    const auto trusted_config = security::classify_content(
        security::ContentOrigin::trusted_configuration, "host-policy");
    assert(user_content.untrusted && !security::may_supply_security_policy(user_content));
    assert(memory_content.untrusted && !security::may_supply_security_policy(memory_content));
    assert(!trusted_config.untrusted && security::may_supply_security_policy(trusted_config));

    auto denied = security_system.authorize_tool(
        principal, "read_file", tools::ToolCapability::filesystem_read);
    assert(!denied && denied.error_if()->code == common::ErrorCode::permission_denied);

    assert(security_system.grant_tool(
        principal, "read_file", tools::ToolCapability::filesystem_read));
    assert(security_system.grant_count() == 1);
    assert(security_system.authorize_tool(
        principal, "read_file", tools::ToolCapability::filesystem_read));
    auto isolated = security_system.authorize_tool(
        other_principal, "read_file", tools::ToolCapability::filesystem_read);
    assert(!isolated && isolated.error_if()->code == common::ErrorCode::permission_denied);

    auto policy = security_system.tool_policy(principal);
    assert(policy);
    assert(policy.value_if()->allowed_tools.size() == 1);
    assert(policy.value_if()->allowed_tools.front() == "read_file");
    assert(policy.value_if()->granted_capabilities.size() == 1);
    assert(policy.value_if()->granted_capabilities.front() ==
           tools::ToolCapability::filesystem_read);

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

    assert(runtime.shutdown());
    assert(!security_system.initialized());
    return 0;
}
