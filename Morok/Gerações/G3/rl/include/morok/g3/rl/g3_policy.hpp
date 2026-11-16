#pragma once

#include "morok/g3/rl/reinforce.hpp"
#include "morok/g3/neural/gru_backend.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace morok::g3::rl {

// Connects the generic REINFORCE engine to the real 100k-parameter G3 model.
// RL updates the tied output head (embedding, projection and biases); recurrent
// GRU tensors remain anchored to the SFT checkpoint to limit destructive drift.
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
    std::uint64_t optimizer_step_{0};
    std::vector<float> first_moment_;
    std::vector<float> second_moment_;
};

} // namespace morok::g3::rl
