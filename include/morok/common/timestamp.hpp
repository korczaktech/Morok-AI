#pragma once

#include <chrono>
#include <compare>
#include <cstdint>

namespace morok::common {

// UTC instant represented as nanoseconds since the Unix epoch.
class Timestamp final {
public:
    [[nodiscard]] static Timestamp now() noexcept;
    [[nodiscard]] static Timestamp from_unix_nanoseconds(std::int64_t value) noexcept;

    [[nodiscard]] std::int64_t unix_nanoseconds() const noexcept;

    friend bool operator==(const Timestamp&, const Timestamp&) = default;
    friend auto operator<=>(const Timestamp&, const Timestamp&) = default;

private:
    explicit Timestamp(std::int64_t value) noexcept : value_(value) {}
    std::int64_t value_{0};
};

} // namespace morok::common
