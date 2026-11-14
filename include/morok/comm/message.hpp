#pragma once

#include "morok/common/ids.hpp"
#include "morok/common/timestamp.hpp"

#include <string>

namespace morok::comm {

enum class MessageRole { system, user, assistant, tool };
enum class MessageState { complete, streaming, cancelled, failed };

struct Message final {
    common::MessageId id;
    common::ConversationId conversation_id;
    common::SessionId session_id;
    common::RequestId request_id;
    MessageRole role{MessageRole::user};
    std::string content;
    common::Timestamp created_at;
    MessageState state{MessageState::complete};
    std::string tool_name;
};

} // namespace morok::comm
