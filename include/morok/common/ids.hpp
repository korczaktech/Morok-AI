#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace morok::common {

template <typename T>
class Result;

struct SessionIdTag final {};
struct MemoryIdTag final {};
struct RequestIdTag final {};

template <typename Tag>
class BasicId final {
public:
    using Storage = std::array<std::uint8_t, 16>;

    [[nodiscard]] static Result<BasicId> generate();
    [[nodiscard]] static Result<BasicId> from_string(std::string_view value);

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] bool is_nil() const noexcept;

    friend bool operator==(const BasicId&, const BasicId&) = default;

private:
    explicit BasicId(Storage bytes) noexcept : bytes_(bytes) {}

    Storage bytes_{};
};

using SessionId = BasicId<SessionIdTag>;
using MemoryId = BasicId<MemoryIdTag>;
using RequestId = BasicId<RequestIdTag>;

} // namespace morok::common
