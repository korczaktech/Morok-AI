#include "morok/common/ids.hpp"

#include "morok/common/error.hpp"
#include "morok/common/result.hpp"

#include <array>
#include <exception>
#include <algorithm>
#include <random>
#include <string>

namespace morok::common {

template <typename Tag>
Result<BasicId<Tag>> BasicId<Tag>::generate() {
    try {
        std::random_device source;
        if (source.entropy() == 0.0) {
            return Result<BasicId>::failure(Error::make(
                ErrorCode::internal_error,
                "The platform random source does not report entropy.",
                "core",
                "identifier_generation"));
        }

        Storage bytes{};
        for (std::size_t offset = 0; offset < bytes.size(); offset += sizeof(std::uint32_t)) {
            const auto value = source();
            for (std::size_t index = 0; index < sizeof(std::uint32_t); ++index) {
                bytes[offset + index] = static_cast<std::uint8_t>(
                    (value >> (index * 8U)) & 0xFFU);
            }
        }

        // UUID-compatible version and variant bits; the identifier remains opaque to callers.
        bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U);
        bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);
        return Result<BasicId>::success(BasicId(bytes));
    } catch (const std::exception&) {
        return Result<BasicId>::failure(Error::make(
            ErrorCode::internal_error,
            "The platform random source failed.",
            "core",
            "identifier_generation"));
    }
}

template <typename Tag>
Result<BasicId<Tag>> BasicId<Tag>::from_string(std::string_view value) {
    if (value.size() != 36U) {
        return Result<BasicId>::failure(Error::make(
            ErrorCode::invalid_argument, "identifier must use canonical UUID form",
            "core", "identifier_parse"));
    }
    constexpr std::array<std::size_t, 4> hyphen_positions{8U, 13U, 18U, 23U};
    Storage bytes{};
    std::size_t byte_index = 0;
    int high_nibble = -1;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        const bool must_be_hyphen =
            std::find(hyphen_positions.begin(), hyphen_positions.end(), index) !=
            hyphen_positions.end();
        if (must_be_hyphen) {
            if (ch != '-') return Result<BasicId>::failure(Error::make(
                ErrorCode::invalid_argument, "identifier has invalid UUID separators",
                "core", "identifier_parse"));
            continue;
        }
        unsigned int digit = 0;
        if (ch >= '0' && ch <= '9') digit = static_cast<unsigned int>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') digit = static_cast<unsigned int>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') digit = static_cast<unsigned int>(ch - 'A' + 10);
        else return Result<BasicId>::failure(Error::make(
            ErrorCode::invalid_argument, "identifier contains a non-hexadecimal character",
            "core", "identifier_parse"));
        if (high_nibble < 0) high_nibble = static_cast<int>(digit);
        else {
            if (byte_index >= bytes.size()) return Result<BasicId>::failure(Error::make(
                ErrorCode::invalid_argument, "identifier has too many digits",
                "core", "identifier_parse"));
            bytes[byte_index++] = static_cast<std::uint8_t>(
                (static_cast<unsigned int>(high_nibble) << 4U) | digit);
            high_nibble = -1;
        }
    }
    if (byte_index != bytes.size() || high_nibble >= 0) {
        return Result<BasicId>::failure(Error::make(
            ErrorCode::invalid_argument, "identifier has an invalid length",
            "core", "identifier_parse"));
    }
    return Result<BasicId>::success(BasicId(bytes));
}

template <typename Tag>
std::string BasicId<Tag>::to_string() const {
    constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(36);
    for (std::size_t index = 0; index < bytes_.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            output.push_back('-');
        }
        output.push_back(digits[(bytes_[index] >> 4U) & 0x0FU]);
        output.push_back(digits[bytes_[index] & 0x0FU]);
    }
    return output;
}

template <typename Tag>
bool BasicId<Tag>::is_nil() const noexcept {
    for (const auto byte : bytes_) {
        if (byte != 0U) {
            return false;
        }
    }
    return true;
}

template class BasicId<SessionIdTag>;
template class BasicId<MemoryIdTag>;
template class BasicId<RequestIdTag>;
template class BasicId<MessageIdTag>;
template class BasicId<ConversationIdTag>;

} // namespace morok::common
