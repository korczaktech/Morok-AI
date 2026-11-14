#pragma once

#include "morok/common/ids.hpp"
#include "morok/common/result.hpp"

#include <cstddef>
#include <functional>
#include <string>

namespace morok::comm {

enum class StreamEventKind { message_started, text_delta, message_completed, message_cancelled, error };

struct StreamEvent final {
    StreamEventKind kind{StreamEventKind::text_delta};
    common::ConversationId conversation_id;
    common::MessageId message_id;
    std::string text;
    std::size_t sequence{0};
};

using StreamCallback = std::function<common::Status(const StreamEvent&)>;

} // namespace morok::comm
