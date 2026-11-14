#pragma once

#include "morok/common/error.hpp"
#include "morok/common/result.hpp"
#include "morok/common/utf8_text.hpp"
#include "morok/comm/message.hpp"

#include <string>

namespace morok::comm {

class MessageFormatter final {
public:
    [[nodiscard]] static common::Result<common::Utf8Text> format(
        const Message& message) {
        return common::Utf8Text::from_utf8(message.content);
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
