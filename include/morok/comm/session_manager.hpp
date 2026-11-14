#pragma once

#include "morok/comm/conversation.hpp"
#include "morok/comm/event_stream.hpp"
#include "morok/core/cancellation.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/request_context.hpp"
#include "morok/core/session_registry.hpp"
#include "morok/core/runtime.hpp"
#include "morok/lang/language_subsystem.hpp"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace morok::comm {

class CommunicationSubsystem final {
public:
    CommunicationSubsystem(core::SessionRegistry& sessions,
                           lang::LanguageSubsystem& language,
                           ConversationLimits limits = {});

    [[nodiscard]] common::Status initialize(const core::RuntimeConfig& config);
    [[nodiscard]] common::Status shutdown() noexcept;
    [[nodiscard]] common::Result<common::ConversationId> create_conversation();
    [[nodiscard]] common::Result<core::RequestContext> create_request_context(
        const common::ConversationId& conversation_id,
        core::CancellationToken cancellation = {}) const;
    [[nodiscard]] common::Status append_control_message(
        const core::RequestContext& request,
        const common::ConversationId& conversation_id,
        MessageRole role, const common::Utf8Text& text,
        std::string tool_name = {});
    [[nodiscard]] common::Status close_conversation(
        const common::ConversationId& conversation_id);
    [[nodiscard]] common::Result<Message> send_message(
        const core::RequestContext& request,
        const common::ConversationId& conversation_id,
        const common::Utf8Text& text,
        std::size_t max_new_bytes,
        StreamCallback stream = {});
    [[nodiscard]] common::Result<std::vector<Message>> history(
        const common::ConversationId& conversation_id) const;
    [[nodiscard]] core::Runtime::Initializer initializer();
    [[nodiscard]] core::Runtime::Shutdown shutdown_callback();
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t active_conversations() const noexcept;

private:
    [[nodiscard]] common::Status validate_limits(
        const core::RuntimeConfig& config) const;
    [[nodiscard]] common::Result<Message> make_message(
        const Conversation& conversation,
        const core::RequestContext& request,
        MessageRole role,
        std::string content,
        MessageState state = MessageState::complete) const;
    [[nodiscard]] common::Status append_locked(
        Conversation& conversation, Message message);
    [[nodiscard]] common::Status emit(
        const StreamCallback& callback,
        StreamEventKind kind,
        const Conversation& conversation,
        const common::MessageId& message_id,
        std::string text,
        std::size_t sequence) const;

    core::SessionRegistry& sessions_;
    lang::LanguageSubsystem& language_;
    ConversationLimits limits_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Conversation> conversations_;
    std::unordered_map<std::string, bool> in_flight_;
    bool initialized_{false};
};

} // namespace morok::comm
