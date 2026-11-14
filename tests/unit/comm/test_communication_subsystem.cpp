#include "morok/common/utf8_text.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/runtime.hpp"
#include "morok/core/session_registry.hpp"
#include "morok/lang/language_subsystem.hpp"
#include "morok/comm/session_manager.hpp"
#include "morok/comm/formatter.hpp"
#include "morok/model/parameters/g1_parameters.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
    using namespace morok;

    model::G1Parameters parameters;
    model::initialize_g1_parameters(parameters, 2026);
    core::RuntimeConfig config;
    config.max_sessions = 2;
    config.resources.max_generation_bytes = 16;

    core::SessionRegistry sessions(2);
    lang::LanguageSubsystem language(parameters);
    comm::CommunicationSubsystem communication(sessions, language);
    core::Runtime runtime(config);
    assert(runtime.initialize(
        {language.initializer(), communication.initializer()},
        {language.shutdown_callback(), communication.shutdown_callback()}));
    assert(communication.initialized());

    auto conversation = communication.create_conversation();
    assert(conversation);
    assert(communication.active_conversations() == 1);
    auto request = communication.create_request_context(*conversation.value_if());
    assert(request);

    auto system_text = common::Utf8Text::from_utf8("Você é a Morok AI.");
    auto tool_text = common::Utf8Text::from_utf8("resultado controlado");
    auto user_text = common::Utf8Text::from_utf8("Olá, Morok!");
    assert(system_text && tool_text && user_text);
    assert(communication.append_control_message(
        *request.value_if(), *conversation.value_if(),
        comm::MessageRole::system, *system_text.value_if()));
    assert(communication.append_control_message(
        *request.value_if(), *conversation.value_if(),
        comm::MessageRole::tool, *tool_text.value_if(), "fixture"));

    std::vector<comm::StreamEventKind> events;
    std::string streamed;
    auto response = communication.send_message(
        *request.value_if(), *conversation.value_if(),
        *user_text.value_if(), 12,
        [&](const comm::StreamEvent& event) {
            events.push_back(event.kind);
            if (event.kind == comm::StreamEventKind::text_delta) {
                assert(common::Utf8Text::from_utf8(event.text));
                streamed += event.text;
            }
            return common::Status::success();
        });
    assert(response);
    assert(response.value_if()->role == comm::MessageRole::assistant);
    assert(response.value_if()->request_id == request.value_if()->request_id());
    assert(common::Utf8Text::from_utf8(streamed));
    assert(streamed == response.value_if()->content);
    assert(!events.empty());
    assert(events.front() == comm::StreamEventKind::message_started);
    assert(events.back() == comm::StreamEventKind::message_completed);

    auto history = communication.history(*conversation.value_if());
    assert(history);
    assert(history.value_if()->size() == 4);
    assert((*history.value_if())[0].id != (*history.value_if())[1].id);
    auto parsed_message_id = common::MessageId::from_string(
        (*history.value_if())[0].id.to_string());
    auto parsed_conversation_id = common::ConversationId::from_string(
        conversation.value_if()->to_string());
    assert(parsed_message_id && *parsed_message_id.value_if() == (*history.value_if())[0].id);
    assert(parsed_conversation_id && *parsed_conversation_id.value_if() == *conversation.value_if());
    assert((*history.value_if())[0].role == comm::MessageRole::system);
    assert((*history.value_if())[1].role == comm::MessageRole::tool);
    assert((*history.value_if())[1].tool_name == "fixture");
    assert((*history.value_if())[2].role == comm::MessageRole::user);
    assert((*history.value_if())[3].role == comm::MessageRole::assistant);
    auto transcript = comm::MessageFormatter::format_transcript(*history.value_if());
    assert(transcript);
    assert(transcript.value_if()->view().find("Tool[fixture]:") != std::string_view::npos);
    auto formatted_error = comm::MessageFormatter::format_error(common::Error::make(
        common::ErrorCode::cancelled, "request cancelled", "test", "format"));
    assert(formatted_error);
    assert(formatted_error.value_if()->view().find("Erro [") == 0);

    auto second_conversation = communication.create_conversation();
    assert(second_conversation);
    auto second_request = communication.create_request_context(*second_conversation.value_if());
    assert(second_request);
    auto cross_session = communication.send_message(
        *request.value_if(), *second_conversation.value_if(),
        *user_text.value_if(), 4);
    assert(!cross_session);
    assert(cross_session.error_if()->code == common::ErrorCode::invalid_argument);

    core::CancellationSource cancelled_source;
    auto cancelled_request = communication.create_request_context(
        *conversation.value_if(), cancelled_source.token());
    assert(cancelled_request);
    auto cancelled = communication.send_message(
        *cancelled_request.value_if(), *conversation.value_if(),
        *user_text.value_if(), 4,
        [&](const comm::StreamEvent& event) {
            if (event.kind == comm::StreamEventKind::message_started)
                assert(cancelled_source.cancel());
            return common::Status::success();
        });
    assert(!cancelled);
    assert(cancelled.error_if()->code == common::ErrorCode::cancelled);
    auto after_cancel = communication.history(*conversation.value_if());
    assert(after_cancel);
    assert(after_cancel.value_if()->size() == 6);
    assert(after_cancel.value_if()->back().role == comm::MessageRole::assistant);
    assert(after_cancel.value_if()->back().state == comm::MessageState::cancelled);

    auto invalid_role = communication.append_control_message(
        *request.value_if(), *conversation.value_if(),
        comm::MessageRole::assistant, *tool_text.value_if());
    assert(!invalid_role);
    assert(communication.close_conversation(*second_conversation.value_if()));

    auto third_conversation = communication.create_conversation();
    assert(third_conversation);
    auto third_request = communication.create_request_context(*third_conversation.value_if());
    assert(third_request);
    auto callback_failure = communication.send_message(
        *third_request.value_if(), *third_conversation.value_if(),
        *user_text.value_if(), 4,
        [](const comm::StreamEvent& event) {
            if (event.kind == comm::StreamEventKind::message_started)
                return common::Status::failure(common::Error::make(
                    common::ErrorCode::internal_error,
                    "test stream consumer failure", "test", "stream"));
            return common::Status::success();
        });
    assert(!callback_failure);
    assert(callback_failure.error_if()->code == common::ErrorCode::internal_error);
    auto failed_history = communication.history(*third_conversation.value_if());
    assert(failed_history);
    assert(failed_history.value_if()->back().state == comm::MessageState::failed);

    assert(communication.close_conversation(*third_conversation.value_if()));
    assert(communication.close_conversation(*conversation.value_if()));
    assert(communication.active_conversations() == 0);
    assert(sessions.size() == 0);
    assert(!communication.history(*conversation.value_if()));
    assert(runtime.shutdown());
    assert(!communication.initialized());
    assert(!language.initialized());
    return 0;
}
