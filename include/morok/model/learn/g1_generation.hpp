#pragma once

#include "morok/common/utf8_text.hpp"
#include "morok/model/g1_forward.hpp"

#include <cstddef>

namespace morok::model {

struct G1GenerationConfig {
    std::size_t max_new_bytes{256};
    // Zero temperature preserves greedy decoding. Positive values sample from
    // the model distribution after optional top-k filtering.
    double temperature{0.0};
    std::size_t top_k{0}; // Zero means all valid UTF-8 candidates.
    std::uint64_t seed{42};
    // Add this many nats per recent occurrence of a printable ASCII byte.
    double repetition_penalty{0.0};
    std::size_t repetition_window{64};
};

struct G1GenerationReport {
    common::Utf8Text continuation;
    std::size_t candidate_evaluations{0};
    std::size_t generated_bytes{0};
    bool reached_byte_limit{false};
};

// Greedy autoregressive decoding constrained to valid UTF-8 byte transitions.
// G1 has no EOS token: generation is bounded by max_new_bytes.
[[nodiscard]] G1GenerationReport generate_g1(
    const G1Parameters& parameters,
    const common::Utf8Text& prompt,
    const G1GenerationConfig& config = {});

} // namespace morok::model
