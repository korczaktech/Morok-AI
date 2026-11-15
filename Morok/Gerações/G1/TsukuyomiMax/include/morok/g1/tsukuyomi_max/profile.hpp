#pragma once

#include "morok/common/utf8_text.hpp"
#include "morok/model/learn/g1_generation.hpp"

#include <cstddef>
#include <cstdint>

namespace morok::g1::tsukuyomi_max {

struct Policy {
    std::size_t max_new_bytes{384};
    std::uint64_t seed{42};
};

[[nodiscard]] model::G1GenerationReport generate(
    const model::G1Parameters&, const common::Utf8Text&, const Policy& = {});

} // namespace morok::g1::tsukuyomi_max
