#include "morok/common/utf8_text.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace morok::common {
namespace {

[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<std::uint8_t>(
            static_cast<unsigned char>(text[index]));

        if (lead <= 0x7FU) {
            ++index;
            continue;
        }

        std::size_t continuation_count = 0;
        std::uint32_t code_point = 0;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            continuation_count = 1;
            code_point = lead & 0x1FU;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            continuation_count = 2;
            code_point = lead & 0x0FU;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            continuation_count = 3;
            code_point = lead & 0x07U;
        } else {
            return false;
        }

        if (continuation_count > text.size() - index - 1U) {
            return false;
        }

        for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
            const auto byte = static_cast<std::uint8_t>(
                static_cast<unsigned char>(text[index + offset]));
            if ((byte & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (byte & 0x3FU);
        }

        if ((continuation_count == 2U && code_point < 0x800U) ||
            (continuation_count == 3U && code_point < 0x10000U) ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU) ||
            code_point > 0x10FFFFU) {
            return false;
        }

        index += continuation_count + 1U;
    }
    return true;
}

} // namespace

Result<Utf8Text> Utf8Text::from_utf8(std::string_view text) {
    if (!is_valid_utf8(text)) {
        return Result<Utf8Text>::failure(Error::make(
            ErrorCode::invalid_utf8,
            "Input contains an invalid UTF-8 sequence.",
            "lang",
            "utf8_validation"));
    }
    return Result<Utf8Text>::success(Utf8Text(std::string(text)));
}

Result<Utf8Text> Utf8Text::from_bytes(std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return from_utf8({});
    }
    const auto* data = reinterpret_cast<const char*>(bytes.data());
    return from_utf8(std::string_view(data, bytes.size()));
}

std::string_view Utf8Text::view() const noexcept {
    return text_;
}

const std::string& Utf8Text::string() const noexcept {
    return text_;
}

std::span<const std::byte> Utf8Text::bytes() const noexcept {
    return {
        reinterpret_cast<const std::byte*>(text_.data()),
        text_.size()
    };
}

bool Utf8Text::empty() const noexcept {
    return text_.empty();
}

std::size_t Utf8Text::size_bytes() const noexcept {
    return text_.size();
}

} // namespace morok::common
