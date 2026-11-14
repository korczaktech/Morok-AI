#include "morok/reason/reasoning_subsystem.hpp"

#include "morok/core/cancellation.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <string>
#include <variant>

namespace {

using morok::common::ErrorCode;
using morok::core::CancellationSource;
using morok::core::RequestContext;
using morok::core::Runtime;
using morok::core::RuntimeConfig;
using morok::reason::Comparator;
using morok::reason::Condition;
using morok::reason::Operation;
using morok::reason::ReasoningConfig;
using morok::reason::ReasoningStep;
using morok::reason::ReasoningSubsystem;
using morok::reason::ReasoningTask;
using morok::reason::Value;

RequestContext make_request(const morok::common::SessionId& session,
                            morok::core::CancellationToken token = {}) {
    auto request = RequestContext::create(session, std::move(token));
    assert(request);
    return std::move(*request.value_if());
}

ReasoningStep step(std::string id, std::string output, Operation operation,
                   std::vector<std::string> inputs = {}) {
    ReasoningStep result;
    result.id = std::move(id);
    result.output = std::move(output);
    result.operation = operation;
    result.inputs = std::move(inputs);
    return result;
}

} // namespace

int main() {
    auto session = morok::common::SessionId::generate();
    assert(session);
    auto request = make_request(*session.value_if());

    ReasoningSubsystem reasoning;
    Runtime runtime(RuntimeConfig{});
    assert(runtime.initialize({reasoning.initializer()},
                              {reasoning.shutdown_callback()}));
    assert(reasoning.initialized());

    ReasoningTask task;
    task.objective = "calcular e validar resultado";
    task.preconditions.push_back(Condition{"a", Comparator::greater, Value{0.0}});
    task.steps.push_back(step("sum", "sum", Operation::add, {"a", "b"}));
    task.steps.back().preconditions.push_back(
        Condition{"a", Comparator::greater, Value{0.0}});
    task.steps.back().postconditions.push_back(
        Condition{"sum", Comparator::greater, Value{0.0}});
    task.steps.push_back(step("double", "double", Operation::multiply,
                              {"sum", "a"}));
    task.steps.push_back(step("compare", "is_larger", Operation::less,
                              {"sum", "double"}));
    task.steps.push_back(step("join", "joined", Operation::concatenate,
                              {"left", "right"}));
    task.initial_values.emplace("left", Value{std::string("morok ")});
    task.initial_values.emplace("right", Value{std::string("ai")});
    const std::map<std::string, Value> context{
        {"a", Value{2.0}}, {"b", Value{3.0}}};

    auto plan = reasoning.create_plan(request, task, context);
    assert(plan);
    assert(plan.value_if()->ordered_steps.size() == 4);
    assert(plan.value_if()->dependency_depth == 3);
    auto result = reasoning.execute(request, *plan.value_if());
    assert(result);
    assert(result.value_if()->steps_executed == 4);
    assert(std::get<double>(result.value_if()->values.at("sum")) == 5.0);
    assert(std::get<double>(result.value_if()->values.at("double")) == 10.0);
    assert(std::get<bool>(result.value_if()->values.at("is_larger")));
    assert(std::get<std::string>(result.value_if()->values.at("joined")) == "morok ai");

    ReasoningTask fallback_task;
    fallback_task.objective = "recuperar divisão inválida";
    fallback_task.initial_values.emplace("numerator", Value{8.0});
    fallback_task.initial_values.emplace("zero", Value{0.0});
    auto division = step("division", "quotient", Operation::divide,
                         {"numerator", "zero"});
    division.fallback = Value{0.0};
    division.postconditions.push_back(
        Condition{"quotient", Comparator::equal, Value{0.0}});
    fallback_task.steps.push_back(std::move(division));
    auto fallback_plan = reasoning.create_plan(request, fallback_task);
    assert(fallback_plan);
    auto fallback_result = reasoning.execute(request, *fallback_plan.value_if());
    assert(fallback_result);
    assert(fallback_result.value_if()->recovered_steps.size() == 1);
    assert(fallback_result.value_if()->recovered_steps.front() == "division");
    assert(std::get<double>(fallback_result.value_if()->values.at("quotient")) == 0.0);

    ReasoningTask failed_task;
    failed_task.objective = "falhar sem fallback";
    failed_task.initial_values.emplace("one", Value{1.0});
    failed_task.initial_values.emplace("zero", Value{0.0});
    failed_task.steps.push_back(step("division", "quotient", Operation::divide,
                                     {"one", "zero"}));
    auto failed_plan = reasoning.create_plan(request, failed_task);
    assert(failed_plan);
    auto failed_result = reasoning.execute(request, *failed_plan.value_if());
    assert(!failed_result);
    assert(failed_result.error_if()->code == ErrorCode::invalid_argument);

    ReasoningTask operators_task;
    operators_task.objective = "validar operadores matemáticos e lógicos";
    operators_task.initial_values.emplace("ten", Value{10.0});
    operators_task.initial_values.emplace("four", Value{4.0});
    operators_task.initial_values.emplace("eight", Value{8.0});
    operators_task.initial_values.emplace("two", Value{2.0});
    operators_task.initial_values.emplace("yes", Value{true});
    operators_task.initial_values.emplace("no", Value{false});
    operators_task.steps.push_back(step("subtract", "difference", Operation::subtract,
                                         {"ten", "four"}));
    operators_task.steps.push_back(step("divide", "ratio", Operation::divide,
                                         {"eight", "two"}));
    operators_task.steps.push_back(step("equal", "same", Operation::equal,
                                         {"difference", "ratio"}));
    operators_task.steps.push_back(step("and", "both", Operation::logical_and,
                                         {"yes", "no"}));
    operators_task.steps.push_back(step("or", "either", Operation::logical_or,
                                         {"yes", "no"}));
    operators_task.steps.push_back(step("not", "negated", Operation::logical_not,
                                         {"no"}));
    auto operators_plan = reasoning.create_plan(request, operators_task);
    assert(operators_plan);
    auto operators_result = reasoning.execute(request, *operators_plan.value_if());
    assert(operators_result);
    assert(std::get<double>(operators_result.value_if()->values.at("difference")) == 6.0);
    assert(std::get<double>(operators_result.value_if()->values.at("ratio")) == 4.0);
    assert(std::get<bool>(operators_result.value_if()->values.at("same")) == false);
    assert(std::get<bool>(operators_result.value_if()->values.at("both")) == false);
    assert(std::get<bool>(operators_result.value_if()->values.at("either")) == true);
    assert(std::get<bool>(operators_result.value_if()->values.at("negated")) == true);

    ReasoningTask continue_task;
    continue_task.objective = "continuar somente etapas independentes";
    continue_task.failure_policy = morok::reason::FailurePolicy::continue_independent;
    continue_task.initial_values.emplace("numerator", Value{8.0});
    continue_task.initial_values.emplace("zero", Value{0.0});
    continue_task.steps.push_back(step("broken", "quotient", Operation::divide,
                                       {"numerator", "zero"}));
    auto independent = step("independent", "ready", Operation::assign);
    independent.literal = Value{true};
    continue_task.steps.push_back(std::move(independent));
    continue_task.steps.push_back(step("dependent", "next", Operation::logical_not,
                                       {"ready"}));
    continue_task.steps.back().dependencies.push_back("broken");
    auto continue_plan = reasoning.create_plan(request, continue_task);
    assert(continue_plan);
    auto continue_result = reasoning.execute(request, *continue_plan.value_if());
    assert(continue_result);
    assert(continue_result.value_if()->failed_steps.size() == 1);
    assert(continue_result.value_if()->failed_steps.front() == "broken");
    assert(continue_result.value_if()->skipped_steps.size() == 1);
    assert(continue_result.value_if()->skipped_steps.front() == "dependent");
    assert(continue_result.value_if()->completed_steps.size() == 1);
    assert(continue_result.value_if()->completed_steps.front() == "independent");

    ReasoningTask cycle_task;
    cycle_task.objective = "detectar ciclo";
    cycle_task.steps.push_back(step("first", "first_value", Operation::logical_not,
                                     {"second_value"}));
    cycle_task.steps.push_back(step("second", "second_value", Operation::logical_not,
                                     {"first_value"}));
    auto cycle = reasoning.create_plan(request, cycle_task);
    assert(!cycle);
    assert(cycle.error_if()->code == ErrorCode::invalid_argument);

    ReasoningTask missing_task;
    missing_task.objective = "detectar entrada inexistente";
    missing_task.steps.push_back(step("missing", "result", Operation::logical_not,
                                      {"not_defined"}));
    auto missing = reasoning.create_plan(request, missing_task);
    assert(!missing);
    assert(missing.error_if()->code == ErrorCode::invalid_argument);

    ReasoningTask deep_task;
    deep_task.objective = "detectar profundidade acima do limite";
    deep_task.initial_values.emplace("v0", Value{1.0});
    for (int i = 1; i <= 9; ++i) {
        deep_task.steps.push_back(step("s" + std::to_string(i),
            "v" + std::to_string(i), Operation::add,
            {"v" + std::to_string(i - 1), "v0"}));
    }
    auto too_deep = reasoning.create_plan(request, deep_task);
    assert(!too_deep);
    assert(too_deep.error_if()->code == ErrorCode::resource_limit);

    ReasoningTask over_budget;
    over_budget.objective = "detectar orçamento excedido";
    over_budget.step_budget = 1;
    over_budget.steps.push_back(step("one", "one", Operation::assign));
    over_budget.steps.push_back(step("two", "two", Operation::assign));
    auto budget = reasoning.create_plan(request, over_budget);
    assert(!budget);
    assert(budget.error_if()->code == ErrorCode::resource_limit);

    ReasoningTask bad_precondition;
    bad_precondition.objective = "pré-condição falsa";
    bad_precondition.initial_values.emplace("ready", Value{false});
    bad_precondition.preconditions.push_back(
        Condition{"ready", Comparator::truthy, Value{false}});
    bad_precondition.steps.push_back(step("set", "done", Operation::assign));
    auto precondition_plan = reasoning.create_plan(request, bad_precondition);
    assert(precondition_plan);
    auto precondition_result = reasoning.execute(request, *precondition_plan.value_if());
    assert(!precondition_result);
    assert(precondition_result.error_if()->code == ErrorCode::invalid_argument);

    ReasoningTask invalid_postcondition;
    invalid_postcondition.objective = "pós-condição inválida";
    invalid_postcondition.steps.push_back(step("set", "answer", Operation::assign));
    invalid_postcondition.steps.back().literal = Value{true};
    invalid_postcondition.steps.back().postconditions.push_back(
        Condition{"answer", Comparator::truthy, Value{false}});
    auto post_plan = reasoning.create_plan(request, invalid_postcondition);
    assert(post_plan);
    auto post_result = reasoning.execute(request, *post_plan.value_if());
    assert(!post_result);
    assert(post_result.error_if()->code == ErrorCode::invalid_argument);

    auto other_request = make_request(*session.value_if());
    auto cross_request = reasoning.execute(other_request, *plan.value_if());
    assert(!cross_request);
    assert(cross_request.error_if()->code == ErrorCode::permission_denied);

    CancellationSource cancellation;
    assert(cancellation.cancel());
    auto cancelled_request = make_request(*session.value_if(), cancellation.token());
    auto cancelled_plan = reasoning.create_plan(cancelled_request, task, context);
    assert(!cancelled_plan);
    assert(cancelled_plan.error_if()->code == ErrorCode::cancelled);

    assert(runtime.shutdown());
    assert(!reasoning.initialized());
    auto after_shutdown = reasoning.create_plan(request, task, context);
    assert(!after_shutdown);
    assert(after_shutdown.error_if()->code == ErrorCode::model_not_loaded);
    return 0;
}
