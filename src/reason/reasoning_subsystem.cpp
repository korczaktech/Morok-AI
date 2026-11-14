#include "morok/reason/reasoning_subsystem.hpp"

#include "morok/common/utf8_text.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace morok::reason {
namespace {

common::Error make_error(common::ErrorCode code, std::string message,
                          std::string stage,
                          const core::RequestContext* request = nullptr) {
    auto error = common::Error::make(code, std::move(message), "reason", std::move(stage));
    if (request) error.request_id = request->request_id();
    return error;
}

common::Status fail_status(common::ErrorCode code, std::string message,
                            std::string stage,
                            const core::RequestContext* request = nullptr) {
    return common::Status::failure(make_error(
        code, std::move(message), std::move(stage), request));
}

template <typename T>
common::Result<T> fail_result(common::ErrorCode code, std::string message,
                              std::string stage,
                              const core::RequestContext* request = nullptr) {
    return common::Result<T>::failure(make_error(
        code, std::move(message), std::move(stage), request));
}

bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    const auto first = static_cast<unsigned char>(name.front());
    if (!((first >= 'a' && first <= 'z') ||
          (first >= 'A' && first <= 'Z') || first == '_')) return false;
    for (const char raw : name.substr(1)) {
        const auto c = static_cast<unsigned char>(raw);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return false;
    }
    return true;
}

std::size_t value_bytes(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value)) return text->size();
    return sizeof(value);
}

bool valid_value(const Value& value, std::size_t maximum_bytes) {
    if (value_bytes(value) > maximum_bytes) return false;
    if (const auto* number = std::get_if<double>(&value))
        return std::isfinite(*number);
    if (const auto* text = std::get_if<std::string>(&value))
        return static_cast<bool>(common::Utf8Text::from_utf8(*text));
    return true;
}

bool valid_operation(Operation operation) {
    switch (operation) {
    case Operation::assign: case Operation::add: case Operation::subtract:
    case Operation::multiply: case Operation::divide: case Operation::equal:
    case Operation::less: case Operation::logical_and: case Operation::logical_or:
    case Operation::logical_not: case Operation::concatenate: return true;
    }
    return false;
}

bool valid_failure_policy(FailurePolicy policy) {
    return policy == FailurePolicy::abort_plan ||
           policy == FailurePolicy::continue_independent;
}

bool numeric(const Value& value) {
    const auto* number = std::get_if<double>(&value);
    return number && std::isfinite(*number);
}

common::Result<bool> compare_values(const Value& left, Comparator comparator,
                                    const Value& right,
                                    const core::RequestContext& request) {
    if (comparator == Comparator::truthy) {
        const auto* boolean = std::get_if<bool>(&left);
        if (!boolean) return fail_result<bool>(
            common::ErrorCode::invalid_argument,
            "truthy condition requires a boolean value", "condition", &request);
        return common::Result<bool>::success(*boolean);
    }

    if (comparator == Comparator::equal || comparator == Comparator::not_equal) {
        const bool same_type = left.index() == right.index();
        const bool equal = same_type && left == right;
        return common::Result<bool>::success(
            comparator == Comparator::equal ? equal : !equal);
    }

    if (const auto* lhs = std::get_if<double>(&left)) {
        const auto* rhs = std::get_if<double>(&right);
        if (!rhs || !std::isfinite(*lhs) || !std::isfinite(*rhs))
            return fail_result<bool>(common::ErrorCode::invalid_argument,
                "ordered comparison requires finite numbers of the same type",
                "condition", &request);
        switch (comparator) {
        case Comparator::less: return common::Result<bool>::success(*lhs < *rhs);
        case Comparator::less_equal: return common::Result<bool>::success(*lhs <= *rhs);
        case Comparator::greater: return common::Result<bool>::success(*lhs > *rhs);
        case Comparator::greater_equal: return common::Result<bool>::success(*lhs >= *rhs);
        default: break;
        }
    }
    if (const auto* lhs = std::get_if<std::string>(&left)) {
        const auto* rhs = std::get_if<std::string>(&right);
        if (!rhs) return fail_result<bool>(common::ErrorCode::invalid_argument,
            "ordered comparison requires values of the same ordered type",
            "condition", &request);
        switch (comparator) {
        case Comparator::less: return common::Result<bool>::success(*lhs < *rhs);
        case Comparator::less_equal: return common::Result<bool>::success(*lhs <= *rhs);
        case Comparator::greater: return common::Result<bool>::success(*lhs > *rhs);
        case Comparator::greater_equal: return common::Result<bool>::success(*lhs >= *rhs);
        default: break;
        }
    }
    return fail_result<bool>(common::ErrorCode::invalid_argument,
        "ordered comparison is not defined for these value types",
        "condition", &request);
}

common::Status check_conditions(const std::vector<Condition>& conditions,
                                const std::map<std::string, Value>& values,
                                const core::RequestContext& request,
                                std::string_view stage) {
    for (const auto& condition : conditions) {
        const auto found = values.find(condition.key);
        if (found == values.end())
            return fail_status(common::ErrorCode::invalid_argument,
                "condition references an unavailable value: " + condition.key,
                std::string(stage), &request);
        const auto compared = compare_values(
            found->second, condition.comparator, condition.expected, request);
        if (!compared) return common::Status::failure(*compared.error_if());
        if (!*compared.value_if())
            return fail_status(common::ErrorCode::invalid_argument,
                "condition was not satisfied: " + condition.key,
                std::string(stage), &request);
    }
    return common::Status::success();
}

common::Result<Value> evaluate(const ReasoningStep& step,
                               const std::map<std::string, Value>& values,
                               const core::RequestContext& request) {
    if (step.operation == Operation::assign)
        return common::Result<Value>::success(step.literal);

    std::vector<const Value*> operands;
    operands.reserve(step.inputs.size());
    for (const auto& key : step.inputs) {
        const auto found = values.find(key);
        if (found == values.end())
            return fail_result<Value>(common::ErrorCode::invalid_argument,
                "step input is unavailable: " + key, "execute", &request);
        operands.push_back(&found->second);
    }

    switch (step.operation) {
    case Operation::add:
    case Operation::subtract:
    case Operation::multiply:
    case Operation::divide: {
        if (!numeric(*operands[0]) || !numeric(*operands[1]))
            return fail_result<Value>(common::ErrorCode::invalid_argument,
                "arithmetic operations require finite numeric operands",
                "execute", &request);
        const double a = std::get<double>(*operands[0]);
        const double b = std::get<double>(*operands[1]);
        double result = 0.0;
        if (step.operation == Operation::add) result = a + b;
        else if (step.operation == Operation::subtract) result = a - b;
        else if (step.operation == Operation::multiply) result = a * b;
        else {
            if (b == 0.0)
                return fail_result<Value>(common::ErrorCode::invalid_argument,
                    "division by zero", "execute", &request);
            result = a / b;
        }
        if (!std::isfinite(result))
            return fail_result<Value>(common::ErrorCode::resource_limit,
                "arithmetic result is not finite", "execute", &request);
        return common::Result<Value>::success(Value{result});
    }
    case Operation::equal:
    case Operation::less: {
        const auto comparison = compare_values(
            *operands[0],
            step.operation == Operation::equal ? Comparator::equal : Comparator::less,
            *operands[1], request);
        if (!comparison) return common::Result<Value>::failure(*comparison.error_if());
        return common::Result<Value>::success(Value{*comparison.value_if()});
    }
    case Operation::logical_and:
    case Operation::logical_or: {
        const auto* a = std::get_if<bool>(operands[0]);
        const auto* b = std::get_if<bool>(operands[1]);
        if (!a || !b)
            return fail_result<Value>(common::ErrorCode::invalid_argument,
                "logical binary operations require boolean operands",
                "execute", &request);
        return common::Result<Value>::success(Value{
            step.operation == Operation::logical_and ? (*a && *b) : (*a || *b)});
    }
    case Operation::logical_not: {
        const auto* value = std::get_if<bool>(operands[0]);
        if (!value)
            return fail_result<Value>(common::ErrorCode::invalid_argument,
                "logical_not requires a boolean operand", "execute", &request);
        return common::Result<Value>::success(Value{!*value});
    }
    case Operation::concatenate: {
        const auto* a = std::get_if<std::string>(operands[0]);
        const auto* b = std::get_if<std::string>(operands[1]);
        if (!a || !b)
            return fail_result<Value>(common::ErrorCode::invalid_argument,
                "concatenate requires string operands", "execute", &request);
        return common::Result<Value>::success(Value{*a + *b});
    }
    case Operation::assign: break;
    }
    return fail_result<Value>(common::ErrorCode::internal_error,
        "unsupported reasoning operation", "execute", &request);
}

std::size_t required_inputs(Operation operation) {
    switch (operation) {
    case Operation::assign: return 0;
    case Operation::logical_not: return 1;
    default: return 2;
    }
}

bool valid_condition(const Condition& condition) {
    const bool comparator_valid = condition.comparator == Comparator::equal ||
        condition.comparator == Comparator::not_equal ||
        condition.comparator == Comparator::less ||
        condition.comparator == Comparator::less_equal ||
        condition.comparator == Comparator::greater ||
        condition.comparator == Comparator::greater_equal ||
        condition.comparator == Comparator::truthy;
    return valid_name(condition.key) && comparator_valid &&
        valid_value(condition.expected, 4096);
}

} // namespace

ReasoningSubsystem::ReasoningSubsystem(ReasoningConfig config)
    : config_(config) {}

common::Status ReasoningSubsystem::validate_config(
    const core::RuntimeConfig& config) const {
    const auto runtime_valid = config.validate();
    if (!runtime_valid) return runtime_valid;
    if (config_.max_steps == 0 || config_.max_steps > 32 ||
        config_.max_dependency_depth == 0 || config_.max_dependency_depth > 8 ||
        config_.max_value_bytes == 0 || config_.max_objective_bytes == 0 ||
        config_.max_recovery_attempts > 1) {
        return fail_status(common::ErrorCode::resource_limit,
            "IA-REAS configuration exceeds the supported deterministic planner limits",
            "initialize");
    }
    return common::Status::success();
}

common::Status ReasoningSubsystem::initialize(const core::RuntimeConfig& config) {
    std::lock_guard lock(mutex_);
    if (initialized_)
        return fail_status(common::ErrorCode::invalid_argument,
            "IA-REAS is already initialized", "initialize");
    const auto valid = validate_config(config);
    if (!valid) return valid;
    initialized_ = true;
    return common::Status::success();
}

common::Status ReasoningSubsystem::shutdown() noexcept {
    std::lock_guard lock(mutex_);
    initialized_ = false;
    return common::Status::success();
}

common::Result<ReasoningPlan> ReasoningSubsystem::create_plan(
    const core::RequestContext& request, const ReasoningTask& task,
    const std::map<std::string, Value>& authorized_context) const {
    std::unique_lock lock(mutex_);
    if (!initialized_)
        return fail_result<ReasoningPlan>(common::ErrorCode::model_not_loaded,
            "IA-REAS is not initialized", "plan", &request);
    if (request.is_cancelled())
        return fail_result<ReasoningPlan>(common::ErrorCode::cancelled,
            "planning request was cancelled", "plan", &request);
    const auto objective_text = common::Utf8Text::from_utf8(task.objective);
    if (!objective_text || task.objective.empty() ||
        task.objective.size() > config_.max_objective_bytes ||
        task.steps.empty() || task.step_budget == 0 ||
        !valid_failure_policy(task.failure_policy) ||
        task.steps.size() > std::min(task.step_budget, config_.max_steps)) {
        return fail_result<ReasoningPlan>(common::ErrorCode::resource_limit,
            "task objective, step count, or execution budget is invalid",
            "plan", &request);
    }

    try {
        ReasoningPlan plan{request.session_id(), request.request_id(),
            task.objective, {}, task.preconditions, {}, 0, task.failure_policy};
        for (const auto& [key, value] : authorized_context) {
            if (!valid_name(key) || !valid_value(value, config_.max_value_bytes))
                return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                    "authorized context contains an invalid key or value",
                    "plan", &request);
            plan.initial_values.emplace(key, value);
        }
        for (const auto& [key, value] : task.initial_values) {
            if (!valid_name(key) || !valid_value(value, config_.max_value_bytes) ||
                plan.initial_values.contains(key))
                return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                    "task initial values contain an invalid or duplicate key",
                    "plan", &request);
            plan.initial_values.emplace(key, value);
        }
        for (const auto& condition : task.preconditions) {
            if (!valid_condition(condition) ||
                !plan.initial_values.contains(condition.key))
                return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                    "task precondition must reference an authorized initial value",
                    "plan", &request);
        }

        const std::size_t count = task.steps.size();
        std::unordered_map<std::string, std::size_t> step_by_id;
        std::unordered_map<std::string, std::size_t> producer_by_key;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& step = task.steps[i];
            if (!valid_name(step.id) || !valid_name(step.output) ||
                !step_by_id.emplace(step.id, i).second ||
                plan.initial_values.contains(step.output) ||
                !producer_by_key.emplace(step.output, i).second ||
                !valid_operation(step.operation) ||
                !valid_operation(step.operation) ||
                step.inputs.size() != required_inputs(step.operation)) {
                return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                    "step identifier, output, or operation signature is invalid",
                    "plan", &request);
            }
            if (step.operation == Operation::assign &&
                !valid_value(step.literal, config_.max_value_bytes))
                return fail_result<ReasoningPlan>(common::ErrorCode::input_too_large,
                    "literal exceeds IA-REAS value limit", "plan", &request);
            if (step.fallback && !valid_value(*step.fallback, config_.max_value_bytes))
                return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                    "step fallback is invalid", "plan", &request);
            for (const auto& condition : step.preconditions)
                if (!valid_condition(condition))
                    return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                        "step precondition is invalid", "plan", &request);
            for (const auto& condition : step.postconditions)
                if (!valid_condition(condition) || condition.key != step.output)
                    return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                        "step postconditions must validate that step output", "plan", &request);
        }

        std::vector<std::set<std::size_t>> edges(count);
        std::vector<std::set<std::size_t>> parents_by_step(count);
        std::vector<std::size_t> indegree(count, 0);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& step = task.steps[i];
            std::set<std::size_t> parents;
            for (const auto& dependency : step.dependencies) {
                const auto found = step_by_id.find(dependency);
                if (found == step_by_id.end() || found->second == i)
                    return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                        "step dependency is missing or self-referential",
                        "plan", &request);
                parents.insert(found->second);
            }
            for (const auto& input : step.inputs) {
                if (plan.initial_values.contains(input)) continue;
                const auto found = producer_by_key.find(input);
                if (found == producer_by_key.end() || found->second == i)
                    return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                        "step input has no initial value or producer: " + input,
                        "plan", &request);
                parents.insert(found->second);
            }
            for (const auto& condition : step.preconditions) {
                if (condition.key == step.output)
                    return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                        "step precondition cannot reference its own output",
                        "plan", &request);
                if (!plan.initial_values.contains(condition.key)) {
                    const auto found = producer_by_key.find(condition.key);
                    if (found == producer_by_key.end() || found->second == i)
                        return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                            "step precondition references an unknown value",
                            "plan", &request);
                    parents.insert(found->second);
                }
            }
            parents_by_step[i] = parents;
            for (const auto parent : parents) edges[parent].insert(i);
            indegree[i] = parents.size();
        }

        std::priority_queue<std::size_t, std::vector<std::size_t>,
                            std::greater<>> ready;
        for (std::size_t i = 0; i < count; ++i)
            if (indegree[i] == 0) ready.push(i);
        std::vector<std::size_t> depths(count, 1);
        while (!ready.empty()) {
            if (request.is_cancelled())
                return fail_result<ReasoningPlan>(common::ErrorCode::cancelled,
                    "planning request was cancelled", "plan", &request);
            const auto current = ready.top();
            ready.pop();
            auto ordered_step = task.steps[current];
            ordered_step.dependencies.clear();
            for (const auto parent : parents_by_step[current])
                ordered_step.dependencies.push_back(task.steps[parent].id);
            plan.ordered_steps.push_back(std::move(ordered_step));
            plan.dependency_depth = std::max(plan.dependency_depth, depths[current]);
            for (const auto next : edges[current]) {
                depths[next] = std::max(depths[next], depths[current] + 1);
                if (--indegree[next] == 0) ready.push(next);
            }
        }
        if (plan.ordered_steps.size() != count)
            return fail_result<ReasoningPlan>(common::ErrorCode::invalid_argument,
                "task dependency graph contains a cycle", "plan", &request);
        if (plan.dependency_depth > config_.max_dependency_depth)
            return fail_result<ReasoningPlan>(common::ErrorCode::resource_limit,
                "task dependency depth exceeds configured limit", "plan", &request);
        return common::Result<ReasoningPlan>::success(std::move(plan));
    } catch (const std::bad_alloc&) {
        return fail_result<ReasoningPlan>(common::ErrorCode::out_of_memory,
            "could not allocate deterministic reasoning plan", "plan", &request);
    }
}

common::Result<ReasoningResult> ReasoningSubsystem::execute(
    const core::RequestContext& request, const ReasoningPlan& plan) const {
    std::unique_lock lock(mutex_);
    if (!initialized_)
        return fail_result<ReasoningResult>(common::ErrorCode::model_not_loaded,
            "IA-REAS is not initialized", "execute", &request);
    if (request.is_cancelled())
        return fail_result<ReasoningResult>(common::ErrorCode::cancelled,
            "reasoning execution was cancelled", "execute", &request);
    if (request.session_id() != plan.session_id ||
        request.request_id() != plan.request_id)
        return fail_result<ReasoningResult>(common::ErrorCode::permission_denied,
            "reasoning plan belongs to a different request or session",
            "execute", &request);
    const auto objective_text = common::Utf8Text::from_utf8(plan.objective);
    if (!objective_text || plan.objective.empty() ||
        plan.objective.size() > config_.max_objective_bytes ||
        !valid_failure_policy(plan.failure_policy) ||
        plan.ordered_steps.empty() || plan.ordered_steps.size() > config_.max_steps ||
        plan.dependency_depth == 0 ||
        plan.dependency_depth > config_.max_dependency_depth)
        return fail_result<ReasoningResult>(common::ErrorCode::invalid_argument,
            "reasoning plan metadata is invalid", "execute", &request);

    try {
        ReasoningResult result;
        result.values = plan.initial_values;
        const auto preconditions = check_conditions(
            plan.preconditions, result.values, request, "task.preconditions");
        if (!preconditions)
            return common::Result<ReasoningResult>::failure(*preconditions.error_if());

        std::unordered_set<std::string> completed;
        std::unordered_set<std::string> seen_step_ids;
        std::unordered_set<std::string> output_keys;
        for (const auto& step : plan.ordered_steps) {
            if (!valid_name(step.id) || !valid_name(step.output) ||
                !seen_step_ids.insert(step.id).second ||
                !output_keys.insert(step.output).second ||
                result.values.contains(step.output) ||
                step.inputs.size() != required_inputs(step.operation)) {
                return fail_result<ReasoningResult>(common::ErrorCode::invalid_argument,
                    "reasoning plan contains duplicate or malformed steps",
                    "execute", &request);
            }
            if (request.is_cancelled())
                return fail_result<ReasoningResult>(common::ErrorCode::cancelled,
                    "reasoning execution was cancelled between steps",
                    "execute", &request);
            bool dependency_failed = false;
            for (const auto& dependency : step.dependencies)
                if (!completed.contains(dependency)) dependency_failed = true;
            if (dependency_failed) {
                if (plan.failure_policy == FailurePolicy::continue_independent) {
                    result.skipped_steps.push_back(step.id);
                    continue;
                }
                return fail_result<ReasoningResult>(common::ErrorCode::invalid_argument,
                    "a required dependency did not complete", "execute", &request);
            }
            const auto before = check_conditions(
                step.preconditions, result.values, request, "step.preconditions");
            if (!before) {
                if (plan.failure_policy == FailurePolicy::continue_independent) {
                    result.failed_steps.push_back(step.id);
                    continue;
                }
                return common::Result<ReasoningResult>::failure(*before.error_if());
            }

            auto candidate = evaluate(step, result.values, request);
            bool recovered = false;
            auto validate_candidate = [&](const Value& value) -> common::Status {
                auto staged = result.values;
                staged.insert_or_assign(step.output, value);
                return check_conditions(step.postconditions, staged, request,
                                        "step.postconditions");
            };
            if (candidate) {
                const auto post = validate_candidate(*candidate.value_if());
                if (!post) candidate = common::Result<Value>::failure(*post.error_if());
            }
            if (!candidate && step.fallback && config_.max_recovery_attempts == 1) {
                candidate = common::Result<Value>::success(*step.fallback);
                const auto post = validate_candidate(*candidate.value_if());
                if (!post) candidate = common::Result<Value>::failure(*post.error_if());
                else recovered = true;
            }
            if (!candidate) {
                if (plan.failure_policy == FailurePolicy::continue_independent) {
                    result.failed_steps.push_back(step.id);
                    continue;
                }
                return common::Result<ReasoningResult>::failure(*candidate.error_if());
            }
            if (!valid_value(*candidate.value_if(), config_.max_value_bytes))
                return fail_result<ReasoningResult>(common::ErrorCode::resource_limit,
                    "step result exceeds IA-REAS value limit", "execute", &request);

            result.values.insert_or_assign(step.output, *candidate.value_if());
            result.completed_steps.push_back(step.id);
            if (recovered) result.recovered_steps.push_back(step.id);
            completed.insert(step.id);
            ++result.steps_executed;
        }
        if (request.is_cancelled())
            return fail_result<ReasoningResult>(common::ErrorCode::cancelled,
                "reasoning execution was cancelled after the final step",
                "execute", &request);
        return common::Result<ReasoningResult>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return fail_result<ReasoningResult>(common::ErrorCode::out_of_memory,
            "reasoning execution allocation failed", "execute", &request);
    }
}

core::Runtime::Initializer ReasoningSubsystem::initializer() {
    return [this](const core::RuntimeConfig& config) { return initialize(config); };
}

core::Runtime::Shutdown ReasoningSubsystem::shutdown_callback() {
    return [this] { (void)shutdown(); };
}

bool ReasoningSubsystem::initialized() const noexcept {
    std::lock_guard lock(mutex_);
    return initialized_;
}

const char* to_string(Operation operation) noexcept {
    switch (operation) {
    case Operation::assign: return "assign";
    case Operation::add: return "add";
    case Operation::subtract: return "subtract";
    case Operation::multiply: return "multiply";
    case Operation::divide: return "divide";
    case Operation::equal: return "equal";
    case Operation::less: return "less";
    case Operation::logical_and: return "logical_and";
    case Operation::logical_or: return "logical_or";
    case Operation::logical_not: return "logical_not";
    case Operation::concatenate: return "concatenate";
    }
    return "unknown";
}

const char* to_string(Comparator comparator) noexcept {
    switch (comparator) {
    case Comparator::equal: return "equal";
    case Comparator::not_equal: return "not_equal";
    case Comparator::less: return "less";
    case Comparator::less_equal: return "less_equal";
    case Comparator::greater: return "greater";
    case Comparator::greater_equal: return "greater_equal";
    case Comparator::truthy: return "truthy";
    }
    return "unknown";
}

} // namespace morok::reason
