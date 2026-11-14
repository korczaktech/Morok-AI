#include "morok/model/learn/g1_checkpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
bool decode_hex_seed(const std::uint8_t* data, std::size_t size,
                     std::vector<std::uint8_t>& output) {
    if (size < 4 || data[0] != 'H' || data[1] != 'E' ||
        data[2] != 'X' || data[3] != ':') return false;
    if ((size - 4) % 2 != 0) return false;
    auto nibble = [](std::uint8_t value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
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
    constexpr std::size_t kMaximumInput = 16U * 1024U * 1024U;
    if (size > kMaximumInput) return 0;

    std::vector<std::uint8_t> input;
    if (!decode_hex_seed(data, size, input) && size != 0)
        input.assign(data, data + size);
    if (input.size() > kMaximumInput) return 0;
    const auto path = std::filesystem::temp_directory_path() /
        "morok-checkpoint-fuzz-input.bin";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) return 0;
        output.write(reinterpret_cast<const char*>(input.data()),
                     static_cast<std::streamsize>(input.size()));
        if (!output) return 0;
    }
    try {
        (void)morok::model::load_g1_checkpoint(path);
    } catch (...) {
        // Malformed input is an expected parser outcome.
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return 0;
}
