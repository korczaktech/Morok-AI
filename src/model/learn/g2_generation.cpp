#include "morok/model/learn/g2_generation.hpp"
#include "morok/model/g2_forward.hpp"
#include "morok/g2/engine/generation.hpp"
#include <cstdint>
namespace morok::model {
namespace {
double score(const G2Parameters& p,const common::Utf8Text& prompt,const common::Utf8Text& text) {
    G2State state;
    for(unsigned char c:prompt.string()) advance_g2_state(p,state,c);
    double total=0.0; std::size_t count=0;
    for(unsigned char c:text.string()) {
        total+=evaluate_g2_next_byte(p,state,c).negative_log_likelihood_nats;
        advance_g2_state(p,state,c); ++count;
    }
    return count?total/static_cast<double>(count):0.0;
}
}
G2GenerationReport generate_g2(const G2Parameters& p,const common::Utf8Text& prompt,const G2ExpertGenerationConfig& cfg) {
    auto a=generate_g2_expert(p.expert_a,prompt,cfg);
    auto bcfg=cfg; bcfg.seed^=0x9E3779B97F4A7C15ULL;
    auto b=generate_g2_expert(p.expert_b,prompt,bcfg);
    const double sa=score(p,prompt,a.continuation), sb=score(p,prompt,b.continuation);
    const auto& chosen=sa<=sb?a:b;
    return {chosen.continuation,a.candidate_evaluations+b.candidate_evaluations,
            chosen.generated_bytes,chosen.reached_byte_limit,chosen.stopped_by_eos};
}
}
