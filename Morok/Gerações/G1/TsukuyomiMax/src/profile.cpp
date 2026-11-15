#include "morok/g1/tsukuyomi_max/profile.hpp"
#include "morok/g1/profile_policy.hpp"

namespace morok::g1::tsukuyomi_max {

model::G1GenerationReport generate(const model::G1Parameters& parameters,
                                   const common::Utf8Text& prompt,
                                   const Policy& policy) {
    return model::generate_g1(parameters, prompt,
        g1::make_generation_config(g1::ProfileKind::tsukuyomi_max,
                                   policy.max_new_bytes, policy.seed));
}

} // namespace morok::g1::tsukuyomi_max
