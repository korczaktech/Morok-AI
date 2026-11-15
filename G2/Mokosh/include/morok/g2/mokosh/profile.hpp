#pragma once
#include "morok/common/utf8_text.hpp"
#include "morok/g2/model/learn/g2_generation.hpp"
#include <cstddef>
#include <cstdint>
namespace morok::g2::mokosh {
struct Policy { std::size_t max_new_bytes{96}; std::size_t top_k{8}; std::uint64_t seed{42}; };
[[nodiscard]] model::G2GenerationReport generate(const model::G2Parameters&, const common::Utf8Text&, const Policy& = {});
}
