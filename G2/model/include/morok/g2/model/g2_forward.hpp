#pragma once
#include "morok/g2/engine/forward.hpp"
#include "morok/g2/model/parameters/g2_parameters.hpp"
namespace morok::g2::model {
struct G2State { engine::G2ExpertState expert_a{}, expert_b{}; void reset() noexcept { expert_a.reset(); expert_b.reset(); } };
struct G2Evaluation { double negative_log_likelihood_nats{0.0}; };
struct G2Prediction { std::array<double,engine::kG2ExpertPredictionBits> bit_probabilities{}; std::uint8_t greedy_byte{0}; };
void advance_g2_state(const G2Parameters&, G2State&, std::uint8_t observed_byte);
[[nodiscard]] G2Evaluation evaluate_g2_next_byte(const G2Parameters&, const G2State&, std::uint8_t target_byte);
[[nodiscard]] G2Prediction predict_g2_next_byte(const G2Parameters&, const G2State&);
}
