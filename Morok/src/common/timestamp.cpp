#include "morok/common/timestamp.hpp"

namespace morok::common {

Timestamp Timestamp::now() noexcept {
    const auto current = std::chrono::system_clock::now().time_since_epoch();
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(current);
    return Timestamp(nanoseconds.count());
}

Timestamp Timestamp::from_unix_nanoseconds(std::int64_t value) noexcept {
    return Timestamp(value);
}

std::int64_t Timestamp::unix_nanoseconds() const noexcept {
    return value_;
}

} // namespace morok::common
