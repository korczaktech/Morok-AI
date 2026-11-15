#include "morok/model/g2_forward.hpp"
#include <cmath>
#include <stdexcept>
namespace morok::model {
void advance_g2_state(const G2Parameters& p,G2State& s,std::uint8_t b) {
    advance_g2_expert_state(p.expert_a,s.expert_a,b);
    advance_g2_expert_state(p.expert_b,s.expert_b,b);
}
G2Evaluation evaluate_g2_next_byte(const G2Parameters& p,const G2State& s,std::uint8_t b) {
    const auto a=evaluate_g2_expert_next_byte(p.expert_a,s.expert_a,b);
    const auto c=evaluate_g2_expert_next_byte(p.expert_b,s.expert_b,b);
    const double loss=(a.negative_log_likelihood_nats+c.negative_log_likelihood_nats)*0.5;
    if(!std::isfinite(loss)) throw std::domain_error("G2 produced a non-finite loss");
    return {loss};
}
G2Prediction predict_g2_next_byte(const G2Parameters& p,const G2State& s) {
    const auto a=predict_g2_expert_next_byte(p.expert_a,s.expert_a);
    const auto b=predict_g2_expert_next_byte(p.expert_b,s.expert_b);
    G2Prediction out; unsigned char byte=0;
    for(std::size_t i=0;i<kG2ExpertPredictionBits;++i) {
        out.bit_probabilities[i]=(a.bit_probabilities[i]+b.bit_probabilities[i])*0.5;
        if(out.bit_probabilities[i]>=0.5) byte|=static_cast<unsigned char>(1U<<(7U-i));
    }
    out.greedy_byte=byte; return out;
}
}
