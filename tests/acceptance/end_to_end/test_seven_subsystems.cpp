#include "morok/comm/session_manager.hpp"
#include "morok/core/runtime.hpp"
#include "morok/core/session_registry.hpp"
#include "morok/lang/language_subsystem.hpp"
#include "morok/learn/learning_subsystem.hpp"
#include "morok/memory/memory_subsystem.hpp"
#include "morok/model/parameters/g1_parameters.hpp"
#include "morok/reason/reasoning_subsystem.hpp"
#include "morok/security/security_subsystem.hpp"
#include "morok/tools/tool_subsystem.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <variant>

int main() {
    namespace fs = std::filesystem;
    using namespace morok;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto database = fs::temp_directory_path() /
        ("morok-stage19-memory-" + std::to_string(nonce) + ".sqlite");

    core::RuntimeConfig config;
    config.runtime_name = "morok-stage19-acceptance";
    config.max_sessions = 2;
    core::SessionRegistry sessions(config.max_sessions);
    model::G1Parameters parameters{};
    model::initialize_g1_parameters(parameters, 19);
    memory::MemorySubsystem memory(database.string());
    lang::LanguageSubsystem language(parameters);
    reason::ReasoningSubsystem reasoning;
    tools::ToolSubsystem tools(
        {}, [](const tools::ToolAuditRecord&) { return common::Status::success(); });
    security::SecuritySubsystem security(
        {}, [](const security::SecurityAuditRecord&) { return common::Status::success(); });
    comm::CommunicationSubsystem communication(sessions, language);
    learn::LearningSubsystem learning;
    core::Runtime runtime(config);

    assert(runtime.initialize(
        {memory.initializer(), language.initializer(), reasoning.initializer(),
         tools.initializer(), security.initializer(), communication.initializer(),
         learning.initializer()},
        {memory.shutdown_callback(), language.shutdown_callback(),
         reasoning.shutdown_callback(), tools.shutdown_callback(),
         security.shutdown_callback(), communication.shutdown_callback(),
         learning.shutdown_callback()}));
    assert(runtime.accepts_requests());
    assert(memory.initialized() && language.initialized() && reasoning.initialized());
    assert(tools.initialized() && security.initialized() && communication.initialized());
    assert(learning.initialized());

    auto conversation = communication.create_conversation();
    assert(conversation);
    auto request = communication.create_request_context(*conversation.value_if());
    assert(request);
    const auto session = request.value_if()->session_id();
    const security::SecurityPrincipal principal{"acceptance", session};
    assert(security.grant_tool(principal, "acceptance_echo",
                               tools::ToolCapability::read_only));
    auto policy = security.tool_policy(principal);
    assert(policy && policy.value_if()->exact_grants.size() == 1);

    auto title = common::Utf8Text::from_utf8("Acceptance memory");
    auto body = common::Utf8Text::from_utf8("Seven subsystems are active.");
    auto query = common::Utf8Text::from_utf8("subsystems");
    assert(title && body && query);
    memory::MemoryInput memory_input{
        session, memory::MemoryKind::episodic, *title.value_if(), *body.value_if(),
        memory::MemorySource::system, "acceptance:test",
        memory::Sensitivity::normal, std::nullopt};
    auto memory_id = memory.store().store(memory_input, request.value_if()->request_id());
    assert(memory_id);
    auto memory_record = memory.store().retrieve(session, *memory_id.value_if());
    assert(memory_record && memory_record.value_if()->body.view() == body.value_if()->view());
    auto memory_results = memory.store().search(
        memory::SearchQuery{session, *query.value_if(), 5, 1024, std::nullopt});
    assert(memory_results && !memory_results.value_if()->empty());

    reason::ReasoningTask task;
    task.objective = "acceptance deterministic calculation";
    task.initial_values.emplace("left", reason::Value{2.0});
    task.initial_values.emplace("right", reason::Value{3.0});
    reason::ReasoningStep sum;
    sum.id = "sum";
    sum.output = "result";
    sum.operation = reason::Operation::add;
    sum.inputs = {"left", "right"};
    task.steps.push_back(sum);
    auto plan = reasoning.create_plan(*request.value_if(), task);
    assert(plan);
    auto reasoning_result = reasoning.execute(*request.value_if(), *plan.value_if());
    assert(reasoning_result);
    assert(std::get<double>(reasoning_result.value_if()->values.at("result")) == 5.0);

    tools::ToolDefinition echo;
    echo.name = "acceptance_echo";
    echo.description = "Acceptance-only echo handler";
    echo.schema.fields.push_back(
        tools::ToolField{"text", tools::ToolValueType::string, true, 64});
    echo.capability = tools::ToolCapability::read_only;
    echo.handler = [](const tools::ToolExecutionContext&,
                      const tools::ToolArguments& args) {
        return common::Result<std::string>::success(
            std::get<std::string>(args.at("text")));
    };
    assert(tools.register_tool(std::move(echo)));
    auto tool_result = security.invoke_tool(
        tools, *request.value_if(), principal, "acceptance_echo",
        {{"text", std::string("tool-ok")}});
    assert(tool_result && *tool_result.value_if() == "tool-ok");

    auto prompt = common::Utf8Text::from_utf8("Acceptance request");
    assert(prompt);
    auto response = communication.send_message(
        *request.value_if(), *conversation.value_if(), *prompt.value_if(), 12);
    assert(response && response.value_if()->role == comm::MessageRole::assistant);
    assert(response.value_if()->content.size() <= 12);
    auto history = communication.history(*conversation.value_if());
    assert(history && history.value_if()->size() >= 2);

    auto cleared_working = memory.store().clear_working(session);
    assert(cleared_working);
    assert(security.close_session(principal));
    assert(communication.close_conversation(*conversation.value_if()));
    assert(sessions.size() == 0);
    assert(communication.active_conversations() == 0);
    assert(runtime.shutdown());
    assert(runtime.state() == core::LifecycleState::stopped);
    assert(!memory.initialized() && !language.initialized() && !reasoning.initialized());
    assert(!tools.initialized() && !security.initialized() && !communication.initialized());
    assert(!learning.initialized());

    std::error_code ignored;
    fs::remove(database, ignored);
    fs::remove(database.string() + "-wal", ignored);
    fs::remove(database.string() + "-shm", ignored);
    return 0;
}
