#pragma once

#include "morok/model/learn/g1_generation.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace morok::g1 {
[[nodiscard]] inline model::G1GenerationConfig make_generation_config(
    std::size_t requested_bytes, std::uint64_t seed) {
    model::G1GenerationConfig config{};
    config.seed = seed;
    config.printable_only = true;
    config.max_new_bytes = std::clamp<std::size_t>(requested_bytes, 1, 512);
    config.temperature = 0.75;
    config.top_k = 16;
    config.repetition_penalty = 0.0;
    config.repetition_window = 64;
    return config;
}
} // namespace morok::g1
