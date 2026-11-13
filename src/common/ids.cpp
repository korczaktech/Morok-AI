#include "morok/common/ids.hpp"

#include "morok/common/error.hpp"
#include "morok/common/result.hpp"

#include <array>
#include <exception>
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

} // namespace morok::common
