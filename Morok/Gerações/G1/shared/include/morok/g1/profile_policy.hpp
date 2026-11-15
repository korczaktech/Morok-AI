#pragma once

#include "morok/model/learn/g1_generation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace morok::g1 {

enum class ProfileKind { tsukuyomi, apollo, mokosh, tsukuyomi_max };

// Shared source of truth for profile decoding policies. These controls are
// inference-time settings and do not add trainable neural parameters.
[[nodiscard]] inline model::G1GenerationConfig make_generation_config(
    ProfileKind profile, std::size_t requested_bytes, std::uint64_t seed,
    std::size_t mokosh_top_k = 8) {
    model::G1GenerationConfig config{};
    config.seed = seed;
    config.printable_only = true;
    switch (profile) {
    case ProfileKind::tsukuyomi:
        config.max_new_bytes = std::clamp<std::size_t>(requested_bytes, 1, 256);
        config.temperature = 0.8;
        config.top_k = 0;
        config.repetition_penalty = 0.0;
        config.repetition_window = 48;
        break;
    case ProfileKind::apollo:
        config.max_new_bytes = std::clamp<std::size_t>(requested_bytes, 1, 512);
        config.temperature = 0.8;
        config.top_k = 0;
        config.repetition_penalty = 0.0;
        config.repetition_window = 48;
        break;
    case ProfileKind::mokosh:
        config.max_new_bytes = std::clamp<std::size_t>(requested_bytes, 1, 128);
        config.temperature = 0.7;
        config.top_k = mokosh_top_k;
        config.repetition_penalty = 0.0;
        config.repetition_window = 32;
        break;
    case ProfileKind::tsukuyomi_max:
        config.max_new_bytes = std::clamp<std::size_t>(requested_bytes, 1, 512);
        config.temperature = 0.75;
        config.top_k = 16;
        config.repetition_penalty = 0.0;
        config.repetition_window = 64;
        break;
    }
    return config;
}

} // namespace morok::g1
