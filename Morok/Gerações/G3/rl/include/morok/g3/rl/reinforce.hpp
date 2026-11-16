#pragma once

#include "morok/common/result.hpp"
#include "morok/lang/byte_tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace morok::g3::rl {

inline constexpr std::size_t kVocabularySize = morok::lang::kVocabularySize;

struct RewardComponents final {
    double correctness{0.0};
    double intent{0.0};
    double coherence{0.0};
    double format{0.0};
};

struct RewardWeights final {
    double correctness{0.70};
    double intent{0.15};
    double coherence{0.05};
    double format{0.10};
};

struct RewardPenalties final {
    double empty_response{0.80};
    double repeated_token_ratio{0.20};
    double prompt_copy_ratio{0.15};
    // Zero disables a length penalty. A nonzero limit must be rubric-justified.
    std::size_t max_response_tokens{0};
    double over_length{0.20};
};

struct Config final {
    std::size_t candidates_per_prompt{4};
    std::size_t max_response_tokens{128};
    std::size_t top_k{10};
    double temperature{0.7};
    double gamma{1.0};
    double kl_beta{0.02};
    double baseline_ema{0.10};
    double kl_epsilon{1.0e-12};
    std::uint64_t seed{42};
    RewardWeights reward_weights{};
    RewardPenalties penalties{};
};

struct Prompt final {
    std::string id;
    std::string category;
    std::vector<morok::lang::TokenId> tokens;
};

struct EvaluatorMetadata final {
    std::string identity;
    std::string rubric_version;
    std::string label_origin;
};

struct SequenceGradient;

class InferencePolicy {
public:
    virtual ~InferencePolicy() = default;
    [[nodiscard]] virtual std::string_view identity() const noexcept = 0;

    // Returns a normalized probability distribution over all 261 IDs.
    [[nodiscard]] virtual morok::common::Status next_token_probabilities(
        std::span<const morok::lang::TokenId> context,
        std::span<const morok::lang::TokenId> generated_prefix,
        std::span<float> probabilities) const = 0;
};

class Policy : public InferencePolicy {
public:
    // Applies one accumulated batch of d(loss)/d(logits), row-major [time, 261].
    // The trainable policy owns its weights and optimizer; this method must
    // perform one optimizer update for the whole batch, not per sequence.
    [[nodiscard]] virtual morok::common::Status apply_batch_gradients(
        std::span<const SequenceGradient> gradients) = 0;
};

struct SequenceGradient final {
    std::vector<morok::lang::TokenId> context;
    // Includes EOS when the sampled sequence ended naturally.
    std::vector<morok::lang::TokenId> response;
    // One full vocabulary row for every response token.
    std::vector<float> logit_gradients;
};

class RewardEvaluator {
public:
    virtual ~RewardEvaluator() = default;
    [[nodiscard]] virtual EvaluatorMetadata metadata() const = 0;
    [[nodiscard]] virtual morok::common::Result<RewardComponents> evaluate(
        std::span<const morok::lang::TokenId> prompt,
        std::span<const morok::lang::TokenId> response) const = 0;
};

struct RewardAuditCase final {
    std::string id;
    std::vector<morok::lang::TokenId> prompt;
    std::vector<morok::lang::TokenId> response;
    double minimum_score{0.0};
    double maximum_score{1.0};
};

struct CandidateMetrics final {
    std::string prompt_id;
    std::string category;
    RewardComponents components{};
    std::vector<morok::lang::TokenId> response_tokens_data;
    std::vector<double> sampled_token_log_probabilities;
    double reward{0.0};
    double advantage{0.0};
    double kl{0.0};
    std::size_t response_tokens{0};
    bool ended_with_eos{false};
};

struct TrainingReport final {
    Config config_snapshot{};
    EvaluatorMetadata evaluator{};
    std::size_t prompts{0};
    std::size_t candidates{0};
    std::size_t optimizer_updates{0};
    double mean_reward{0.0};
    double mean_kl{0.0};
    double mean_response_tokens{0.0};
    double empty_response_rate{0.0};
    double repeated_response_rate{0.0};
    std::vector<CandidateMetrics> candidates_metrics;
};

struct TrainerState final {
    std::uint32_t version{1};
    Config config{};
    std::unordered_map<std::string, double> category_baselines;
    std::string random_engine_state;
    std::string policy_identity;
    std::string reference_identity;
    EvaluatorMetadata evaluator{};
};

struct EvaluationReport final {
    std::size_t examples{0};
    double mean_reward{0.0};
    double mean_response_tokens{0.0};
    double empty_response_rate{0.0};
    double repeated_response_rate{0.0};
};

struct ComparisonReport final {
    EvaluationReport reference_sft;
    EvaluationReport reinforcement_policy;
    double reward_delta{0.0};
};

[[nodiscard]] morok::common::Status validate_config(const Config& config);
[[nodiscard]] morok::common::Status audit_evaluator(
    const RewardEvaluator& evaluator,
    std::string_view policy_identity,
    std::span<const RewardAuditCase> audit_cases,
    const Config& config = Config{});

class ReinforceTrainer final {
public:
    explicit ReinforceTrainer(Config config = {});

    [[nodiscard]] morok::common::Result<TrainingReport> train_batch(
        std::span<const Prompt> prompts,
        Policy& policy,
        const InferencePolicy& frozen_sft_reference,
        const RewardEvaluator& evaluator,
        std::span<const RewardAuditCase> evaluator_audit_cases);

    [[nodiscard]] double baseline(std::string_view category) const;
    [[nodiscard]] morok::common::Result<TrainerState> capture_state(
        std::string_view policy_identity,
        std::string_view reference_identity,
        const EvaluatorMetadata& evaluator) const;
    [[nodiscard]] morok::common::Status restore_state(
        const TrainerState& state,
        std::string_view expected_policy_identity,
        std::string_view expected_reference_identity,
        const EvaluatorMetadata& expected_evaluator);
    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    Config config_;
    std::mt19937_64 random_;
    std::unordered_map<std::string, double> baselines_;
};

[[nodiscard]] morok::common::Result<EvaluationReport> evaluate_policy(
    const InferencePolicy& policy,
    const RewardEvaluator& evaluator,
    std::span<const Prompt> examples,
    std::size_t max_response_tokens = 128);

[[nodiscard]] morok::common::Result<ComparisonReport> compare_sft_and_rl(
    const InferencePolicy& frozen_sft_reference,
    const InferencePolicy& reinforcement_policy,
    const RewardEvaluator& evaluator,
    std::span<const Prompt> closed_set,
    std::size_t max_response_tokens = 128);

} // namespace morok::g3::rl
