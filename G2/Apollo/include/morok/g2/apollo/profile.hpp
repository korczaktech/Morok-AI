#pragma once
#include "morok/common/utf8_text.hpp"
#include "morok/g2/model/learn/g2_generation.hpp"
#include <cstddef>
#include <cstdint>
namespace morok::g2::apollo {
struct Policy { std::size_t max_new_bytes{256}; std::uint64_t seed{42}; };
[[nodiscard]] model::G2GenerationReport generate(const model::G2Parameters&,const common::Utf8Text&,const Policy& = {});
}
