#include "morok/g2/tsukuyomi/profile.hpp"
#include <algorithm>
namespace morok::g2::tsukuyomi {
model::G2GenerationReport generate(const model::G2Parameters& parameters,const common::Utf8Text& prompt,const Policy& policy) {
 model::G2ExpertGenerationConfig config{}; config.max_new_bytes=std::clamp<std::size_t>(policy.max_new_bytes,1,256); config.temperature=0.0; config.top_k=1; config.seed=policy.seed;
 return model::generate_g2(parameters,prompt,config);
}
}
