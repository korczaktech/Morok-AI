#include "morok/g3/rl/reinforce.hpp"

#include "morok/common/error.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>

namespace morok::g3::rl {
namespace {

using morok::common::Error;
using morok::common::ErrorCode;
using morok::common::Result;
using morok::common::Status;
using morok::lang::TokenId;

constexpr double kScoreMinimum = -1.0;
constexpr double kScoreMaximum = 1.0;

[[nodiscard]] Status failure(ErrorCode code, const char* stage,
                             const char* message) {
    return Status::failure(Error::make(code, message, "g3.rl", stage));
}

template <typename T>
[[nodiscard]] Result<T> failure_result(ErrorCode code, const char* stage,
                                        const char* message) {
    return Result<T>::failure(Error::make(code, message, "g3.rl", stage));
}

[[nodiscard]] bool valid_component(double value) noexcept {
    return std::isfinite(value) && value >= -1.0 && value <= 1.0;
}

[[nodiscard]] bool valid_weights(const RewardWeights& weights) noexcept {
    const double sum = weights.correctness + weights.intent +
                       weights.coherence + weights.format;
    return std::isfinite(sum) && weights.correctness >= 0.0 &&
           weights.intent >= 0.0 && weights.coherence >= 0.0 &&
           weights.format >= 0.0 && std::abs(sum - 1.0) <= 1.0e-9;
}

[[nodiscard]] bool valid_penalties(const RewardPenalties& penalties) noexcept {
    return std::isfinite(penalties.empty_response) &&
           std::isfinite(penalties.repeated_token_ratio) &&
           std::isfinite(penalties.prompt_copy_ratio) &&
           std::isfinite(penalties.over_length) &&
           penalties.empty_response >= 0.0 &&
           penalties.repeated_token_ratio >= 0.0 &&
           penalties.prompt_copy_ratio >= 0.0 &&
           penalties.over_length >= 0.0;
}

[[nodiscard]] double reward_score(const RewardComponents& components,
                                  const RewardWeights& weights) noexcept {
    return weights.correctness * components.correctness +
           weights.intent * components.intent +
           weights.coherence * components.coherence +
           weights.format * components.format;
}

[[nodiscard]] double repeated_ratio(std::span<const TokenId> tokens) noexcept {
    if (tokens.size() < 2U) {
        return 0.0;
    }
    std::size_t repeated = 0;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        if (tokens[i] == tokens[i - 1U]) {
            ++repeated;
        }
    }
    return static_cast<double>(repeated) /
           static_cast<double>(tokens.size() - 1U);
}

[[nodiscard]] double prompt_copy_ratio(std::span<const TokenId> prompt,
                                       std::span<const TokenId> response) {
    if (prompt.empty() || response.empty()) {
        return 0.0;
    }
    std::vector<bool> seen(morok::lang::kByteVocabularySize, false);
    for (const TokenId token : prompt) {
        if (token < morok::lang::kByteVocabularySize) {
            seen[token] = true;
        }
    }
    std::size_t copied = 0;
    for (const TokenId token : response) {
        if (token < morok::lang::kByteVocabularySize && seen[token]) {
            ++copied;
        }
    }
    return static_cast<double>(copied) /
           static_cast<double>(response.size());
}

[[nodiscard]] Status normalize_distribution(std::span<const float> source,
                                            std::vector<double>& output) {
    if (source.size() != kVocabularySize) {
        return failure(ErrorCode::invalid_argument, "distribution.shape",
                       "Policy distribution must contain exactly 261 values");
    }
    output.assign(kVocabularySize, 0.0);
    double sum = 0.0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const double value = source[i];
        if (!std::isfinite(value) || value < 0.0) {
            return failure(ErrorCode::invalid_argument, "distribution.value",
                           "Policy probabilities must be finite and nonnegative");
        }
        const bool allowed = i < morok::lang::kByteVocabularySize ||
                             i == morok::lang::kEosToken;
        if (allowed) {
            output[i] = value;
            sum += value;
        }
    }
    if (!std::isfinite(sum) || sum <= 0.0) {
        return failure(ErrorCode::invalid_argument, "distribution.sum",
                       "Policy assigns no probability to a valid response token");
    }
    for (double& value : output) {
        value /= sum;
    }
    return Status::success();
}

[[nodiscard]] std::vector<double> sampling_distribution(
    std::span<const double> probabilities, const Config& config) {
    std::vector<std::size_t> indices;
    indices.reserve(morok::lang::kByteVocabularySize + 1U);
    for (std::size_t i = 0; i < probabilities.size(); ++i) {
        if (probabilities[i] > 0.0) {
            indices.push_back(i);
        }
    }
    std::sort(indices.begin(), indices.end(),
              [&](std::size_t left, std::size_t right) {
                  return probabilities[left] > probabilities[right];
              });

    std::vector<double> result(probabilities.size(), 0.0);
    const std::size_t count = std::min(config.top_k, indices.size());
    double sum = 0.0;
    for (std::size_t rank = 0; rank < count; ++rank) {
        const std::size_t index = indices[rank];
        const double adjusted =
            std::pow(probabilities[index], 1.0 / config.temperature);
        result[index] = adjusted;
        sum += adjusted;
    }
    if (sum > 0.0) {
        for (double& value : result) {
            value /= sum;
        }
    }
    return result;
}

[[nodiscard]] double calculate_kl(std::span<const double> current,
                                  std::span<const double> reference,
                                  double epsilon) noexcept {
    double result = 0.0;
    for (std::size_t i = 0; i < current.size(); ++i) {
        const double p = current[i];
        if (p > 0.0) {
            result += p * std::log(p / std::max(reference[i], epsilon));
        }
    }
    return std::max(0.0, result);
}

struct Generated final {
    std::vector<TokenId> response;
    std::vector<std::vector<double>> current;
    std::vector<std::vector<double>> reference;
    std::vector<std::vector<double>> sampling;
    double kl{0.0};
    bool ended_with_eos{false};
};

[[nodiscard]] Result<Generated> generate_candidate(
    const Config& config, Policy& policy, const Policy& reference,
    std::span<const TokenId> prompt, std::mt19937_64& random) {
    Generated generated;
    generated.response.reserve(config.max_response_tokens);
    generated.current.reserve(config.max_response_tokens);
    generated.reference.reserve(config.max_response_tokens);
    generated.sampling.reserve(config.max_response_tokens);

    std::vector<float> current_buffer(kVocabularySize);
    std::vector<float> reference_buffer(kVocabularySize);
    for (std::size_t step = 0; step < config.max_response_tokens; ++step) {
        const auto current_status = policy.next_token_probabilities(
            prompt, generated.response, current_buffer);
        if (!current_status) {
            return Result<Generated>::failure(*current_status.error_if());
        }
        const auto reference_status = reference.next_token_probabilities(
            prompt, generated.response, reference_buffer);
        if (!reference_status) {
            return Result<Generated>::failure(*reference_status.error_if());
        }

        std::vector<double> current;
        std::vector<double> reference_distribution;
        auto status = normalize_distribution(current_buffer, current);
        if (!status) {
            return Result<Generated>::failure(*status.error_if());
        }
        status = normalize_distribution(reference_buffer, reference_distribution);
        if (!status) {
            return Result<Generated>::failure(*status.error_if());
        }

        auto sampling = sampling_distribution(current, config);
        if (std::accumulate(sampling.begin(), sampling.end(), 0.0) <= 0.0) {
            return failure_result<Generated>(
                ErrorCode::internal_error, "sampling.empty",
                "Top-K sampling produced an empty distribution");
        }

        std::discrete_distribution<std::size_t> choose(
            sampling.begin(), sampling.end());
        const std::size_t selected = choose(random);
        generated.kl += calculate_kl(current, reference_distribution,
                                     config.kl_epsilon);
        generated.current.push_back(std::move(current));
        generated.reference.push_back(std::move(reference_distribution));
        generated.sampling.push_back(std::move(sampling));
        generated.response.push_back(static_cast<TokenId>(selected));
        if (selected == morok::lang::kEosToken) {
            generated.ended_with_eos = true;
            break;
        }
    }
    return Result<Generated>::success(std::move(generated));
}

[[nodiscard]] std::span<const TokenId> visible_response(
    const std::vector<TokenId>& response) noexcept {
    if (!response.empty() && response.back() == morok::lang::kEosToken) {
        return std::span<const TokenId>(response.data(), response.size() - 1U);
    }
    return response;
}

[[nodiscard]] Result<double> score_response(
    const RewardEvaluator& evaluator, const RewardWeights& weights,
    const RewardPenalties& penalties, std::span<const TokenId> prompt,
    std::span<const TokenId> response) {
    const auto evaluated = evaluator.evaluate(prompt, response);
    if (!evaluated) {
        return Result<double>::failure(*evaluated.error_if());
    }
    const auto& components = *evaluated.value_if();
    if (!valid_component(components.correctness) ||
        !valid_component(components.intent) ||
        !valid_component(components.coherence) ||
        !valid_component(components.format)) {
        return failure_result<double>(
            ErrorCode::invalid_argument, "reward.range",
            "Evaluator components must be finite values in [-1, 1]");
    }

    double score = reward_score(components, weights);
    if (response.empty()) {
        score -= penalties.empty_response;
    }
    score -= penalties.repeated_token_ratio * repeated_ratio(response);
    score -= penalties.prompt_copy_ratio * prompt_copy_ratio(prompt, response);
    if (penalties.max_response_tokens > 0U &&
        response.size() > penalties.max_response_tokens) {
        score -= penalties.over_length;
    }
    return Result<double>::success(
        std::clamp(score, kScoreMinimum, kScoreMaximum));
}

[[nodiscard]] Result<std::vector<TokenId>> greedy_response(
    const Policy& policy, std::span<const TokenId> prompt,
    std::size_t max_tokens) {
    std::vector<TokenId> response;
    response.reserve(max_tokens);
    std::vector<float> buffer(kVocabularySize);
    for (std::size_t step = 0; step < max_tokens; ++step) {
        const auto status = policy.next_token_probabilities(
            prompt, response, buffer);
        if (!status) {
            return Result<std::vector<TokenId>>::failure(*status.error_if());
        }
        std::vector<double> probabilities;
        const auto normalized = normalize_distribution(buffer, probabilities);
        if (!normalized) {
            return Result<std::vector<TokenId>>::failure(*normalized.error_if());
        }
        const auto best = std::max_element(probabilities.begin(),
                                           probabilities.end());
        if (best == probabilities.end() || *best <= 0.0) {
            return failure_result<std::vector<TokenId>>(
                ErrorCode::internal_error, "evaluation.distribution",
                "Greedy evaluation found no valid token");
        }
        const auto selected = static_cast<TokenId>(
            std::distance(probabilities.begin(), best));
        response.push_back(selected);
        if (selected == morok::lang::kEosToken) {
            break;
        }
    }
    return Result<std::vector<TokenId>>::success(std::move(response));
}

} // namespace

Status validate_config(const Config& config) {
    if (config.candidates_per_prompt == 0U ||
        config.candidates_per_prompt > 4U ||
        config.max_response_tokens == 0U ||
        config.top_k == 0U ||
        !std::isfinite(config.temperature) || config.temperature <= 0.0 ||
        !std::isfinite(config.gamma) || config.gamma < 0.0 ||
        config.gamma > 1.0 ||
        !std::isfinite(config.kl_beta) || config.kl_beta < 0.0 ||
        !std::isfinite(config.baseline_ema) || config.baseline_ema <= 0.0 ||
        config.baseline_ema > 1.0 ||
        !std::isfinite(config.kl_epsilon) || config.kl_epsilon <= 0.0 ||
        !valid_weights(config.reward_weights) ||
        !valid_penalties(config.penalties)) {
        return failure(ErrorCode::invalid_argument, "config",
                       "Invalid REINFORCE configuration or reward weights");
    }
    return Status::success();
}

Status audit_evaluator(const RewardEvaluator& evaluator,
                       std::string_view policy_identity,
                       std::span<const RewardAuditCase> audit_cases) {
    const auto metadata = evaluator.metadata();
    if (metadata.identity.empty() || metadata.rubric_version.empty() ||
        metadata.label_origin.empty() || metadata.identity == policy_identity ||
        audit_cases.empty()) {
        return failure(ErrorCode::invalid_argument, "evaluator.metadata",
                       "Evaluator must be identified, versioned, independently sourced and audited");
    }
    const RewardWeights weights{};
    const RewardPenalties penalties{};
    for (const auto& test_case : audit_cases) {
        if (test_case.id.empty() || !std::isfinite(test_case.minimum_score) ||
            !std::isfinite(test_case.maximum_score) ||
            test_case.minimum_score < -1.0 || test_case.maximum_score > 1.0 ||
            test_case.minimum_score > test_case.maximum_score) {
            return failure(ErrorCode::invalid_argument, "evaluator.audit_case",
                           "Evaluator audit case has invalid metadata or score bounds");
        }
        const auto score = score_response(evaluator, weights, penalties,
                                          test_case.prompt, test_case.response);
        if (!score) {
            return Status::failure(*score.error_if());
        }
        if (*score.value_if() < test_case.minimum_score ||
            *score.value_if() > test_case.maximum_score) {
            return failure(ErrorCode::invalid_argument, "evaluator.audit_score",
                           "Evaluator output falls outside its human-audited score interval");
        }
    }
    return Status::success();
}

ReinforceTrainer::ReinforceTrainer(Config config)
    : config_(std::move(config)), random_(config_.seed) {}

double ReinforceTrainer::baseline(std::string_view category) const {
    const auto found = baselines_.find(std::string(category));
    return found == baselines_.end() ? 0.0 : found->second;
}

Result<TrainingReport> ReinforceTrainer::train_batch(
    std::span<const Prompt> prompts, Policy& policy,
    const Policy& frozen_sft_reference, const RewardEvaluator& evaluator) {
    const auto config_status = validate_config(config_);
    if (!config_status) {
        return Result<TrainingReport>::failure(*config_status.error_if());
    }
    if (prompts.empty() || policy.identity().empty() ||
        frozen_sft_reference.identity().empty() ||
        policy.identity() == frozen_sft_reference.identity()) {
        return failure_result<TrainingReport>(
            ErrorCode::invalid_argument, "train.identity",
            "Training requires prompts, a policy and a distinct frozen SFT reference");
    }
    const auto evaluator_status =
        audit_evaluator(evaluator, policy.identity(), std::array<RewardAuditCase, 1>{
            RewardAuditCase{"runtime-contract", {}, {}, -1.0, 1.0}});
    if (!evaluator_status) {
        return Result<TrainingReport>::failure(*evaluator_status.error_if());
    }

    TrainingReport report;
    report.prompts = prompts.size();
    std::vector<SequenceGradient> batch_gradients;
    std::unordered_map<std::string, std::pair<double, std::size_t>> category_rewards;
    double total_reward = 0.0;
    double total_kl = 0.0;
    double total_tokens = 0.0;
    std::size_t empty_count = 0;
    std::size_t repeated_count = 0;

    for (const auto& prompt : prompts) {
        if (prompt.id.empty() || prompt.category.empty() || prompt.tokens.empty()) {
            return failure_result<TrainingReport>(
                ErrorCode::invalid_argument, "train.prompt",
                "Every prompt needs an ID, category and encoded context");
        }
        const double category_baseline = baseline(prompt.category);
        for (std::size_t candidate_index = 0;
             candidate_index < config_.candidates_per_prompt;
             ++candidate_index) {
            auto generated_result = generate_candidate(
                config_, policy, frozen_sft_reference, prompt.tokens, random_);
            if (!generated_result) {
                return Result<TrainingReport>::failure(
                    *generated_result.error_if());
            }
            auto generated = std::move(*generated_result.value_if());
            const auto visible = visible_response(generated.response);
            const auto reward_result = score_response(
                evaluator, config_.reward_weights, config_.penalties,
                prompt.tokens, visible);
            if (!reward_result) {
                return Result<TrainingReport>::failure(*reward_result.error_if());
            }
            const double reward = *reward_result.value_if();
            const double advantage = reward - category_baseline;

            SequenceGradient gradient;
            gradient.context = prompt.tokens;
            gradient.response = generated.response;
            gradient.logit_gradients.resize(
                generated.response.size() * kVocabularySize, 0.0F);
            for (std::size_t step = 0; step < generated.response.size(); ++step) {
                const double temporal_weight =
                    std::pow(config_.gamma, static_cast<double>(step));
                const TokenId selected = generated.response[step];
                const auto& sample_probabilities = generated.sampling[step];
                const auto& current_probabilities = generated.current[step];
                const auto& reference_probabilities = generated.reference[step];
                for (std::size_t token = 0; token < kVocabularySize; ++token) {
                    const double indicator = token == selected ? 1.0 : 0.0;
                    const double reinforce_gradient =
                        advantage * temporal_weight *
                        (sample_probabilities[token] - indicator) /
                        config_.temperature;
                    const double kl_gradient = config_.kl_beta *
                        (current_probabilities[token] -
                         reference_probabilities[token]);
                    gradient.logit_gradients[
                        step * kVocabularySize + token] =
                        static_cast<float>(reinforce_gradient + kl_gradient);
                }
            }
            batch_gradients.push_back(std::move(gradient));

            ++report.candidates;
            total_reward += reward;
            total_kl += generated.kl /
                        static_cast<double>(std::max<std::size_t>(
                            generated.response.size(), 1U));
            total_tokens += static_cast<double>(visible.size());
            if (visible.empty()) {
                ++empty_count;
            }
            if (repeated_ratio(visible) > 0.0) {
                ++repeated_count;
            }
            report.candidates_metrics.push_back(CandidateMetrics{
                reward, advantage,
                generated.kl / static_cast<double>(
                    std::max<std::size_t>(generated.response.size(), 1U)),
                visible.size(), generated.ended_with_eos});
            auto& category = category_rewards[prompt.category];
            category.first += reward;
            ++category.second;
        }
    }

    const auto apply_status = policy.apply_batch_gradients(batch_gradients);
    if (!apply_status) {
        return Result<TrainingReport>::failure(*apply_status.error_if());
    }

    for (const auto& [category, aggregate] : category_rewards) {
        const double batch_mean = aggregate.first /
                                  static_cast<double>(aggregate.second);
        const auto found = baselines_.find(category);
        if (found == baselines_.end()) {
            baselines_.emplace(category, batch_mean);
        } else {
            found->second = (1.0 - config_.baseline_ema) * found->second +
                            config_.baseline_ema * batch_mean;
        }
    }

    if (report.candidates > 0U) {
        const double count = static_cast<double>(report.candidates);
        report.mean_reward = total_reward / count;
        report.mean_kl = total_kl / count;
        report.mean_response_tokens = total_tokens / count;
        report.empty_response_rate = static_cast<double>(empty_count) / count;
        report.repeated_response_rate =
            static_cast<double>(repeated_count) / count;
        report.optimizer_updates = 1U;
    }
    return Result<TrainingReport>::success(std::move(report));
}

Result<EvaluationReport> evaluate_policy(
    const Policy& policy, const RewardEvaluator& evaluator,
    std::span<const Prompt> examples, std::size_t max_response_tokens) {
    if (examples.empty() || max_response_tokens == 0U ||
        policy.identity().empty()) {
        return failure_result<EvaluationReport>(
            ErrorCode::invalid_argument, "evaluation.input",
            "Evaluation requires a policy, examples and a positive token limit");
    }
    const auto metadata = evaluator.metadata();
    if (metadata.identity.empty() || metadata.rubric_version.empty() ||
        metadata.label_origin.empty() || metadata.identity == policy.identity()) {
        return failure_result<EvaluationReport>(
            ErrorCode::invalid_argument, "evaluation.evaluator",
            "Evaluation requires an independent, versioned evaluator");
    }

    EvaluationReport report;
    report.examples = examples.size();
    double total_reward = 0.0;
    double total_tokens = 0.0;
    std::size_t empty = 0;
    std::size_t repeated = 0;
    const RewardWeights weights{};
    const RewardPenalties penalties{};
    for (const auto& example : examples) {
        const auto generated = greedy_response(
            policy, example.tokens, max_response_tokens);
        if (!generated) {
            return Result<EvaluationReport>::failure(*generated.error_if());
        }
        const auto visible = visible_response(*generated.value_if());
        const auto score = score_response(
            evaluator, weights, penalties, example.tokens, visible);
        if (!score) {
            return Result<EvaluationReport>::failure(*score.error_if());
        }
        total_reward += *score.value_if();
        total_tokens += static_cast<double>(visible.size());
        empty += visible.empty() ? 1U : 0U;
        repeated += repeated_ratio(visible) > 0.0 ? 1U : 0U;
    }
    const double count = static_cast<double>(examples.size());
    report.mean_reward = total_reward / count;
    report.mean_response_tokens = total_tokens / count;
    report.empty_response_rate = static_cast<double>(empty) / count;
    report.repeated_response_rate = static_cast<double>(repeated) / count;
    return Result<EvaluationReport>::success(report);
}

Result<ComparisonReport> compare_sft_and_rl(
    const Policy& frozen_sft_reference, const Policy& reinforcement_policy,
    const RewardEvaluator& evaluator, std::span<const Prompt> closed_set,
    std::size_t max_response_tokens) {
    if (frozen_sft_reference.identity() == reinforcement_policy.identity()) {
        return failure_result<ComparisonReport>(
            ErrorCode::invalid_argument, "comparison.identity",
            "SFT and RL policies must be distinct model snapshots");
    }
    const auto sft = evaluate_policy(frozen_sft_reference, evaluator, closed_set,
                                     max_response_tokens);
    if (!sft) {
        return Result<ComparisonReport>::failure(*sft.error_if());
    }
    const auto rl = evaluate_policy(reinforcement_policy, evaluator, closed_set,
                                    max_response_tokens);
    if (!rl) {
        return Result<ComparisonReport>::failure(*rl.error_if());
    }
    return Result<ComparisonReport>::success(ComparisonReport{
        *sft.value_if(), *rl.value_if(),
        rl.value_if()->mean_reward - sft.value_if()->mean_reward});
}

} // namespace morok::g3::rl
