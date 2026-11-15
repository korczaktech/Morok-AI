#include "morok/g1/apollo/profile.hpp"
#include <algorithm>
namespace morok::g1::apollo {
model::G1GenerationReport generate(const model::G1Parameters& parameters,
                                   const common::Utf8Text& prompt,
                                   const Policy& policy) {
    model::G1GenerationConfig config{};
    config.max_new_bytes = std::clamp<std::size_t>(policy.max_new_bytes, 1, 512);
    config.temperature = 0.0;
    config.top_k = 1;
    config.repetition_penalty = 0.0;
    config.repetition_window = 0;
    config.seed = policy.seed;
    return model::generate_g1(parameters, prompt, config);
}
}
