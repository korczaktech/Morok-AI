#pragma once

#include "morok/common/byte_buffer.hpp"
#include "morok/common/result.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace morok::common {

// Owns text that has passed strict UTF-8 validation.
class Utf8Text final {
public:
    [[nodiscard]] static Result<Utf8Text> from_utf8(std::string_view text);
    [[nodiscard]] static Result<Utf8Text> from_bytes(std::span<const std::byte> bytes);

    [[nodiscard]] std::string_view view() const noexcept;
    [[nodiscard]] const std::string& string() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size_bytes() const noexcept;

private:
    explicit Utf8Text(std::string text) : text_(std::move(text)) {}
    std::string text_;
};

} // namespace morok::common
