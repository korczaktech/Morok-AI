#pragma once

#include "morok/comm/message.hpp"

#include <cstddef>
#include <vector>

namespace morok::comm {

struct Conversation final {
    common::ConversationId id;
    common::SessionId session_id;
    common::Timestamp created_at;
    common::Timestamp updated_at;
    std::vector<Message> messages;
    bool closed{false};
};

struct ConversationLimits final {
    std::size_t max_conversations{128};
    std::size_t max_messages_per_conversation{256};
    std::size_t max_message_bytes{64U * 1024U};
    std::size_t max_history_bytes{256U * 1024U};
    std::size_t stream_chunk_bytes{64};
};

} // namespace morok::comm
