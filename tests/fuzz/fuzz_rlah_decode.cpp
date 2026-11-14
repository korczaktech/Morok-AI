#include "morok/lang/rlah/segmenter.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {
bool decode_hex_seed(const std::uint8_t* data, std::size_t size,
                     std::vector<std::uint8_t>& output) {
    if (size < 4 || data[0] != 'H' || data[1] != 'E' ||
        data[2] != 'X' || data[3] != ':') return false;
    if ((size - 4) % 2 != 0) return false;
    auto nibble = [](std::uint8_t c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    output.clear();
    output.reserve((size - 4) / 2);
    for (std::size_t i = 4; i < size; i += 2) {
        const int high = nibble(data[i]);
        const int low = nibble(data[i + 1]);
        if (high < 0 || low < 0) { output.clear(); return false; }
        output.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}
} // namespace


extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    std::vector<std::uint8_t> input;
    if (!decode_hex_seed(data, size, input) && size != 0)
        input.assign(data, data + size);
    std::vector<std::uint8_t> decoded;
    std::string error;
    (void)morok::lang::rlah::decode(input, decoded, error);
    return 0;
}
