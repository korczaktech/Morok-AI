#pragma once

#include "morok/g3/rl/reinforce.hpp"
#include "morok/g3/neural/gru_backend.hpp"
#include "morok/g3/training/trainer.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace morok::g3::rl {
using morok::lang::TokenId;

// Connects REINFORCE to the real 100k-parameter G3 model. Sampled-token
// logit gradients are propagated through both GRU layers with truncated BPTT;
// all 100,000 FP32 parameters are updated with the SFT AdamW implementation.
class G3PolicyAdapter final : public Policy {
public:
    G3PolicyAdapter(neural::G3Backend& backend, std::string identity,
                    double learning_rate = 1.0e-4,
                    double weight_decay = 1.0e-4);

    [[nodiscard]] std::string_view identity() const noexcept override;
    [[nodiscard]] morok::common::Status next_token_probabilities(
        std::span<const TokenId> context,
        std::span<const TokenId> generated_prefix,
        std::span<float> probabilities) const override;
    [[nodiscard]] morok::common::Status apply_batch_gradients(
        std::span<const SequenceGradient> gradients) override;

private:
    neural::G3Backend* backend_;
    std::string identity_;
    double learning_rate_;
    double weight_decay_;
    training::ReinforceOptimizerState optimizer_state_;
    training::TrainingConfig optimizer_config_;
    mutable bool cache_valid_{false};
    mutable std::vector<TokenId> cached_context_;
    mutable std::vector<TokenId> cached_prefix_;
    mutable std::vector<std::byte> cached_state_;
};

} // namespace morok::g3::rl
