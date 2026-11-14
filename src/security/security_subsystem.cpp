#include "morok/security/security_subsystem.hpp"

#include <algorithm>
#include <utility>

namespace morok::security {
namespace {

common::Error make_error(common::ErrorCode code, std::string message,
                         std::string stage) {
    return common::Error::make(code, std::move(message), "security", std::move(stage));
}

common::Status failure(common::ErrorCode code, std::string message,
                       std::string stage) {
    return common::Status::failure(make_error(code, std::move(message), std::move(stage)));
}

template <typename T>
common::Result<T> result_failure(common::ErrorCode code, std::string message,
                                  std::string stage) {
    return common::Result<T>::failure(make_error(
        code, std::move(message), std::move(stage)));
}

bool valid_tool_capability(tools::ToolCapability capability) noexcept {
    switch (capability) {
    case tools::ToolCapability::read_only:
    case tools::ToolCapability::filesystem_read:
    case tools::ToolCapability::filesystem_write:
    case tools::ToolCapability::network:
    case tools::ToolCapability::external_write: return true;
    }
    return false;
}

} // namespace

TrustLabel classify_content(ContentOrigin origin, std::string source_id) {
    return TrustLabel{origin, std::move(source_id),
                      origin != ContentOrigin::trusted_configuration};
}

bool may_supply_security_policy(const TrustLabel& label) noexcept {
    return label.origin == ContentOrigin::trusted_configuration && !label.untrusted;
}

SecuritySubsystem::SecuritySubsystem(SecurityConfig config, SecurityAuditSink audit_sink)
    : config_(std::move(config)), audit_sink_(std::move(audit_sink)) {}

common::Status SecuritySubsystem::validate_config(const core::RuntimeConfig& config) const {
    const auto runtime_valid = config.validate();
    if (!runtime_valid) return runtime_valid;
    if (config_.max_grants == 0 || config_.max_pending_confirmations == 0 ||
        config_.confirmation_ttl <= std::chrono::seconds::zero() ||
        (config_.require_audit_sink && !audit_sink_)) {
        return failure(common::ErrorCode::invalid_argument,
            "invalid security limits or mandatory audit sink is missing", "configuration");
    }
    return common::Status::success();
}

common::Status SecuritySubsystem::initialize(const core::RuntimeConfig& config) {
    std::lock_guard lock(mutex_);
    if (initialized_) {
        return failure(common::ErrorCode::invalid_argument,
                       "security subsystem is already initialized", "lifecycle");
    }
    const auto valid = validate_config(config);
    if (!valid) return valid;
    initialized_ = true;
    return common::Status::success();
}

common::Status SecuritySubsystem::shutdown() noexcept {
    std::lock_guard lock(mutex_);
    confirmations_.clear();
    grants_.clear();
    initialized_ = false;
    return common::Status::success();
}

bool SecuritySubsystem::valid_principal(const SecurityPrincipal& principal) noexcept {
    return !principal.subject_id.empty() && principal.subject_id.size() <= 128 &&
           !principal.session_id.is_nil();
}

bool SecuritySubsystem::valid_action(const SecurityAction& action) noexcept {
    return !action.tool_name.empty() && action.tool_name.size() <= 64 &&
           !action.operation.empty() && action.operation.size() <= 128 &&
           !action.target.empty() && action.target.size() <= 512 &&
           !action.effect_summary.empty() && action.effect_summary.size() <= 1024;
}

bool SecuritySubsystem::same_action(
    const SecurityAction& left, const SecurityAction& right) noexcept {
    return left.tool_name == right.tool_name &&
           left.operation == right.operation &&
           left.target == right.target &&
           left.effect_summary == right.effect_summary;
}

common::Status SecuritySubsystem::audit(SecurityAuditRecord record) const {
    if (!audit_sink_) {
        if (config_.require_audit_sink) {
            return failure(common::ErrorCode::tool_failure,
                           "security audit sink is unavailable", "audit");
        }
        return common::Status::success();
    }
    record.timestamp = std::chrono::system_clock::now();
    try {
        return audit_sink_(record);
    } catch (...) {
        return failure(common::ErrorCode::tool_failure,
                       "security audit sink threw an exception", "audit");
    }
}

bool SecuritySubsystem::has_grant_locked(
    const SecurityPrincipal& principal, const std::string& tool_name,
    tools::ToolCapability capability) const noexcept {
    return std::any_of(grants_.begin(), grants_.end(),
        [&](const SecurityGrant& grant) {
            return grant.subject_id == principal.subject_id &&
                   grant.session_id == principal.session_id &&
                   grant.tool_name == tool_name &&
                   grant.capability == capability;
        });
}

common::Status SecuritySubsystem::grant_tool(
    const SecurityPrincipal& principal, std::string tool_name,
    tools::ToolCapability capability) {
    if (!valid_principal(principal) || tool_name.empty() || tool_name.size() > 64 ||
        !valid_tool_capability(capability)) {
        return failure(common::ErrorCode::invalid_argument,
                       "invalid principal or tool grant", "policy");
    }
    {
        std::lock_guard lock(mutex_);
        if (!initialized_) {
            return failure(common::ErrorCode::model_not_loaded,
                           "security subsystem is not initialized", "policy");
        }
        if (has_grant_locked(principal, tool_name, capability)) {
            return failure(common::ErrorCode::invalid_argument,
                           "tool grant already exists", "policy");
        }
        if (grants_.size() >= config_.max_grants) {
            return failure(common::ErrorCode::resource_limit,
                           "security grant limit reached", "policy");
        }
        grants_.push_back(SecurityGrant{
            principal.subject_id, principal.session_id, tool_name, capability});
    }
    SecurityAuditRecord record;
    record.type = SecurityEventType::grant_added;
    record.subject_id = principal.subject_id;
    record.session_id = principal.session_id;
    record.tool_name = std::move(tool_name);
    record.operation = "grant";
    const auto written = audit(std::move(record));
    if (!written) {
        std::lock_guard lock(mutex_);
        std::erase_if(grants_, [&](const SecurityGrant& grant) {
            return grant.subject_id == principal.subject_id &&
                   grant.session_id == principal.session_id &&
                   grant.tool_name == record.tool_name &&
                   grant.capability == capability;
        });
        return written;
    }
    return common::Status::success();
}

common::Status SecuritySubsystem::revoke_tool(
    const SecurityPrincipal& principal, const std::string& tool_name,
    tools::ToolCapability capability) {
    if (!valid_principal(principal) || tool_name.empty() || !valid_tool_capability(capability)) {
        return failure(common::ErrorCode::invalid_argument,
                       "invalid principal or tool grant", "policy");
    }
    bool removed = false;
    {
        std::lock_guard lock(mutex_);
        if (!initialized_) {
            return failure(common::ErrorCode::model_not_loaded,
                           "security subsystem is not initialized", "policy");
        }
        const auto before = grants_.size();
        std::erase_if(grants_, [&](const SecurityGrant& grant) {
            return grant.subject_id == principal.subject_id &&
                   grant.session_id == principal.session_id &&
                   grant.tool_name == tool_name && grant.capability == capability;
        });
        removed = grants_.size() != before;
    }
    if (!removed) {
        return failure(common::ErrorCode::permission_denied,
                       "no matching grant exists", "policy");
    }
    SecurityAuditRecord record;
    record.type = SecurityEventType::grant_revoked;
    record.subject_id = principal.subject_id;
    record.session_id = principal.session_id;
    record.tool_name = tool_name;
    record.operation = "revoke";
    return audit(std::move(record));
}

common::Result<tools::ToolAccessPolicy> SecuritySubsystem::tool_policy(
    const SecurityPrincipal& principal) const {
    if (!valid_principal(principal)) {
        return result_failure<tools::ToolAccessPolicy>(
            common::ErrorCode::invalid_argument, "invalid security principal", "policy");
    }
    std::lock_guard lock(mutex_);
    if (!initialized_) {
        return result_failure<tools::ToolAccessPolicy>(
            common::ErrorCode::model_not_loaded,
            "security subsystem is not initialized", "policy");
    }
    tools::ToolAccessPolicy policy;
    for (const auto& grant : grants_) {
        if (grant.subject_id != principal.subject_id ||
            !(grant.session_id == principal.session_id)) continue;
        if (std::find(policy.allowed_tools.begin(), policy.allowed_tools.end(),
                      grant.tool_name) == policy.allowed_tools.end()) {
            policy.allowed_tools.push_back(grant.tool_name);
        }
        if (std::find(policy.granted_capabilities.begin(),
                      policy.granted_capabilities.end(), grant.capability) ==
            policy.granted_capabilities.end()) {
            policy.granted_capabilities.push_back(grant.capability);
        }
    }
    return common::Result<tools::ToolAccessPolicy>::success(std::move(policy));
}

common::Status SecuritySubsystem::authorize_tool(
    const SecurityPrincipal& principal, const std::string& tool_name,
    tools::ToolCapability capability) const {
    if (!valid_principal(principal) || tool_name.empty() || !valid_tool_capability(capability)) {
        return failure(common::ErrorCode::invalid_argument,
                       "invalid authorization request", "authorization");
    }
    bool allowed = false;
    {
        std::lock_guard lock(mutex_);
        if (!initialized_) {
            return failure(common::ErrorCode::model_not_loaded,
                           "security subsystem is not initialized", "authorization");
        }
        allowed = has_grant_locked(principal, tool_name, capability);
    }
    if (allowed) return common::Status::success();

    SecurityAuditRecord record;
    record.type = SecurityEventType::authorization_denied;
    record.subject_id = principal.subject_id;
    record.session_id = principal.session_id;
    record.tool_name = tool_name;
    record.operation = "deny";
    (void)audit(std::move(record));
    return failure(common::ErrorCode::permission_denied,
                   "no explicit grant for requested tool capability", "authorization");
}

void SecuritySubsystem::purge_expired_locked(std::chrono::system_clock::time_point now) {
    std::erase_if(confirmations_, [&](const auto& item) {
        return item.second.expires_at <= now;
    });
}

common::Result<ConfirmationTicket> SecuritySubsystem::request_confirmation(
    const SecurityPrincipal& principal, const SecurityAction& action) {
    if (!valid_principal(principal) || !valid_action(action)) {
        return result_failure<ConfirmationTicket>(
            common::ErrorCode::invalid_argument, "invalid confirmation request", "confirmation");
    }
    auto generated = common::RequestId::generate();
    if (!generated) {
        return common::Result<ConfirmationTicket>::failure(*generated.error_if());
    }
    const auto now = std::chrono::system_clock::now();
    ConfirmationTicket ticket{
        generated.value_if()->to_string(), now + config_.confirmation_ttl};
    {
        std::lock_guard lock(mutex_);
        if (!initialized_) {
            return result_failure<ConfirmationTicket>(
                common::ErrorCode::model_not_loaded,
                "security subsystem is not initialized", "confirmation");
        }
        purge_expired_locked(now);
        if (confirmations_.size() >= config_.max_pending_confirmations) {
            return result_failure<ConfirmationTicket>(
                common::ErrorCode::resource_limit,
                "pending confirmation limit reached", "confirmation");
        }
        confirmations_.emplace(ticket.id, PendingConfirmation{
            principal, action, ticket.expires_at, false});
    }
    SecurityAuditRecord record;
    record.type = SecurityEventType::confirmation_requested;
    record.subject_id = principal.subject_id;
    record.session_id = principal.session_id;
    record.tool_name = action.tool_name;
    record.operation = action.operation;
    const auto written = audit(std::move(record));
    if (!written) {
        std::lock_guard lock(mutex_);
        confirmations_.erase(ticket.id);
        return common::Result<ConfirmationTicket>::failure(*written.error_if());
    }
    return common::Result<ConfirmationTicket>::success(std::move(ticket));
}

common::Result<ConfirmationGrant> SecuritySubsystem::approve_confirmation(
    const SecurityPrincipal& principal, const ConfirmationTicket& ticket,
    const SecurityAction& action, bool human_approved) {
    if (!valid_principal(principal) || !valid_action(action) || ticket.id.empty()) {
        return result_failure<ConfirmationGrant>(
            common::ErrorCode::invalid_argument, "invalid confirmation approval", "confirmation");
    }
    bool expired = false;
    {
        std::lock_guard lock(mutex_);
        if (!initialized_) {
            return result_failure<ConfirmationGrant>(
                common::ErrorCode::model_not_loaded,
                "security subsystem is not initialized", "confirmation");
        }
        const auto found = confirmations_.find(ticket.id);
        if (found == confirmations_.end()) {
            return result_failure<ConfirmationGrant>(
                common::ErrorCode::confirmation_required,
                "confirmation ticket is unknown or already consumed", "confirmation");
        }
        auto& pending = found->second;
        expired = pending.expires_at <= std::chrono::system_clock::now();
        if (expired) {
            confirmations_.erase(found);
        } else if (!(pending.principal.subject_id == principal.subject_id &&
                     pending.principal.session_id == principal.session_id) ||
                   !same_action(pending.action, action) ||
                   ticket.expires_at != pending.expires_at) {
            return result_failure<ConfirmationGrant>(
                common::ErrorCode::permission_denied,
                "confirmation ticket does not match principal, session or action",
                "confirmation");
        } else if (!human_approved) {
            confirmations_.erase(found);
        } else {
            pending.approved = true;
        }
    }
    SecurityAuditRecord record;
    record.subject_id = principal.subject_id;
    record.session_id = principal.session_id;
    record.tool_name = action.tool_name;
    record.operation = action.operation;
    if (expired) {
        record.type = SecurityEventType::confirmation_expired;
        (void)audit(std::move(record));
        return result_failure<ConfirmationGrant>(
            common::ErrorCode::confirmation_required,
            "confirmation ticket expired", "confirmation");
    }
    if (!human_approved) {
        record.type = SecurityEventType::confirmation_rejected;
        (void)audit(std::move(record));
        return result_failure<ConfirmationGrant>(
            common::ErrorCode::permission_denied,
            "human confirmation was rejected", "confirmation");
    }
    record.type = SecurityEventType::confirmation_approved;
    const auto written = audit(std::move(record));
    if (!written) {
        std::lock_guard lock(mutex_);
        confirmations_.erase(ticket.id);
        return common::Result<ConfirmationGrant>::failure(*written.error_if());
    }
    return common::Result<ConfirmationGrant>::success(ConfirmationGrant(ticket.id));
}

common::Status SecuritySubsystem::consume_confirmation(
    const SecurityPrincipal& principal, const ConfirmationGrant& grant,
    const SecurityAction& action) {
    if (!valid_principal(principal) || !valid_action(action)) {
        return failure(common::ErrorCode::invalid_argument,
                       "invalid confirmation consumption", "confirmation");
    }
    bool consumed = false;
    {
        std::lock_guard lock(mutex_);
        if (!initialized_) {
            return failure(common::ErrorCode::model_not_loaded,
                           "security subsystem is not initialized", "confirmation");
        }
        const auto found = confirmations_.find(grant.id_);
        if (found == confirmations_.end()) {
            return failure(common::ErrorCode::confirmation_required,
                           "confirmation grant is unknown or already consumed", "confirmation");
        }
        const auto& pending = found->second;
        if (pending.expires_at <= std::chrono::system_clock::now()) {
            confirmations_.erase(found);
            return failure(common::ErrorCode::confirmation_required,
                           "confirmation grant expired", "confirmation");
        }
        if (!pending.approved ||
            pending.principal.subject_id != principal.subject_id ||
            !(pending.principal.session_id == principal.session_id) ||
            !same_action(pending.action, action)) {
            return failure(common::ErrorCode::permission_denied,
                           "confirmation grant does not match this exact action",
                           "confirmation");
        }
        confirmations_.erase(found);
        consumed = true;
    }
    if (consumed) {
        SecurityAuditRecord record;
        record.type = SecurityEventType::confirmation_consumed;
        record.subject_id = principal.subject_id;
        record.session_id = principal.session_id;
        record.tool_name = action.tool_name;
        record.operation = action.operation;
        return audit(std::move(record));
    }
    return failure(common::ErrorCode::confirmation_required,
                   "confirmation was not consumed", "confirmation");
}

core::Runtime::Initializer SecuritySubsystem::initializer() {
    return [this](const core::RuntimeConfig& config) { return initialize(config); };
}

core::Runtime::Shutdown SecuritySubsystem::shutdown_callback() {
    return [this] { (void)shutdown(); };
}

bool SecuritySubsystem::initialized() const noexcept {
    std::lock_guard lock(mutex_);
    return initialized_;
}

std::size_t SecuritySubsystem::grant_count() const noexcept {
    std::lock_guard lock(mutex_);
    return grants_.size();
}

std::size_t SecuritySubsystem::pending_confirmations() const noexcept {
    std::lock_guard lock(mutex_);
    return confirmations_.size();
}

} // namespace morok::security
