#include "morok/tools/tool_subsystem.hpp"

#include "morok/common/utf8_text.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace morok::tools {
namespace {

common::Error make_error(common::ErrorCode code, std::string message,
                         std::string stage,
                         const core::RequestContext* request = nullptr) {
    auto error = common::Error::make(code, std::move(message), "tools", std::move(stage));
    if (request) error.request_id = request->request_id();
    return error;
}

common::Status failure(common::ErrorCode code, std::string message,
                       std::string stage,
                       const core::RequestContext* request = nullptr) {
    return common::Status::failure(make_error(
        code, std::move(message), std::move(stage), request));
}

template <typename T>
common::Result<T> result_failure(common::ErrorCode code, std::string message,
                                  std::string stage,
                                  const core::RequestContext* request = nullptr) {
    return common::Result<T>::failure(make_error(
        code, std::move(message), std::move(stage), request));
}

bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    const auto first = static_cast<unsigned char>(name.front());
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_'))
        return false;
    for (const char raw : name) {
        const auto c = static_cast<unsigned char>(raw);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return false;
    }
    return true;
}

bool valid_capability(ToolCapability capability) {
    switch (capability) {
    case ToolCapability::read_only:
    case ToolCapability::filesystem_read:
    case ToolCapability::filesystem_write:
    case ToolCapability::network:
    case ToolCapability::external_write: return true;
    }
    return false;
}

bool has_type(const ToolValue& value, ToolValueType type) {
    switch (type) {
    case ToolValueType::string: return std::holds_alternative<std::string>(value);
    case ToolValueType::number:
        return std::holds_alternative<double>(value) &&
               std::isfinite(std::get<double>(value));
    case ToolValueType::boolean: return std::holds_alternative<bool>(value);
    }
    return false;
}

std::size_t argument_size(const ToolArguments& arguments) {
    std::size_t total = 0;
    for (const auto& [name, value] : arguments) {
        if (name.size() > std::numeric_limits<std::size_t>::max() - total)
            return std::numeric_limits<std::size_t>::max();
        total += name.size();
        std::size_t value_size = sizeof(value);
        if (const auto* text = std::get_if<std::string>(&value)) value_size = text->size();
        if (value_size > std::numeric_limits<std::size_t>::max() - total)
            return std::numeric_limits<std::size_t>::max();
        total += value_size;
    }
    return total;
}

} // namespace

ToolSubsystem::ToolSubsystem(ToolSubsystemConfig config, ToolAuditSink audit_sink)
    : config_(std::move(config)), audit_sink_(std::move(audit_sink)) {}

common::Status ToolSubsystem::validate_config(const core::RuntimeConfig& config) const {
    const auto runtime_valid = config.validate();
    if (!runtime_valid) return runtime_valid;
    if (config_.max_registered_tools == 0 || config_.max_concurrent_executions == 0 ||
        config_.max_timeout_ms == 0 || config_.max_input_bytes == 0 ||
        config_.max_output_bytes == 0 ||
        (config_.require_audit_sink && !audit_sink_)) {
        return failure(common::ErrorCode::invalid_argument,
            "invalid IA-TOOLS limits or mandatory audit sink is missing", "configuration");
    }
    return common::Status::success();
}

common::Status ToolSubsystem::initialize(const core::RuntimeConfig& config) {
    std::lock_guard lock(mutex_);
    if (initialized_) {
        return failure(common::ErrorCode::invalid_argument,
                       "IA-TOOLS is already initialized", "lifecycle");
    }
    const auto valid = validate_config(config);
    if (!valid) return valid;
    initialized_ = true;
    return common::Status::success();
}

common::Status ToolSubsystem::shutdown() noexcept {
    std::lock_guard lock(mutex_);
    if (active_executions_ != 0) {
        return failure(common::ErrorCode::resource_limit,
                       "cannot shut down IA-TOOLS while tools are executing", "lifecycle");
    }
    initialized_ = false;
    tools_.clear();
    return common::Status::success();
}

common::Status ToolSubsystem::validate_definition(const ToolDefinition& definition) const {
    if (!valid_name(definition.name) || definition.description.empty() ||
        definition.description.size() > 1024 || !valid_capability(definition.capability) ||
        !definition.handler || definition.timeout_ms == 0 ||
        definition.timeout_ms > config_.max_timeout_ms ||
        definition.max_input_bytes == 0 ||
        definition.max_input_bytes > config_.max_input_bytes ||
        definition.max_output_bytes == 0 ||
        definition.max_output_bytes > config_.max_output_bytes) {
        return failure(common::ErrorCode::invalid_argument,
                       "invalid tool definition", "registration");
    }

    std::set<std::string> field_names;
    for (const auto& field : definition.schema.fields) {
        if (!valid_name(field.name) || !field_names.insert(field.name).second ||
            (field.type == ToolValueType::string && field.max_string_bytes == 0)) {
            return failure(common::ErrorCode::invalid_argument,
                           "invalid or duplicate tool schema field", "registration");
        }
        if (field.type != ToolValueType::string &&
            field.type != ToolValueType::number &&
            field.type != ToolValueType::boolean) {
            return failure(common::ErrorCode::invalid_argument,
                           "unknown tool schema field type", "registration");
        }
    }
    return common::Status::success();
}

common::Status ToolSubsystem::register_tool(ToolDefinition definition) {
    const auto valid = validate_definition(definition);
    if (!valid) return valid;

    std::lock_guard lock(mutex_);
    if (!initialized_) {
        return failure(common::ErrorCode::model_not_loaded,
                       "IA-TOOLS is not initialized", "registration");
    }
    if (tools_.contains(definition.name)) {
        return failure(common::ErrorCode::invalid_argument,
                       "tool name is already registered", "registration");
    }
    if (tools_.size() >= config_.max_registered_tools) {
        return failure(common::ErrorCode::resource_limit,
                       "registered tool limit reached", "registration");
    }
    tools_.emplace(definition.name, std::move(definition));
    return common::Status::success();
}

common::Status ToolSubsystem::unregister_tool(const std::string& name) {
    std::lock_guard lock(mutex_);
    if (!initialized_) {
        return failure(common::ErrorCode::model_not_loaded,
                       "IA-TOOLS is not initialized", "registration");
    }
    if (tools_.erase(name) == 0) {
        return failure(common::ErrorCode::invalid_argument,
                       "tool is not registered", "registration");
    }
    return common::Status::success();
}

bool ToolSubsystem::authorized(
    const ToolDefinition& definition, const ToolAccessPolicy& policy) const noexcept {
    const bool named = std::find(policy.allowed_tools.begin(), policy.allowed_tools.end(),
                                 definition.name) != policy.allowed_tools.end();
    const bool capability = std::find(policy.granted_capabilities.begin(),
        policy.granted_capabilities.end(), definition.capability) !=
        policy.granted_capabilities.end();
    return named && capability;
}

common::Status ToolSubsystem::validate_arguments(
    const ToolDefinition& definition, const ToolArguments& arguments) const {
    if (argument_size(arguments) > definition.max_input_bytes) {
        return failure(common::ErrorCode::input_too_large,
                       "tool arguments exceed input limit", "validation");
    }
    for (const auto& [name, value] : arguments) {
        const auto field = std::find_if(definition.schema.fields.begin(),
            definition.schema.fields.end(),
            [&name](const ToolField& candidate) { return candidate.name == name; });
        if (field == definition.schema.fields.end()) {
            if (definition.schema.reject_unknown_fields) {
                return failure(common::ErrorCode::invalid_argument,
                               "unknown tool argument: " + name, "validation");
            }
            continue;
        }
        if (!has_type(value, field->type)) {
            return failure(common::ErrorCode::invalid_argument,
                           "tool argument has incorrect type: " + name, "validation");
        }
        if (const auto* text = std::get_if<std::string>(&value)) {
            if (text->size() > field->max_string_bytes ||
                !common::Utf8Text::from_utf8(*text)) {
                return failure(common::ErrorCode::invalid_argument,
                               "tool string argument is invalid or too large: " + name,
                               "validation");
            }
        }
    }
    for (const auto& field : definition.schema.fields) {
        if (field.required && !arguments.contains(field.name)) {
            return failure(common::ErrorCode::invalid_argument,
                           "required tool argument is missing: " + field.name,
                           "validation");
        }
    }
    return common::Status::success();
}

common::Status ToolSubsystem::audit(
    ToolAuditEventType type, const ToolDefinition& definition,
    const core::RequestContext& request, std::string detail) const {
    if (!audit_sink_) {
        if (config_.require_audit_sink) {
            return failure(common::ErrorCode::tool_failure,
                           "audit sink is unavailable", "audit", &request);
        }
        return common::Status::success();
    }
    ToolAuditRecord record;
    record.type = type;
    record.tool_name = definition.name;
    record.session_id = request.session_id();
    record.request_id = request.request_id();
    record.timestamp = std::chrono::system_clock::now();
    record.detail = std::move(detail);
    try {
        const auto written = audit_sink_(record);
        if (!written) {
            auto error = *written.error_if();
            error.subsystem = "tools";
            error.stage = "audit";
            error.request_id = request.request_id();
            return common::Status::failure(std::move(error));
        }
    } catch (...) {
        return failure(common::ErrorCode::tool_failure,
                       "audit sink threw an exception", "audit", &request);
    }
    return common::Status::success();
}

common::Result<std::vector<ToolDescriptor>> ToolSubsystem::discover(
    const core::RequestContext& request, const ToolAccessPolicy& policy) const {
    if (request.is_cancelled()) {
        return result_failure<std::vector<ToolDescriptor>>(
            common::ErrorCode::cancelled, "request cancelled", "discovery", &request);
    }
    std::lock_guard lock(mutex_);
    if (!initialized_) {
        return result_failure<std::vector<ToolDescriptor>>(
            common::ErrorCode::model_not_loaded, "IA-TOOLS is not initialized",
            "discovery", &request);
    }
    std::vector<ToolDescriptor> visible;
    for (const auto& [name, definition] : tools_) {
        (void)name;
        if (authorized(definition, policy)) {
            visible.push_back(ToolDescriptor{
                definition.name, definition.description, definition.schema,
                definition.capability, definition.requires_confirmation});
        }
    }
    return common::Result<std::vector<ToolDescriptor>>::success(std::move(visible));
}

common::Result<std::string> ToolSubsystem::invoke(
    const core::RequestContext& request, const std::string& name,
    const ToolArguments& arguments, const ToolAccessPolicy& policy,
    bool user_confirmed) {
    ToolDefinition definition;
    {
        std::lock_guard lock(mutex_);
        if (!initialized_) {
            return result_failure<std::string>(common::ErrorCode::model_not_loaded,
                "IA-TOOLS is not initialized", "execution", &request);
        }
        const auto found = tools_.find(name);
        if (found == tools_.end()) {
            return result_failure<std::string>(common::ErrorCode::invalid_argument,
                "tool is not registered", "discovery", &request);
        }
        definition = found->second;
        if (!authorized(definition, policy)) {
            (void)audit(ToolAuditEventType::denied, definition, request,
                        "authorization denied");
            return result_failure<std::string>(common::ErrorCode::permission_denied,
                "tool is not authorized for this request", "authorization", &request);
        }
        if (active_executions_ >= config_.max_concurrent_executions) {
            return result_failure<std::string>(common::ErrorCode::resource_limit,
                "concurrent tool execution limit reached", "execution", &request);
        }
        if (definition.requires_confirmation && !user_confirmed) {
            (void)audit(ToolAuditEventType::confirmation_required, definition, request,
                        "explicit user confirmation required");
            return result_failure<std::string>(common::ErrorCode::confirmation_required,
                "tool requires explicit user confirmation", "authorization", &request);
        }
        ++active_executions_;
    }

    const auto release_slot = [this]() {
        std::lock_guard lock(mutex_);
        if (active_executions_ > 0) --active_executions_;
    };

    if (request.is_cancelled()) {
        release_slot();
        (void)audit(ToolAuditEventType::cancelled, definition, request, "cancelled before start");
        return result_failure<std::string>(common::ErrorCode::cancelled,
            "request cancelled", "execution", &request);
    }

    const auto arguments_valid = validate_arguments(definition, arguments);
    if (!arguments_valid) {
        release_slot();
        (void)audit(ToolAuditEventType::denied, definition, request,
                    "argument validation rejected invocation");
        return result_failure<std::string>(arguments_valid.error_if()->code,
            arguments_valid.error_if()->message, "validation", &request);
    }

    const auto started = audit(ToolAuditEventType::started, definition, request,
                               "tool execution started");
    if (!started) {
        release_slot();
        return common::Result<std::string>::failure(*started.error_if());
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(definition.timeout_ms);
    ToolExecutionContext context{
        request.session_id(), request.request_id(), [&request] { return request.is_cancelled(); }, deadline};

    common::Result<std::string> result = result_failure<std::string>(
        common::ErrorCode::tool_failure, "tool handler did not return", "execution", &request);
    try {
        result = definition.handler(context, arguments);
    } catch (...) {
        result = result_failure<std::string>(common::ErrorCode::tool_failure,
            "tool handler threw an exception", "execution", &request);
    }
    const bool timed_out = context.deadline_exceeded();
    release_slot();

    if (request.is_cancelled()) {
        (void)audit(ToolAuditEventType::cancelled, definition, request, "cancelled during execution");
        return result_failure<std::string>(common::ErrorCode::cancelled,
            "tool execution cancelled", "execution", &request);
    }
    if (timed_out) {
        (void)audit(ToolAuditEventType::timed_out, definition, request,
                    "cooperative deadline exceeded");
        return result_failure<std::string>(common::ErrorCode::tool_timeout,
            "tool execution exceeded its cooperative deadline", "execution", &request);
    }
    if (!result) {
        (void)audit(ToolAuditEventType::failed, definition, request, "tool handler returned an error");
        auto error = *result.error_if();
        error.subsystem = "tools";
        error.stage = "execution";
        error.request_id = request.request_id();
        return common::Result<std::string>::failure(std::move(error));
    }
    if (result.value_if()->size() > definition.max_output_bytes ||
        !common::Utf8Text::from_utf8(*result.value_if())) {
        (void)audit(ToolAuditEventType::failed, definition, request,
                    "tool output invalid or exceeds output limit");
        return result_failure<std::string>(common::ErrorCode::resource_limit,
            "tool output is invalid UTF-8 or exceeds output limit", "output", &request);
    }
    const auto completed = audit(ToolAuditEventType::succeeded, definition, request,
                                 "tool execution completed");
    if (!completed) return common::Result<std::string>::failure(*completed.error_if());
    return result;
}

core::Runtime::Initializer ToolSubsystem::initializer() {
    return [this](const core::RuntimeConfig& config) { return initialize(config); };
}

core::Runtime::Shutdown ToolSubsystem::shutdown_callback() {
    return [this] { (void)shutdown(); };
}

bool ToolSubsystem::initialized() const noexcept {
    std::lock_guard lock(mutex_);
    return initialized_;
}

std::size_t ToolSubsystem::registered_tools() const noexcept {
    std::lock_guard lock(mutex_);
    return tools_.size();
}

std::size_t ToolSubsystem::active_executions() const noexcept {
    std::lock_guard lock(mutex_);
    return active_executions_;
}

} // namespace morok::tools
