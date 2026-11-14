#pragma once

#include "morok/common/ids.hpp"
#include "morok/common/result.hpp"
#include "morok/core/cancellation.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/request_context.hpp"
#include "morok/core/runtime.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

namespace morok::tools {

using ToolValue = std::variant<std::string, double, bool>;
using ToolArguments = std::map<std::string, ToolValue>;

enum class ToolValueType { string, number, boolean };
enum class ToolCapability { read_only, filesystem_read, filesystem_write, network, external_write };

struct ToolField final {
    std::string name;
    ToolValueType type{ToolValueType::string};
    bool required{true};
    std::size_t max_string_bytes{4096};
};

struct ToolSchema final {
    std::vector<ToolField> fields;
    bool reject_unknown_fields{true};
};

struct ToolAccessPolicy final {
    std::vector<std::string> allowed_tools;
    std::vector<ToolCapability> granted_capabilities;
};

struct ToolExecutionContext final {
    common::SessionId session_id;
    common::RequestId request_id;
    core::CancellationToken cancellation;
    std::chrono::steady_clock::time_point deadline;

    [[nodiscard]] bool is_cancelled() const noexcept {
        return cancellation.is_cancelled();
    }
    [[nodiscard]] bool deadline_exceeded() const noexcept {
        return std::chrono::steady_clock::now() >= deadline;
    }
};

struct ToolDefinition final {
    std::string name;
    std::string description;
    ToolSchema schema;
    ToolCapability capability{ToolCapability::read_only};
    bool requires_confirmation{false};
    std::size_t timeout_ms{1000};
    std::size_t max_input_bytes{16U * 1024U};
    std::size_t max_output_bytes{64U * 1024U};
    std::function<common::Result<std::string>(
        const ToolExecutionContext&, const ToolArguments&)> handler;
};

struct ToolDescriptor final {
    std::string name;
    std::string description;
    ToolSchema schema;
    ToolCapability capability{ToolCapability::read_only};
    bool requires_confirmation{false};
};

enum class ToolAuditEventType { registered, denied, confirmation_required, started, succeeded, failed, timed_out, cancelled };

struct ToolAuditRecord final {
    ToolAuditEventType type{ToolAuditEventType::failed};
    std::string tool_name;
    common::SessionId session_id;
    common::RequestId request_id;
    std::chrono::system_clock::time_point timestamp;
    std::string detail;
};

using ToolAuditSink = std::function<common::Status(const ToolAuditRecord&)>;

struct ToolSubsystemConfig final {
    std::size_t max_registered_tools{64};
    std::size_t max_concurrent_executions{1};
    std::size_t max_timeout_ms{30'000};
    std::size_t max_input_bytes{64U * 1024U};
    std::size_t max_output_bytes{256U * 1024U};
    bool require_audit_sink{true};
};

class ToolSubsystem final {
public:
    explicit ToolSubsystem(ToolSubsystemConfig config = {}, ToolAuditSink audit_sink = {});

    ToolSubsystem(const ToolSubsystem&) = delete;
    ToolSubsystem& operator=(const ToolSubsystem&) = delete;

    [[nodiscard]] common::Status initialize(const core::RuntimeConfig& config);
    [[nodiscard]] common::Status shutdown() noexcept;

    [[nodiscard]] common::Status register_tool(ToolDefinition definition);
    [[nodiscard]] common::Status unregister_tool(const std::string& name);

    [[nodiscard]] common::Result<std::vector<ToolDescriptor>> discover(
        const core::RequestContext& request,
        const ToolAccessPolicy& policy) const;

    [[nodiscard]] common::Result<std::string> invoke(
        const core::RequestContext& request,
        const std::string& name,
        const ToolArguments& arguments,
        const ToolAccessPolicy& policy,
        bool user_confirmed = false);

    [[nodiscard]] core::Runtime::Initializer initializer();
    [[nodiscard]] core::Runtime::Shutdown shutdown_callback();
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t registered_tools() const noexcept;
    [[nodiscard]] std::size_t active_executions() const noexcept;

private:
    [[nodiscard]] common::Status validate_config(const core::RuntimeConfig& config) const;
    [[nodiscard]] common::Status validate_definition(const ToolDefinition& definition) const;
    [[nodiscard]] common::Status validate_arguments(
        const ToolDefinition& definition, const ToolArguments& arguments) const;
    [[nodiscard]] common::Status audit(
        ToolAuditEventType type, const ToolDefinition& definition,
        const core::RequestContext& request, std::string detail) const;
    [[nodiscard]] bool authorized(
        const ToolDefinition& definition, const ToolAccessPolicy& policy) const noexcept;

    ToolSubsystemConfig config_;
    ToolAuditSink audit_sink_;
    mutable std::mutex mutex_;
    std::map<std::string, ToolDefinition> tools_;
    std::size_t active_executions_{0};
    bool initialized_{false};
};

} // namespace morok::tools
