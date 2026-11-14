#include "morok/common/utf8_text.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    const auto bytes = std::as_bytes(std::span(data, size));
    const auto parsed = morok::common::Utf8Text::from_bytes(bytes);
    if (parsed) {
        const auto round_trip = morok::common::Utf8Text::from_utf8(
            parsed.value_if()->view());
        if (!round_trip || round_trip.value_if()->view() != parsed.value_if()->view())
            __builtin_trap();
    }
    return 0;
}
