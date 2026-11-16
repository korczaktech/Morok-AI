#include "morok/g3/rl/reinforce.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using morok::lang::TokenId;
using namespace morok::g3::rl;

class MockPolicy final : public Policy {
public:
    explicit MockPolicy(std::string name, std::vector<TokenId> output,
                        float token_probability = 0.8F)
        : name_(std::move(name)), output_(std::move(output)),
          token_probability_(token_probability) {}

    std::string_view identity() const noexcept override { return name_; }

    morok::common::Status next_token_probabilities(
        std::span<const TokenId>,
        std::span<const TokenId> prefix,
        std::span<float> probabilities) const override {
        if (probabilities.size() != kVocabularySize) {
            return morok::common::Status::failure(morok::common::Error::make(
                morok::common::ErrorCode::invalid_argument,
                "bad output shape", "test", "mock_policy"));
        }
        std::fill(probabilities.begin(), probabilities.end(), 0.0F);
        const std::size_t index = prefix.size();
        if (index >= output_.size()) {
            probabilities[morok::lang::kEosToken] = 1.0F;
        } else {
            probabilities[output_[index]] = token_probability_;
            probabilities[morok::lang::kEosToken] =
                1.0F - token_probability_;
        }
        return morok::common::Status::success();
    }

    morok::common::Status apply_batch_gradients(
        std::span<const SequenceGradient> gradients) override {
        ++updates;
        last_batch_size = gradients.size();
        last_gradients_finite = true;
        for (const auto& sequence : gradients) {
            if (sequence.logit_gradients.size() !=
                sequence.response.size() * kVocabularySize) {
                last_gradients_finite = false;
            }
            for (const float value : sequence.logit_gradients) {
                last_gradients_finite =
                    last_gradients_finite && std::isfinite(value);
            }
        }
        return morok::common::Status::success();
    }

    std::size_t updates{0};
    std::size_t last_batch_size{0};
    bool last_gradients_finite{false};

private:
    std::string name_;
    std::vector<TokenId> output_;
    float token_probability_;
};

class MockEvaluator final : public RewardEvaluator {
public:
    EvaluatorMetadata metadata() const override {
        return {"independent-rubric", "v1.0", "human-labelled-fixture"};
    }

    morok::common::Result<RewardComponents> evaluate(
        std::span<const TokenId>,
        std::span<const TokenId> response) const override {
        RewardComponents score;
        if (response.empty()) {
            score.correctness = -1.0;
        } else if (response.front() == static_cast<TokenId>('A')) {
            score.correctness = 1.0;
        } else if (response.front() == static_cast<TokenId>('G')) {
            score.correctness = -1.0; // generic-answer fixture
        } else {
            score.correctness = 0.0;
        }
        score.intent = 0.5;
        score.coherence = 0.5;
        score.format = response.empty() ? -1.0 : 1.0;
        return morok::common::Result<RewardComponents>::success(score);
    }
};

class ExploitEvaluator final : public RewardEvaluator {
public:
    EvaluatorMetadata metadata() const override {
        return {"independent-but-broken", "bad-rubric", "untrusted-fixture"};
    }
    morok::common::Result<RewardComponents> evaluate(
        std::span<const TokenId>,
        std::span<const TokenId>) const override {
        return morok::common::Result<RewardComponents>::success(
            RewardComponents{1.0, 1.0, 1.0, 1.0});
    }
};

void test_config_validation() {
    Config config;
    assert(validate_config(config).has_value());
    config.candidates_per_prompt = 5;
    assert(!validate_config(config).has_value());
    config = {};
    config.reward_weights.correctness = 0.2;
    assert(!validate_config(config).has_value());
}

void test_evaluator_audit_and_reward_hacking() {
    MockEvaluator evaluator;
    const std::array<RewardAuditCase, 1> good_cases{{
        {"correct-answer", {}, {static_cast<TokenId>('A')}, 0.5, 1.0}}};
    assert(audit_evaluator(evaluator, "rl-policy", good_cases).has_value());

    ExploitEvaluator exploit;
    const std::array<RewardAuditCase, 1> exploit_cases{{
        {"generic-answer", {}, {static_cast<TokenId>('G')}, -1.0, 0.0}}};
    assert(!audit_evaluator(exploit, "rl-policy", exploit_cases).has_value());

    MockPolicy empty_policy("empty", {});
    const std::array<Prompt, 1> prompts{{
        Prompt{"empty", "general", {morok::lang::kUsrToken}}}};
    const auto empty = evaluate_policy(empty_policy, evaluator, prompts, 4);
    assert(empty.has_value());
    assert(empty.value_if()->empty_response_rate == 1.0);
    assert(empty.value_if()->mean_reward < 0.0);

    MockPolicy repeated_policy("repeated", {
        static_cast<TokenId>('A'), static_cast<TokenId>('A'),
        static_cast<TokenId>('A')});
    const auto repeated = evaluate_policy(repeated_policy, evaluator, prompts, 8);
    assert(repeated.has_value());
    assert(repeated.value_if()->repeated_response_rate == 1.0);
    assert(repeated.value_if()->mean_reward < 1.0);

    MockPolicy generic_policy("generic", {static_cast<TokenId>('G')});
    const auto generic = evaluate_policy(generic_policy, evaluator, prompts, 4);
    assert(generic.has_value());
    assert(generic.value_if()->mean_reward < 0.0);
}

void test_reinforce_and_reference_kl() {
    Config config;
    config.candidates_per_prompt = 3;
    config.max_response_tokens = 8;
    config.top_k = 2;
    config.seed = 20261116;

    MockPolicy policy("g3-policy", {static_cast<TokenId>('A')}, 0.8F);
    MockPolicy reference("frozen-sft", {static_cast<TokenId>('B')}, 0.8F);
    MockEvaluator evaluator;
    ReinforceTrainer trainer(config);

    const std::array<Prompt, 2> prompts{{
        Prompt{"p1", "factual", {morok::lang::kUsrToken,
                                  static_cast<TokenId>('Q')}},
        Prompt{"p2", "factual", {morok::lang::kUsrToken,
                                  static_cast<TokenId>('R')}}}};

    const auto trained = trainer.train_batch(prompts, policy, reference, evaluator);
    assert(trained.has_value());
    const auto& report = *trained.value_if();
    assert(report.prompts == 2U);
    assert(report.candidates == 6U);
    assert(report.optimizer_updates == 1U);
    assert(policy.updates == 1U);
    assert(policy.last_batch_size == 6U);
    assert(policy.last_gradients_finite);
    assert(std::isfinite(report.mean_reward));
    assert(std::isfinite(report.mean_kl));
    assert(report.mean_kl > 0.0);
    assert(trainer.baseline("factual") != 0.0);
}

void test_closed_set_comparison() {
    MockPolicy sft("sft-snapshot", {static_cast<TokenId>('B')});
    MockPolicy rl("rl-policy", {static_cast<TokenId>('A')});
    MockEvaluator evaluator;
    const std::array<Prompt, 2> closed_set{{
        Prompt{"case-a", "factual", {morok::lang::kUsrToken}},
        Prompt{"case-b", "factual", {morok::lang::kUsrToken}}}};
    const auto comparison = compare_sft_and_rl(sft, rl, evaluator, closed_set, 4);
    assert(comparison.has_value());
    assert(comparison.value_if()->reference_sft.examples == 2U);
    assert(comparison.value_if()->reinforcement_policy.examples == 2U);
    assert(comparison.value_if()->reward_delta > 0.0);
}

} // namespace

int main() {
    test_config_validation();
    test_evaluator_audit_and_reward_hacking();
    test_reinforce_and_reference_kl();
    test_closed_set_comparison();
    std::cout << "G3 REINFORCE tests passed.\n";
    return 0;
}
