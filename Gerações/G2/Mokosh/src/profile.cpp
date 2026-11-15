#include "morok/g2/mokosh/profile.hpp"
#include <algorithm>
namespace morok::g2::mokosh {
model::G2GenerationReport generate(const model::G2Parameters& parameters,const common::Utf8Text& prompt,const Policy& policy) {
 engine::G2ExpertGenerationConfig config{}; config.max_new_bytes=std::clamp<std::size_t>(policy.max_new_bytes,1,128); config.top_k=std::max<std::size_t>(1,policy.top_k); config.temperature=0.0; config.repetition_penalty=0.15; config.repetition_window=24; config.seed=policy.seed;
 return model::generate_g2(parameters,prompt,config);
}
}
