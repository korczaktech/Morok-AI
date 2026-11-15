#include "morok/g1/apollo/profile.hpp"
#include "morok/g1/profile_policy.hpp"

namespace morok::g1::apollo {
model::G1GenerationReport generate(const model::G1Parameters& parameters,
                                   const common::Utf8Text& prompt,
                                   const Policy& policy) {
    return model::generate_g1(parameters, prompt,
        g1::make_generation_config(g1::ProfileKind::apollo,
                                   policy.max_new_bytes, policy.seed));
}
}
