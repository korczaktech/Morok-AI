#pragma once

#include "morok/common/error.hpp"
#include "morok/common/result.hpp"
#include "morok/common/utf8_text.hpp"
#include "morok/comm/message.hpp"

#include <string>
#include <vector>

namespace morok::comm {

class MessageFormatter final {
public:
    [[nodiscard]] static common::Result<common::Utf8Text> format(
        const Message& message) {
        return common::Utf8Text::from_utf8(message.content);
    }

    [[nodiscard]] static common::Result<common::Utf8Text> format_transcript(
        const std::vector<Message>& messages) {
        std::string transcript;
        for (const auto& message : messages) {
            switch (message.role) {
            case MessageRole::system: transcript += "System"; break;
            case MessageRole::user: transcript += "User"; break;
            case MessageRole::assistant: transcript += "Assistant"; break;
            case MessageRole::tool:
                transcript += "Tool[";
                transcript += message.tool_name;
                transcript += "]";
                break;
            }
            transcript += ": ";
            transcript += message.content;
            transcript.push_back('\n');
        }
        return common::Utf8Text::from_utf8(transcript);
    }

    [[nodiscard]] static common::Result<common::Utf8Text> format_error(
        const common::Error& error) {
        std::string text = "Erro [";
        text += common::to_string(error.code);
        text += "]: ";
        text += error.message;
        return common::Utf8Text::from_utf8(text);
    }
};

} // namespace morok::comm
