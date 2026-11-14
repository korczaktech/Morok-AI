#include "morok/comm/session_manager.hpp"

#include "morok/comm/formatter.hpp"

#include <algorithm>
#include <cstdint>
#include <new>
#include <string_view>
#include <utility>

namespace morok::comm {
namespace {

common::Status failure(common::ErrorCode code, std::string message,
                       std::string stage) {
    return common::Status::failure(common::Error::make(
        code, std::move(message), "comm", std::move(stage)));
}

std::size_t utf8_chunk_end(std::string_view value, std::size_t begin,
                           std::size_t maximum) {
    std::size_t end = std::min(value.size(), begin + maximum);
    if (end == value.size()) return end;
    while (end > begin &&
           (static_cast<unsigned char>(value[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    if (end == begin) {
        const auto lead = static_cast<unsigned char>(value[begin]);
        const std::size_t width = lead < 0x80U ? 1U :
            (lead < 0xE0U ? 2U : (lead < 0xF0U ? 3U : 4U));
        end = std::min(value.size(), begin + width);
    }
    return end;
}

} // namespace

CommunicationSubsystem::CommunicationSubsystem(
    core::SessionRegistry& sessions, lang::LanguageSubsystem& language,
    ConversationLimits limits)
    : sessions_(sessions), language_(language), limits_(limits) {}

common::Status CommunicationSubsystem::validate_limits(
    const core::RuntimeConfig& config) const {
    if (limits_.max_conversations == 0 ||
        limits_.max_messages_per_conversation == 0 ||
        limits_.max_message_bytes == 0 ||
        limits_.max_history_bytes < limits_.max_message_bytes ||
        limits_.stream_chunk_bytes < 4 ||
        limits_.stream_chunk_bytes > 4096) {
        return failure(common::ErrorCode::resource_limit,
                       "IA-COMM limits are inconsistent or exceed runtime capacity",
                       "initialize");
    }
    return common::Status::success();
}

common::Status CommunicationSubsystem::initialize(
    const core::RuntimeConfig& config) {
    std::lock_guard lock(mutex_);
    if (initialized_)
        return failure(common::ErrorCode::invalid_argument,
                       "IA-COMM is already initialized", "initialize");
    const auto valid = config.validate();
    if (!valid) return valid;
    const auto limits_valid = validate_limits(config);
    if (!limits_valid) return limits_valid;
    if (!language_.initialized())
        return failure(common::ErrorCode::model_not_loaded,
                       "IA-LANG must be initialized before IA-COMM", "initialize");
    max_conversations_ = std::min(limits_.max_conversations, config.max_sessions);
    initialized_ = true;
    return common::Status::success();
}

common::Status CommunicationSubsystem::shutdown() noexcept {
    std::lock_guard lock(mutex_);
    if (!in_flight_.empty())
        return failure(common::ErrorCode::resource_limit,
                       "cannot shut down IA-COMM while requests are active", "shutdown");
    for (const auto& entry : conversations_) {
        if (sessions_.contains(entry.second.session_id)) {
            const auto closed = language_.close_session(sessions_, entry.second.session_id);
            if (!closed) return closed;
        }
    }
    conversations_.clear();
    initialized_ = false;
    return common::Status::success();
}

common::Result<common::ConversationId>
CommunicationSubsystem::create_conversation() {
    {
        std::lock_guard lock(mutex_);
        if (!initialized_)
            return common::Result<common::ConversationId>::failure(
                common::Error::make(common::ErrorCode::model_not_loaded,
                    "IA-COMM is not initialized", "comm", "conversation.create"));
        if (conversations_.size() >= max_conversations_)
            return common::Result<common::ConversationId>::failure(
                common::Error::make(common::ErrorCode::resource_limit,
                    "maximum active conversations reached", "comm", "conversation.create"));
    }

    auto session = language_.create_session(sessions_);
    if (!session)
        return common::Result<common::ConversationId>::failure(*session.error_if());
    auto generated = common::ConversationId::generate();
    if (!generated) {
        (void)language_.close_session(sessions_, *session.value_if());
        return common::Result<common::ConversationId>::failure(*generated.error_if());
    }

    const auto id = *generated.value_if();
    const auto now = common::Timestamp::now();
    Conversation conversation{id, *session.value_if(), now, now, {}, false};
    try {
        std::lock_guard lock(mutex_);
        if (!initialized_ || conversations_.size() >= max_conversations_) {
            (void)language_.close_session(sessions_, *session.value_if());
            return common::Result<common::ConversationId>::failure(
                common::Error::make(common::ErrorCode::resource_limit,
                    "conversation capacity changed during creation",
                    "comm", "conversation.create"));
        }
        const auto [position, inserted] =
            conversations_.emplace(id.to_string(), std::move(conversation));
        (void)position;
        if (!inserted) {
            (void)language_.close_session(sessions_, *session.value_if());
            return common::Result<common::ConversationId>::failure(
                common::Error::make(common::ErrorCode::internal_error,
                    "duplicate conversation identifier", "comm", "conversation.create"));
        }
    } catch (...) {
        (void)language_.close_session(sessions_, *session.value_if());
        return common::Result<common::ConversationId>::failure(
            common::Error::make(common::ErrorCode::out_of_memory,
                "could not allocate conversation", "comm", "conversation.create"));
    }
    return common::Result<common::ConversationId>::success(id);
}

common::Result<core::RequestContext>
CommunicationSubsystem::create_request_context(
    const common::ConversationId& conversation_id,
    core::CancellationToken cancellation) const {
    std::lock_guard lock(mutex_);
    const auto found = conversations_.find(conversation_id.to_string());
    if (!initialized_ || found == conversations_.end() || found->second.closed)
        return common::Result<core::RequestContext>::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "conversation is unavailable", "comm", "request.create"));
    return core::RequestContext::create(found->second.session_id,
                                        std::move(cancellation));
}

common::Status CommunicationSubsystem::append_control_message(
    const core::RequestContext& request,
    const common::ConversationId& conversation_id,
    MessageRole role, const common::Utf8Text& text, std::string tool_name) {
    if (role != MessageRole::system && role != MessageRole::tool)
        return failure(common::ErrorCode::invalid_argument,
                       "control messages must use system or tool role",
                       "message.append");
    if ((role == MessageRole::tool && tool_name.empty()) ||
        (role == MessageRole::system && !tool_name.empty()))
        return failure(common::ErrorCode::invalid_argument,
                       "tool messages require a tool name; system messages must not have one",
                       "message.append");
    if (tool_name.size() > 128U || text.size_bytes() > limits_.max_message_bytes)
        return failure(common::ErrorCode::input_too_large,
                       "control message metadata or content exceeds its limit",
                       "message.append");
    if (request.is_cancelled())
        return failure(common::ErrorCode::cancelled,
                       "request was cancelled", "message.append");
    std::lock_guard lock(mutex_);
    const auto found = conversations_.find(conversation_id.to_string());
    if (!initialized_ || found == conversations_.end() ||
        found->second.session_id != request.session_id() ||
        found->second.closed || in_flight_.contains(conversation_id.to_string()))
        return failure(common::ErrorCode::invalid_argument,
                       "control message refers to an unavailable conversation",
                       "message.append");
    auto message = make_message(found->second, request, role, text.string());
    if (!message) return common::Status::failure(*message.error_if());
    message.value_if()->tool_name = std::move(tool_name);
    try {
        auto model_text = MessageFormatter::format_transcript(
            std::vector<Message>{*message.value_if()});
        if (!model_text) return common::Status::failure(*model_text.error_if());
        Conversation staged = found->second;
        const auto capacity = append_locked(staged, std::move(*message.value_if()));
        if (!capacity) return capacity;
        const auto observed = language_.observe(request, *model_text.value_if());
        if (!observed) return observed;
        found->second = std::move(staged);
    } catch (const std::bad_alloc&) {
        return failure(common::ErrorCode::out_of_memory,
                       "could not stage control message", "message.append");
    }
    return common::Status::success();
}

common::Status CommunicationSubsystem::close_conversation(
    const common::ConversationId& conversation_id) {
    std::lock_guard lock(mutex_);
    const auto key = conversation_id.to_string();
    const auto found = conversations_.find(key);
    if (found == conversations_.end())
        return failure(common::ErrorCode::invalid_argument,
                       "conversation does not exist", "conversation.close");
    if (in_flight_.contains(key))
        return failure(common::ErrorCode::resource_limit,
                       "conversation has an active request", "conversation.close");
    const auto closed = language_.close_session(sessions_, found->second.session_id);
    if (!closed) return closed;
    conversations_.erase(found);
    return common::Status::success();
}

common::Result<Message> CommunicationSubsystem::make_message(
    const Conversation& conversation, const core::RequestContext& request,
    MessageRole role, std::string content, MessageState state) const {
    auto id = common::MessageId::generate();
    if (!id) return common::Result<Message>::failure(*id.error_if());
    return common::Result<Message>::success(Message{
        *id.value_if(), conversation.id, conversation.session_id,
        request.request_id(), role, std::move(content),
        common::Timestamp::now(), state, {}});
}

common::Status CommunicationSubsystem::append_locked(
    Conversation& conversation, Message message) {
    if (message.content.size() > limits_.max_message_bytes)
        return failure(common::ErrorCode::input_too_large,
                       "message exceeds IA-COMM byte limit", "message.append");
    if (conversation.messages.size() >= limits_.max_messages_per_conversation)
        return failure(common::ErrorCode::resource_limit,
                       "conversation message count limit reached", "message.append");
    std::size_t total = message.content.size();
    for (const auto& existing : conversation.messages) {
        if (existing.content.size() > limits_.max_history_bytes -
            std::min(total, limits_.max_history_bytes))
            return failure(common::ErrorCode::resource_limit,
                           "conversation history byte limit reached", "message.append");
        total += existing.content.size();
    }
    if (total > limits_.max_history_bytes)
        return failure(common::ErrorCode::resource_limit,
                       "conversation history byte limit reached", "message.append");
    try {
        conversation.messages.push_back(std::move(message));
        conversation.updated_at = common::Timestamp::now();
    } catch (...) {
        return failure(common::ErrorCode::out_of_memory,
                       "could not append conversation message", "message.append");
    }
    return common::Status::success();
}

common::Status CommunicationSubsystem::emit(
    const StreamCallback& callback, StreamEventKind kind,
    const Conversation& conversation, const common::MessageId& message_id,
    std::string text, std::size_t sequence) const {
    if (!callback) return common::Status::success();
    try {
        return callback(StreamEvent{kind, conversation.id, message_id,
                                    std::move(text), sequence});
    } catch (...) {
        return failure(common::ErrorCode::internal_error,
                       "stream callback threw an exception", "stream");
    }
}

common::Result<Message> CommunicationSubsystem::send_message(
    const core::RequestContext& request,
    const common::ConversationId& conversation_id,
    const common::Utf8Text& text, std::size_t max_new_bytes,
    StreamCallback stream) {
    const auto key = conversation_id.to_string();
    Conversation snapshot = [&]() {
        std::lock_guard lock(mutex_);
        const auto found = conversations_.find(key);
        if (!initialized_ || found == conversations_.end() ||
            found->second.session_id != request.session_id() ||
            found->second.closed || in_flight_.contains(key))
            return Conversation{conversation_id, request.session_id(),
                common::Timestamp::now(), common::Timestamp::now(), {}, true};
        in_flight_.emplace(key, true);
        return found->second;
    }();
    const auto release = [this, &key]() {
        std::lock_guard lock(mutex_);
        in_flight_.erase(key);
    };
    if (snapshot.closed) {
        return common::Result<Message>::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "conversation is unavailable, mismatched, or already processing a request",
            "comm", "message.send"));
    }
    if (request.is_cancelled()) {
        release();
        return common::Result<Message>::failure(common::Error::make(
            common::ErrorCode::cancelled, "request was cancelled",
            "comm", "message.send"));
    }
    if (text.size_bytes() > limits_.max_message_bytes) {
        release();
        return common::Result<Message>::failure(common::Error::make(
            common::ErrorCode::input_too_large, "user message exceeds IA-COMM limit",
            "comm", "message.send"));
    }

    auto user = make_message(snapshot, request, MessageRole::user, text.string());
    if (!user) { release(); return user; }
    Conversation staged_user = snapshot;
    const auto user_capacity = append_locked(staged_user, *user.value_if());
    if (!user_capacity) { release(); return common::Result<Message>::failure(*user_capacity.error_if()); }
    auto model_text = MessageFormatter::format_transcript(
        std::vector<Message>{*user.value_if()});
    if (!model_text) { release(); return common::Result<Message>::failure(*model_text.error_if()); }
    const auto observed = language_.observe(request, *model_text.value_if());
    if (!observed) { release(); return common::Result<Message>::failure(*observed.error_if()); }
    {
        std::lock_guard lock(mutex_);
        auto found = conversations_.find(key);
        if (found == conversations_.end()) { in_flight_.erase(key); return common::Result<Message>::failure(
            common::Error::make(common::ErrorCode::invalid_argument,
                "conversation was closed", "comm", "message.send")); }
        found->second = std::move(staged_user);
    }
    auto assistant_prefix = common::Utf8Text::from_utf8("Assistant: ");
    if (!assistant_prefix) { release(); return common::Result<Message>::failure(*assistant_prefix.error_if()); }
    const auto prefix_observed = language_.observe(request, *assistant_prefix.value_if());
    if (!prefix_observed) { release(); return common::Result<Message>::failure(*prefix_observed.error_if()); }
    auto generated = language_.generate(request, max_new_bytes);
    if (!generated) {
        if (stream) {
            auto formatted = MessageFormatter::format_error(*generated.error_if());
            if (formatted) {
                (void)emit(stream, StreamEventKind::error, snapshot,
                    user.value_if()->id, formatted.value_if()->string(), 0);
            }
        }
        release();
        return common::Result<Message>::failure(*generated.error_if());
    }

    auto assistant = make_message(snapshot, request, MessageRole::assistant,
        generated.value_if()->text.string(), MessageState::streaming);
    if (!assistant) { release(); return assistant; }
    {
        std::lock_guard lock(mutex_);
        auto found = conversations_.find(key);
        if (found == conversations_.end()) { in_flight_.erase(key); return common::Result<Message>::failure(
            common::Error::make(common::ErrorCode::invalid_argument,
                "conversation closed before response commit", "comm", "message.send")); }
        const auto appended = append_locked(found->second, *assistant.value_if());
        if (!appended) { in_flight_.erase(key); return common::Result<Message>::failure(*appended.error_if()); }
    }
    const auto update_state = [this, &key, &assistant](MessageState state) {
        std::lock_guard lock(mutex_);
        auto found = conversations_.find(key);
        if (found != conversations_.end()) {
            for (auto it = found->second.messages.rbegin();
                 it != found->second.messages.rend(); ++it) {
                if (it->id == assistant.value_if()->id) {
                    it->state = state;
                    break;
                }
            }
        }
        assistant.value_if()->state = state;
    };
    auto status = emit(stream, StreamEventKind::message_started,
        snapshot, assistant.value_if()->id, {}, 0);
    if (!status) {
        update_state(MessageState::failed);
        release();
        return common::Result<Message>::failure(*status.error_if());
    }
    if (request.is_cancelled()) {
        (void)emit(stream, StreamEventKind::message_cancelled,
            snapshot, assistant.value_if()->id, {}, 0);
        update_state(MessageState::cancelled);
        release();
        return common::Result<Message>::failure(common::Error::make(
            common::ErrorCode::cancelled,
            "request was cancelled while starting response delivery",
            "comm", "message.stream"));
    }

    const auto output = generated.value_if()->text.view();
    std::size_t offset = 0;
    std::size_t sequence = 0;
    while (offset < output.size()) {
        if (request.is_cancelled()) {
            (void)emit(stream, StreamEventKind::message_cancelled,
                snapshot, assistant.value_if()->id, {}, sequence);
            update_state(MessageState::cancelled);
            release();
            return common::Result<Message>::failure(common::Error::make(
                common::ErrorCode::cancelled,
                "request was cancelled while delivering the response",
                "comm", "message.stream"));
        }
        const auto end = utf8_chunk_end(output, offset, limits_.stream_chunk_bytes);
        status = emit(stream, StreamEventKind::text_delta, snapshot,
            assistant.value_if()->id, std::string(output.substr(offset, end - offset)),
            sequence++);
        if (!status) {
            update_state(MessageState::failed);
            release();
            return common::Result<Message>::failure(*status.error_if());
        }
        offset = end;
        if (request.is_cancelled()) {
            (void)emit(stream, StreamEventKind::message_cancelled,
                snapshot, assistant.value_if()->id, {}, sequence);
            update_state(MessageState::cancelled);
            release();
            return common::Result<Message>::failure(common::Error::make(
                common::ErrorCode::cancelled,
                "request was cancelled while delivering the response",
                "comm", "message.stream"));
        }
    }
    status = emit(stream, StreamEventKind::message_completed,
        snapshot, assistant.value_if()->id, {}, sequence);
    if (!status) {
        update_state(MessageState::failed);
        release();
        return common::Result<Message>::failure(*status.error_if());
    }
    update_state(MessageState::complete);
    release();
    return assistant;
}

common::Result<std::vector<Message>> CommunicationSubsystem::history(
    const common::ConversationId& conversation_id) const {
    std::lock_guard lock(mutex_);
    if (!initialized_)
        return common::Result<std::vector<Message>>::failure(common::Error::make(
            common::ErrorCode::model_not_loaded,
            "IA-COMM is not initialized", "comm", "history"));
    const auto found = conversations_.find(conversation_id.to_string());
    if (found == conversations_.end())
        return common::Result<std::vector<Message>>::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "conversation does not exist", "comm", "history"));
    return common::Result<std::vector<Message>>::success(found->second.messages);
}

core::Runtime::Initializer CommunicationSubsystem::initializer() {
    return [this](const core::RuntimeConfig& config) { return initialize(config); };
}

core::Runtime::Shutdown CommunicationSubsystem::shutdown_callback() {
    return [this] { (void)shutdown(); };
}

bool CommunicationSubsystem::initialized() const noexcept {
    std::lock_guard lock(mutex_);
    return initialized_;
}

std::size_t CommunicationSubsystem::active_conversations() const noexcept {
    std::lock_guard lock(mutex_);
    return conversations_.size();
}

} // namespace morok::comm
