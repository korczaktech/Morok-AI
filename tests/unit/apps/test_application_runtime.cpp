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

int main() {
    namespace fs = std::filesystem;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto database = fs::temp_directory_path() /
        ("morok-stage14-" + std::to_string(unique) + ".sqlite");

    morok::core::RuntimeConfig config;
    config.runtime_name = "stage14-integration-test";
    config.max_sessions = 2;
    morok::core::SessionRegistry sessions(config.max_sessions);
    morok::model::G1Parameters parameters{};
    morok::memory::MemorySubsystem memory(database.string());
    morok::lang::LanguageSubsystem language(parameters);
    morok::reason::ReasoningSubsystem reasoning;
    morok::tools::ToolSubsystem tools(
        {}, [](const morok::tools::ToolAuditRecord&) {
            return morok::common::Status::success();
        });
    morok::security::SecuritySubsystem security(
        {}, [](const morok::security::SecurityAuditRecord&) {
            return morok::common::Status::success();
        });
    morok::comm::CommunicationSubsystem communication(sessions, language);
    morok::learn::LearningSubsystem learning;
    morok::core::Runtime runtime(config);

    const auto initialized = runtime.initialize(
        {memory.initializer(), language.initializer(), reasoning.initializer(),
         tools.initializer(), security.initializer(), communication.initializer(),
         learning.initializer()},
        {memory.shutdown_callback(), language.shutdown_callback(),
         reasoning.shutdown_callback(), tools.shutdown_callback(),
         security.shutdown_callback(), communication.shutdown_callback(),
         learning.shutdown_callback()});
    assert(initialized);
    assert(runtime.accepts_requests());

    auto conversation = communication.create_conversation();
    assert(conversation);
    auto request = communication.create_request_context(*conversation.value_if());
    assert(request);
    auto prompt = morok::common::Utf8Text::from_utf8("test");
    assert(prompt);
    auto response = communication.send_message(
        *request.value_if(), *conversation.value_if(), *prompt.value_if(), 4);
    assert(response);
    assert(response.value_if()->role == morok::comm::MessageRole::assistant);
    assert(response.value_if()->content.size() <= 4);
    assert(communication.close_conversation(*conversation.value_if()));
    assert(sessions.size() == 0);

    assert(runtime.shutdown());
    assert(runtime.state() == morok::core::LifecycleState::stopped);
    std::error_code ignored;
    fs::remove(database, ignored);
    fs::remove(database.string() + "-wal", ignored);
    fs::remove(database.string() + "-shm", ignored);
}
