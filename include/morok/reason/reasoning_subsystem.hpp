#pragma once

#include "morok/common/ids.hpp"
#include "morok/common/result.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/request_context.hpp"
#include "morok/core/runtime.hpp"

#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace morok::reason {

using Value = std::variant<double, bool, std::string>;

enum class Operation {
    assign,
    add,
    subtract,
    multiply,
    divide,
    equal,
    less,
    logical_and,
    logical_or,
    logical_not,
    concatenate
};

enum class Comparator {
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
    truthy
};

struct Condition final {
    std::string key;
    Comparator comparator{Comparator::equal};
    Value expected{false};
};

struct ReasoningStep final {
    std::string id;
    std::string output;
    Operation operation{Operation::assign};
    std::vector<std::string> inputs;
    std::vector<std::string> dependencies;
    Value literal{false};
    std::vector<Condition> preconditions;
    std::vector<Condition> postconditions;
    std::optional<Value> fallback;
};

struct ReasoningTask final {
    std::string objective;
    std::map<std::string, Value> initial_values;
    std::vector<Condition> preconditions;
    std::vector<ReasoningStep> steps;
    std::size_t step_budget{32};
};

struct ReasoningConfig final {
    std::size_t max_steps{32};
    std::size_t max_dependency_depth{8};
    std::size_t max_value_bytes{4096};
    std::size_t max_objective_bytes{512};
    std::size_t max_recovery_attempts{1};
};

struct ReasoningPlan final {
    common::SessionId session_id;
    common::RequestId request_id;
    std::string objective;
    std::map<std::string, Value> initial_values;
    std::vector<Condition> preconditions;
    std::vector<ReasoningStep> ordered_steps;
    std::size_t dependency_depth{0};
};

struct ReasoningResult final {
    std::map<std::string, Value> values;
    std::vector<std::string> completed_steps;
    std::vector<std::string> recovered_steps;
    std::size_t steps_executed{0};
};

class ReasoningSubsystem final {
public:
    explicit ReasoningSubsystem(ReasoningConfig config = {});

    [[nodiscard]] common::Status initialize(const core::RuntimeConfig& config);
    [[nodiscard]] common::Status shutdown() noexcept;

    [[nodiscard]] common::Result<ReasoningPlan> create_plan(
        const core::RequestContext& request,
        const ReasoningTask& task,
        const std::map<std::string, Value>& authorized_context = {}) const;

    [[nodiscard]] common::Result<ReasoningResult> execute(
        const core::RequestContext& request,
        const ReasoningPlan& plan) const;

    [[nodiscard]] core::Runtime::Initializer initializer();
    [[nodiscard]] core::Runtime::Shutdown shutdown_callback();
    [[nodiscard]] bool initialized() const noexcept;

private:
    [[nodiscard]] common::Status validate_config(
        const core::RuntimeConfig& config) const;

    ReasoningConfig config_;
    mutable std::mutex mutex_;
    bool initialized_{false};
};

[[nodiscard]] const char* to_string(Operation operation) noexcept;
[[nodiscard]] const char* to_string(Comparator comparator) noexcept;

} // namespace morok::reason
