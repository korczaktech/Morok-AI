#pragma once

#include "morok/common/ids.hpp"
#include "morok/common/result.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/runtime.hpp"
#include "morok/tools/tool_subsystem.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace morok::security {

enum class ContentOrigin {
    user_input,
    external_document,
    retrieved_memory,
    model_output,
    tool_output,
    trusted_configuration
};

struct TrustLabel final {
    ContentOrigin origin{ContentOrigin::user_input};
    std::string source_id;
    bool untrusted{true};
};

[[nodiscard]] TrustLabel classify_content(ContentOrigin origin, std::string source_id = {});
[[nodiscard]] bool may_supply_security_policy(const TrustLabel& label) noexcept;

struct SecurityPrincipal final {
    std::string subject_id;
    common::SessionId session_id;
};

struct SecurityAction final {
    std::string tool_name;
    std::string operation;
    std::string target;
    std::string effect_summary;
};

struct SecurityGrant final {
    std::string subject_id;
    common::SessionId session_id;
    std::string tool_name;
    tools::ToolCapability capability{tools::ToolCapability::read_only};
};

struct SecurityRule final {
    std::string subject_id;
    std::string tool_name;
    tools::ToolCapability capability{tools::ToolCapability::read_only};
};

struct ConfirmationTicket final {
    std::string id;
    std::chrono::system_clock::time_point expires_at;
};

class ConfirmationGrant final {
public:
    ConfirmationGrant(const ConfirmationGrant&) = default;
    ConfirmationGrant& operator=(const ConfirmationGrant&) = default;

private:
    explicit ConfirmationGrant(std::string id) : id_(std::move(id)) {}
    std::string id_;
    friend class SecuritySubsystem;
};

enum class SecurityEventType {
    grant_added,
    grant_revoked,
    authorization_denied,
    confirmation_requested,
    confirmation_approved,
    confirmation_rejected,
    confirmation_consumed,
    confirmation_expired
};

struct SecurityAuditRecord final {
    SecurityEventType type{SecurityEventType::authorization_denied};
    std::string subject_id;
    std::optional<common::SessionId> session_id;
    std::string tool_name;
    std::string operation;
    std::chrono::system_clock::time_point timestamp;
};

using SecurityAuditSink = std::function<common::Status(const SecurityAuditRecord&)>;

struct SecurityConfig final {
    std::size_t max_grants{256};
    std::size_t max_pending_confirmations{128};
    std::chrono::seconds confirmation_ttl{std::chrono::minutes(5)};
    bool require_audit_sink{true};
};

class SecuritySubsystem final {
public:
    explicit SecuritySubsystem(SecurityConfig config = {}, SecurityAuditSink audit_sink = {});
    SecuritySubsystem(const SecuritySubsystem&) = delete;
    SecuritySubsystem& operator=(const SecuritySubsystem&) = delete;

    [[nodiscard]] common::Status initialize(const core::RuntimeConfig& config);
    [[nodiscard]] common::Status shutdown() noexcept;

    [[nodiscard]] common::Status grant_tool(
        const SecurityPrincipal& principal, std::string tool_name,
        tools::ToolCapability capability);
    [[nodiscard]] common::Status revoke_tool(
        const SecurityPrincipal& principal, const std::string& tool_name,
        tools::ToolCapability capability);

    [[nodiscard]] common::Result<tools::ToolAccessPolicy> tool_policy(
        const SecurityPrincipal& principal) const;
    [[nodiscard]] common::Status authorize_tool(
        const SecurityPrincipal& principal, const std::string& tool_name,
        tools::ToolCapability capability) const;

    [[nodiscard]] common::Result<ConfirmationTicket> request_confirmation(
        const SecurityPrincipal& principal, const SecurityAction& action);
    [[nodiscard]] common::Result<ConfirmationGrant> approve_confirmation(
        const SecurityPrincipal& principal, const ConfirmationTicket& ticket,
        const SecurityAction& action, bool human_approved);
    [[nodiscard]] common::Status consume_confirmation(
        const SecurityPrincipal& principal, const ConfirmationGrant& grant,
        const SecurityAction& action);

    [[nodiscard]] core::Runtime::Initializer initializer();
    [[nodiscard]] core::Runtime::Shutdown shutdown_callback();
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t grant_count() const noexcept;
    [[nodiscard]] std::size_t pending_confirmations() const noexcept;

private:
    struct PendingConfirmation final {
        SecurityPrincipal principal;
        SecurityAction action;
        std::chrono::system_clock::time_point expires_at;
        bool approved{false};
    };

    [[nodiscard]] common::Status validate_config(const core::RuntimeConfig& config) const;
    [[nodiscard]] common::Status audit(SecurityAuditRecord record) const;
    [[nodiscard]] bool has_grant_locked(
        const SecurityPrincipal& principal, const std::string& tool_name,
        tools::ToolCapability capability) const noexcept;
    [[nodiscard]] static bool valid_principal(const SecurityPrincipal& principal) noexcept;
    [[nodiscard]] static bool valid_action(const SecurityAction& action) noexcept;
    [[nodiscard]] static bool same_action(
        const SecurityAction& left, const SecurityAction& right) noexcept;
    void purge_expired_locked(std::chrono::system_clock::time_point now);

    SecurityConfig config_;
    SecurityAuditSink audit_sink_;
    mutable std::mutex mutex_;
    std::vector<SecurityGrant> grants_;
    std::map<std::string, PendingConfirmation> confirmations_;
    bool initialized_{false};
};

} // namespace morok::security
