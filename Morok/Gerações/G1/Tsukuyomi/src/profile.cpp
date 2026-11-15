#include "morok/g1/tsukuyomi/profile.hpp"
#include <algorithm>
namespace morok::g1::tsukuyomi {
model::G1GenerationReport generate(const model::G1Parameters& parameters,
                                   const common::Utf8Text& prompt,
                                   const Policy& policy) {
    model::G1GenerationConfig config{};
    config.max_new_bytes = std::clamp<std::size_t>(policy.max_new_bytes, 1, 256);
    // Sampling avoids deterministic byte collapse; the seed keeps runs reproducible.
    config.temperature = 0.8;
    config.top_k = 0;
    config.seed = policy.seed;
    config.repetition_penalty = 0.35;
    config.repetition_window = 48;
    config.printable_only = true;
    return model::generate_g1(parameters, prompt, config);
}
}
