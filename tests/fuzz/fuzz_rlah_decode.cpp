#include "morok/lang/rlah/segmenter.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    std::vector<std::uint8_t> decoded;
    std::string error;
    (void)morok::lang::rlah::decode(std::span(data, size), decoded, error);
    return 0;
}
