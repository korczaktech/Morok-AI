#include "morok/g3/rl/reinforce.hpp"
#include "morok/g3/rl/g3_policy.hpp"
#include "morok/g3/rl/reference_evaluator.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using morok::lang::TokenId;
using namespace morok::g3::rl;

const std::array<RewardAuditCase, 1> kValidEvaluatorAudit{{
    {"correct-A", {}, {static_cast<TokenId>('A')}, 0.5, 1.0}}};

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
        last_gradients.assign(gradients.begin(), gradients.end());
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
    std::vector<SequenceGradient> last_gradients{};

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

class LengthNeutralEvaluator final : public RewardEvaluator {
public:
    EvaluatorMetadata metadata() const override {
        return {"length-neutral-rubric", "v1", "human-labelled-fixture"};
    }
    morok::common::Result<RewardComponents> evaluate(
        std::span<const TokenId>, std::span<const TokenId> response) const override {
        if (response.empty())
            return morok::common::Result<RewardComponents>::success(
                RewardComponents{-1.0, -1.0, 0.0, -1.0});
        return morok::common::Result<RewardComponents>::success(
            RewardComponents{1.0, 1.0, 1.0, 1.0});
    }
    std::size_t expected_response_tokens(std::span<const TokenId>) const override {
        return 100U;
    }
};

void test_short_correct_answer_is_not_length_penalized() {
    LengthNeutralEvaluator evaluator;
    MockPolicy concise("concise-answer", {static_cast<TokenId>('Y')});
    const std::array<Prompt, 1> prompts{{
        Prompt{"concise", "general", {morok::lang::kUsrToken}}}};
    const auto report = evaluate_policy(concise, evaluator, prompts, 4U);
    assert(report.has_value());
    assert(std::abs(report.value_if()->mean_reward - 1.0) < 1.0e-9);
}

void test_missing_eos_is_penalized() {
    LengthNeutralEvaluator evaluator;
    MockPolicy policy("eos-policy", {static_cast<TokenId>('Y')});
    const std::array<Prompt, 1> prompts{{
        Prompt{"eos", "general", {morok::lang::kUsrToken}}}};
    const auto truncated = evaluate_policy(policy, evaluator, prompts, 1U);
    const auto terminated = evaluate_policy(policy, evaluator, prompts, 2U);
    assert(truncated.has_value() && terminated.has_value());
    assert(truncated.value_if()->mean_reward < terminated.value_if()->mean_reward);
    assert(std::abs(terminated.value_if()->mean_reward - 1.0) < 1.0e-9);
}

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

    const std::string repeated_phrase = "ha ha ha";
    std::vector<TokenId> repeated_phrase_tokens;
    for (const unsigned char byte : repeated_phrase) {
        repeated_phrase_tokens.push_back(static_cast<TokenId>(byte));
    }
    MockPolicy phrase_policy("repeated-phrase", repeated_phrase_tokens, 1.0F);
    const auto phrase = evaluate_policy(phrase_policy, evaluator, prompts, 16);
    assert(phrase.has_value());
    assert(phrase.value_if()->repeated_response_rate == 1.0);

    MockPolicy generic_policy("generic", {static_cast<TokenId>('G')});
    const auto generic = evaluate_policy(generic_policy, evaluator, prompts, 4);
    assert(generic.has_value());
    assert(generic.value_if()->mean_reward < 0.0);
}


void test_independent_rl_training_without_sft_reference() {
    Config config;
    config.candidates_per_prompt = 1;
    config.max_response_tokens = 4;
    config.top_k = 2;
    config.seed = 20261120;
    assert(config.kl_beta == 0.0);

    MockPolicy policy("independent-rl-policy", {static_cast<TokenId>('A')});
    MockEvaluator evaluator;
    ReinforceTrainer trainer(config);
    const std::array<Prompt, 1> prompts{{
        Prompt{"independent", "general", {morok::lang::kUsrToken}}}};

    const auto result = trainer.train_batch(
        prompts, policy, evaluator, kValidEvaluatorAudit);
    assert(result.has_value());
    assert(policy.updates == 1U);
    assert(result.value_if()->optimizer_updates == 1U);
    assert(result.value_if()->mean_kl == 0.0);

    const auto state = trainer.capture_state(policy.identity(), evaluator.metadata());
    assert(state.has_value());
    assert(state.value_if()->reference_identity == "independent-no-reference");

    ReinforceTrainer restored(config);
    assert(restored.restore_state(
        *state.value_if(), policy.identity(), evaluator.metadata()).has_value());
    assert(restored.baseline("general") == trainer.baseline("general"));
}

void test_training_requires_evaluator_audit() {
    Config config;
    config.candidates_per_prompt = 1;
    MockPolicy policy("unaudited-policy", {static_cast<TokenId>('A')});
    MockPolicy reference("unaudited-sft", {static_cast<TokenId>('B')});
    MockEvaluator evaluator;
    ReinforceTrainer trainer(config);
    const std::array<Prompt, 1> prompts{{
        Prompt{"audit-required", "general", {morok::lang::kUsrToken}}}};
    const auto result = trainer.train_batch(
        prompts, policy, reference, evaluator,
        std::span<const RewardAuditCase>{});
    assert(!result.has_value());
    assert(policy.updates == 0U);
}

void test_reinforce_and_reference_kl() {
    Config config;
    config.candidates_per_prompt = 3;
    config.max_response_tokens = 8;
    config.top_k = 2;
    config.seed = 20261116;
    config.kl_beta = 0.02;

    MockPolicy policy("g3-policy", {static_cast<TokenId>('A')}, 0.8F);
    MockPolicy reference("frozen-sft", {static_cast<TokenId>('B')}, 0.8F);
    MockEvaluator evaluator;
    ReinforceTrainer trainer(config);

    const std::array<Prompt, 2> prompts{{
        Prompt{"p1", "factual", {morok::lang::kUsrToken,
                                  static_cast<TokenId>('Q')}},
        Prompt{"p2", "factual", {morok::lang::kUsrToken,
                                  static_cast<TokenId>('R')}}}};

    const auto trained = trainer.train_batch(prompts, policy, reference, evaluator, kValidEvaluatorAudit);
    assert(trained.has_value());
    const auto& report = *trained.value_if();
    assert(report.prompts == 2U);
    assert(report.candidates == 6U);
    assert(report.optimizer_updates == 1U);
    assert(policy.updates == 1U);
    assert(policy.last_batch_size == 6U);
    assert(policy.last_gradients_finite);
    assert(reference.updates == 0U);
    assert(report.evaluator.identity == "independent-rubric");
    assert(report.config_snapshot.temperature == config.temperature);
    for (const auto& candidate : report.candidates_metrics) {
        assert(candidate.response_tokens_data.size() ==
               candidate.sampled_token_log_probabilities.size());
        for (const double log_probability :
             candidate.sampled_token_log_probabilities) {
            assert(std::isfinite(log_probability));
        }
    }
    assert(std::isfinite(report.mean_reward));
    assert(std::isfinite(report.mean_kl));
    assert(report.mean_kl > 0.0);
    assert(trainer.baseline("factual") != 0.0);

    const auto saved = trainer.capture_state(
        policy.identity(), reference.identity(), evaluator.metadata());
    assert(saved.has_value());
    ReinforceTrainer resumed(config);
    assert(resumed.restore_state(*saved.value_if(), policy.identity(),
                                 reference.identity(), evaluator.metadata()).has_value());
    assert(resumed.baseline("factual") == trainer.baseline("factual"));
    const auto round_trip = resumed.capture_state(
        policy.identity(), reference.identity(), evaluator.metadata());
    assert(round_trip.has_value());
    assert(round_trip.value_if()->random_engine_state ==
           saved.value_if()->random_engine_state);
    assert(!resumed.restore_state(*saved.value_if(), policy.identity(),
                                  "different-reference",
                                  evaluator.metadata()).has_value());
    TrainerState corrupt = *saved.value_if();
    corrupt.random_engine_state = "not-an-engine-state";
    assert(!resumed.restore_state(corrupt, policy.identity(),
                                  reference.identity(),
                                  evaluator.metadata()).has_value());
}


class ZeroEvaluator final : public RewardEvaluator {
public:
    EvaluatorMetadata metadata() const override {
        return {"independent-zero-rubric", "v1.0", "human-labelled-gradient-fixture"};
    }

    morok::common::Result<RewardComponents> evaluate(
        std::span<const TokenId>,
        std::span<const TokenId>) const override {
        return morok::common::Result<RewardComponents>::success(
            RewardComponents{0.0, 0.0, 0.0, 0.0});
    }
};

void test_exact_kl_logit_gradient() {
    Config config;
    config.candidates_per_prompt = 1;
    config.max_response_tokens = 1;
    config.top_k = 2;
    config.temperature = 1.0;
    config.kl_beta = 0.02;
    config.penalties.empty_response = 0.0;
    config.penalties.missing_eos = 0.0;

    MockPolicy policy("current-policy", {static_cast<TokenId>('A')}, 0.8F);
    MockPolicy reference("frozen-reference", {static_cast<TokenId>('B')}, 0.8F);
    ZeroEvaluator evaluator;
    ReinforceTrainer trainer(config);
    const std::array<Prompt, 1> prompts{{
        Prompt{"kl-gradient", "math", {morok::lang::kUsrToken}}}};
    const std::array<RewardAuditCase, 1> audit{{
        {"zero-score", {}, {static_cast<TokenId>('A')}, 0.0, 0.0}}};

    const auto trained = trainer.train_batch(
        prompts, policy, reference, evaluator, audit);
    assert(trained.has_value());
    assert(policy.last_gradients.size() == 1U);
    const auto& gradient = policy.last_gradients.front().logit_gradients;
    assert(gradient.size() == kVocabularySize);

    const double p = 0.8;
    const double q_floor = config.kl_epsilon;
    const double kl = p * std::log(p / q_floor);
    const double expected_a =
        config.kl_beta * p * (std::log(p / q_floor) - kl);
    const double expected_eos =
        config.kl_beta * 0.2 * (0.0 - kl);
    assert(std::abs(gradient[static_cast<std::size_t>('A')] - expected_a) < 1.0e-5);
    assert(std::abs(gradient[morok::lang::kEosToken] - expected_eos) < 1.0e-5);
    assert(std::abs(gradient[static_cast<std::size_t>('B')]) < 1.0e-7);
}

void test_copy_and_justified_length_penalties() {
    MockEvaluator evaluator;
    MockPolicy answer("answer", {static_cast<TokenId>('A')});
    const std::array<Prompt, 1> clean_prompt{{
        Prompt{"clean", "general", {morok::lang::kUsrToken}}}};
    const std::array<Prompt, 1> copied_prompt{{
        Prompt{"copied", "general", {morok::lang::kUsrToken,
                                      static_cast<TokenId>('A')}}}};
    const auto clean = evaluate_policy(answer, evaluator, clean_prompt, 4);
    const auto copied = evaluate_policy(answer, evaluator, copied_prompt, 4);
    assert(clean.has_value() && copied.has_value());
    assert(copied.value_if()->mean_reward < clean.value_if()->mean_reward);

    Config no_length_penalty;
    no_length_penalty.candidates_per_prompt = 1;
    no_length_penalty.max_response_tokens = 8;
    no_length_penalty.top_k = 2;
    MockPolicy long_answer("long-answer", {
        static_cast<TokenId>('A'), static_cast<TokenId>('B'),
        static_cast<TokenId>('C')}, 1.0F);
    MockPolicy frozen("long-answer-sft", {
        static_cast<TokenId>('X'), static_cast<TokenId>('Y'),
        static_cast<TokenId>('Z')}, 1.0F);
    ReinforceTrainer unrestricted(no_length_penalty);
    const auto no_penalty = unrestricted.train_batch(
        clean_prompt, long_answer, frozen, evaluator, kValidEvaluatorAudit);
    assert(no_penalty.has_value());

    Config bounded = no_length_penalty;
    bounded.penalties.max_response_tokens = 1;
    bounded.penalties.over_length = 0.25;
    MockPolicy bounded_policy("bounded-policy", {
        static_cast<TokenId>('A'), static_cast<TokenId>('B'),
        static_cast<TokenId>('C')}, 1.0F);
    MockPolicy bounded_reference("bounded-sft", {
        static_cast<TokenId>('X'), static_cast<TokenId>('Y'),
        static_cast<TokenId>('Z')}, 1.0F);
    ReinforceTrainer bounded_trainer(bounded);
    const auto with_penalty = bounded_trainer.train_batch(
        clean_prompt, bounded_policy, bounded_reference, evaluator, kValidEvaluatorAudit);
    assert(with_penalty.has_value());
    assert(with_penalty.value_if()->mean_reward <
           no_penalty.value_if()->mean_reward);
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


void test_adamw_rejects_nonfinite_rl_update_atomically() {
    morok::g3::neural::G3Backend backend(
        morok::g3::neural::G3Config{812U, 1.0e-5F});
    G3PolicyAdapter policy(backend, "g3-rl-invalid-gradient");
    const auto before_embedding = backend.weights().embedding;
    const auto before_recurrent = backend.weights().layer1_recurrent;

    SequenceGradient invalid;
    invalid.context = {morok::lang::kBosToken, morok::lang::kUsrToken,
                       morok::lang::kBotToken};
    invalid.response = {static_cast<TokenId>('A')};
    invalid.logit_gradients.assign(kVocabularySize, 0.0F);
    invalid.logit_gradients[static_cast<std::size_t>('A')] =
        std::numeric_limits<float>::quiet_NaN();
    const std::array<SequenceGradient, 1> batch{{invalid}};

    const auto status = policy.apply_batch_gradients(batch);
    assert(!status.has_value());
    assert(backend.weights().embedding == before_embedding);
    assert(backend.weights().layer1_recurrent == before_recurrent);
}

void test_real_g3_policy_adapter_and_independent_evaluator() {
    morok::g3::neural::G3Backend backend(
        morok::g3::neural::G3Config{91U, 1.0e-5F});
    G3PolicyAdapter policy(backend, "g3-rl-trainable");
    const std::array<TokenId,4> prompt{{
        morok::lang::kBosToken,morok::lang::kUsrToken,
        static_cast<TokenId>('H'),morok::lang::kBotToken}};
    std::array<float,kVocabularySize> probabilities{};
    assert(policy.next_token_probabilities(prompt,{},probabilities).has_value());
    double probability_sum=0.0;
    for(float p:probabilities) { assert(std::isfinite(p) && p>=0.0F); probability_sum+=p; }
    assert(std::abs(probability_sum-1.0)<1.0e-5);

    const float original_bias=backend.weights().output_bias[static_cast<std::size_t>('A')];
    const auto original_recurrent=backend.weights().layer1_recurrent;
    SequenceGradient gradient;
    gradient.context.assign(prompt.begin(),prompt.end());
    gradient.response={static_cast<TokenId>('A'),morok::lang::kEosToken};
    gradient.logit_gradients.assign(2U*kVocabularySize,0.0F);
    gradient.logit_gradients[static_cast<std::size_t>('A')]=-0.5F;
    gradient.logit_gradients[kVocabularySize+static_cast<std::size_t>('B')]=0.5F;
    const std::array<SequenceGradient,1> batch{{gradient}};
    assert(policy.apply_batch_gradients(batch).has_value());
    assert(backend.parameter_count()==100000U);
    assert(backend.weights().output_bias[static_cast<std::size_t>('A')]!=original_bias);
    assert(backend.weights().layer1_recurrent!=original_recurrent);

    ReferenceRewardEvaluator evaluator({
        ReferenceAnswer{std::vector<TokenId>(prompt.begin(),prompt.end()),
                        std::vector<TokenId>{static_cast<TokenId>('O'),static_cast<TokenId>('K')}}});
    const std::array<TokenId,2> answer{{static_cast<TokenId>('O'),static_cast<TokenId>('K')}};
    const auto reward=evaluator.evaluate(prompt,answer);
    assert(reward.has_value());
    assert(reward.value_if()->correctness==1.0);
    const std::array<TokenId,2> wrong_answer{{static_cast<TokenId>('N'),static_cast<TokenId>('O')}};
    const auto wrong_reward=evaluator.evaluate(prompt,wrong_answer);
    assert(wrong_reward.has_value());
    assert(wrong_reward.value_if()->correctness<0.0);
    assert(wrong_reward.value_if()->intent<0.0);
    const Config default_config;
    const auto& weights=default_config.reward_weights;
    const double unrelated_score =
        weights.correctness*wrong_reward.value_if()->correctness +
        weights.intent*wrong_reward.value_if()->intent +
        weights.coherence*wrong_reward.value_if()->coherence +
        weights.format*wrong_reward.value_if()->format;
    assert(unrelated_score<0.0);
    assert(evaluator.expected_response_tokens(prompt)==2U);
    assert(reward.value_if()->coherence==0.0);
    assert(evaluator.metadata().identity!="g3-rl-trainable");

    const std::array<TokenId, 1> invalid_control_response{{morok::lang::kEosToken}};
    assert(!evaluator.evaluate(prompt, invalid_control_response).has_value());

    bool oversized_reference_rejected = false;
    try {
        ReferenceRewardEvaluator oversized({
            ReferenceAnswer{std::vector<TokenId>(prompt.begin(),prompt.end()),
                            std::vector<TokenId>(4097U,static_cast<TokenId>('X'))}});
    } catch (const std::invalid_argument&) {
        oversized_reference_rejected = true;
    }
    assert(oversized_reference_rejected);
}

} // namespace

int main() {
    test_config_validation();
    test_evaluator_audit_and_reward_hacking();
    test_short_correct_answer_is_not_length_penalized();
    test_missing_eos_is_penalized();
    test_training_requires_evaluator_audit();
    test_independent_rl_training_without_sft_reference();
    test_exact_kl_logit_gradient();
    test_reinforce_and_reference_kl();
    test_copy_and_justified_length_penalties();
    test_closed_set_comparison();
    test_adamw_rejects_nonfinite_rl_update_atomically();
    test_real_g3_policy_adapter_and_independent_evaluator();
    std::cout << "G3 REINFORCE tests passed.\n";
    return 0;
}
