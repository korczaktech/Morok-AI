#pragma once
#include "morok/common/utf8_text.hpp"
#include "morok/model/learn/g1_generation.hpp"
#include <cstddef>
#include <cstdint>
namespace morok::g1::mokosh {
struct Policy {
    std::size_t max_new_bytes{96};
    std::size_t top_k{8};
    std::uint64_t seed{42};
};
[[nodiscard]] model::G1GenerationReport generate(
    const model::G1Parameters&, const common::Utf8Text&, const Policy& = {});
}
