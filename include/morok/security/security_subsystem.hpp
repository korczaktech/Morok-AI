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
#include <utility>

namespace morok::security {

enum class ContentOrigin {
    user_input,
    external_document,
    retrieved_memory,
    model_output,
    tool_output
};

class TrustLabel final {
public:
    [[nodiscard]] ContentOrigin origin() const noexcept { return origin_; }
    [[nodiscard]] const std::string& source_id() const noexcept { return source_id_; }
    [[nodiscard]] bool is_untrusted() const noexcept { return true; }

private:
    TrustLabel(ContentOrigin origin, std::string source_id)
        : origin_(origin), source_id_(std::move(source_id)) {}
    ContentOrigin origin_;
    std::string source_id_;
    friend TrustLabel classify_content(ContentOrigin, std::string);
};

[[nodiscard]] TrustLabel classify_content(ContentOrigin origin, std::string source_id = {});
[[nodiscard]] std::string bind_tool_arguments(const tools::ToolArguments& arguments);

struct SecurityPrincipal final {
    std::string subject_id;
    common::SessionId session_id;
};

struct SecurityAction final {
    std::string tool_name;
    std::string operation;
    std::string target;
    std::string effect_summary;
    std::string arguments_binding;
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
    [[nodiscard]] common::Result<std::string> invoke_tool(
        tools::ToolSubsystem& tool_subsystem,
        const core::RequestContext& request,
        const SecurityPrincipal& principal,
        const std::string& tool_name,
        const tools::ToolArguments& arguments,
        const SecurityAction* action = nullptr,
        const ConfirmationGrant* confirmation = nullptr);

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
