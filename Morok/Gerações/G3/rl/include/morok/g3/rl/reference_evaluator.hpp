#pragma once
#include "morok/g3/rl/reinforce.hpp"
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace morok::g3::rl {
using morok::lang::TokenId;
struct ReferenceAnswer final {
    std::vector<TokenId> prompt;
    std::vector<TokenId> answer;
};

// Deterministic, separately versioned lexical rubric backed by reference
// answers. It never reads policy logits or training gradients.
class ReferenceRewardEvaluator final : public RewardEvaluator {
public:
    explicit ReferenceRewardEvaluator(std::vector<ReferenceAnswer> references,
                                      std::string rubric_version="g3-reference-v2-signed");
    [[nodiscard]] EvaluatorMetadata metadata() const override;
    [[nodiscard]] morok::common::Result<RewardComponents> evaluate(
        std::span<const TokenId> prompt,
        std::span<const TokenId> response) const override;
    [[nodiscard]] std::size_t expected_response_tokens(
        std::span<const TokenId> prompt) const override;
private:
    std::unordered_map<std::string,std::vector<TokenId>> references_;
    std::string rubric_version_;
};
} // namespace morok::g3::rl
