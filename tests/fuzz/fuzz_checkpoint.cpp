#include "morok/model/learn/g1_checkpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    constexpr std::size_t kMaximumInput = 16U * 1024U * 1024U;
    if (size > kMaximumInput) return 0;

    const auto path = std::filesystem::temp_directory_path() /
        "morok-checkpoint-fuzz-input.bin";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) return 0;
        output.write(reinterpret_cast<const char*>(data),
                     static_cast<std::streamsize>(size));
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
