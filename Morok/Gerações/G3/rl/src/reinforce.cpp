#include "morok/g3/rl/reinforce.hpp"

#include "morok/common/error.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
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

[[nodiscard]] bool same_config(const Config& left,
                                 const Config& right) noexcept {
    return left.candidates_per_prompt == right.candidates_per_prompt &&
           left.max_response_tokens == right.max_response_tokens &&
           left.top_k == right.top_k &&
           left.temperature == right.temperature &&
           left.gamma == right.gamma && left.kl_beta == right.kl_beta &&
           left.baseline_ema == right.baseline_ema &&
           left.kl_epsilon == right.kl_epsilon && left.seed == right.seed &&
           left.reward_weights.correctness == right.reward_weights.correctness &&
           left.reward_weights.intent == right.reward_weights.intent &&
           left.reward_weights.coherence == right.reward_weights.coherence &&
           left.reward_weights.format == right.reward_weights.format &&
           left.penalties.empty_response == right.penalties.empty_response &&
           left.penalties.repeated_token_ratio == right.penalties.repeated_token_ratio &&
           left.penalties.prompt_copy_ratio == right.penalties.prompt_copy_ratio &&
           left.penalties.max_response_tokens == right.penalties.max_response_tokens &&
           left.penalties.over_length == right.penalties.over_length;
}

[[nodiscard]] bool same_evaluator(const EvaluatorMetadata& left,
                                  const EvaluatorMetadata& right) noexcept {
    return left.identity == right.identity &&
           left.rubric_version == right.rubric_version &&
           left.label_origin == right.label_origin;
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
    std::vector<std::size_t> previous(response.size() + 1U, 0U);
    std::vector<std::size_t> current(response.size() + 1U, 0U);
    std::size_t longest_copy = 0U;
    for (const TokenId prompt_token : prompt) {
        std::fill(current.begin(), current.end(), 0U);
        if (prompt_token >= morok::lang::kByteVocabularySize) {
            previous.swap(current);
            continue;
        }
        for (std::size_t index = 1U; index <= response.size(); ++index) {
            if (response[index - 1U] == prompt_token) {
                current[index] = previous[index - 1U] + 1U;
                longest_copy = std::max(longest_copy, current[index]);
            }
        }
        previous.swap(current);
    }
    return static_cast<double>(longest_copy) /
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
    const Config& config, Policy& policy, const InferencePolicy& reference,
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

struct ScoredReward final {
    RewardComponents components;
    double score{0.0};
};

[[nodiscard]] Result<ScoredReward> score_response(
    const RewardEvaluator& evaluator, const RewardWeights& weights,
    const RewardPenalties& penalties, std::span<const TokenId> prompt,
    std::span<const TokenId> response) {
    const auto evaluated = evaluator.evaluate(prompt, response);
    if (!evaluated) {
        return Result<ScoredReward>::failure(*evaluated.error_if());
    }
    const auto& components = *evaluated.value_if();
    if (!valid_component(components.correctness) ||
        !valid_component(components.intent) ||
        !valid_component(components.coherence) ||
        !valid_component(components.format)) {
        return failure_result<ScoredReward>(
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
    return Result<ScoredReward>::success(ScoredReward{
        components, std::clamp(score, kScoreMinimum, kScoreMaximum)});
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
        if (score.value_if()->score < test_case.minimum_score ||
            score.value_if()->score > test_case.maximum_score) {
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

Result<TrainerState> ReinforceTrainer::capture_state(
    std::string_view policy_identity, std::string_view reference_identity,
    const EvaluatorMetadata& evaluator) const {
    if (policy_identity.empty() || reference_identity.empty() ||
        policy_identity == reference_identity || evaluator.identity.empty() ||
        evaluator.rubric_version.empty() || evaluator.label_origin.empty() ||
        evaluator.identity == policy_identity ||
        evaluator.identity == reference_identity) {
        return failure_result<TrainerState>(
            ErrorCode::invalid_argument, "checkpoint.identity",
            "Trainer state requires distinct policy, reference and evaluator identities");
    }
    std::ostringstream random_state;
    random_state << random_;
    if (!random_state.good()) {
        return failure_result<TrainerState>(
            ErrorCode::internal_error, "checkpoint.random",
            "Unable to serialize REINFORCE random engine state");
    }
    for (const auto& [category, value] : baselines_) {
        if (category.empty() || !std::isfinite(value) ||
            value < kScoreMinimum || value > kScoreMaximum) {
            return failure_result<TrainerState>(
                ErrorCode::internal_error, "checkpoint.baseline",
                "REINFORCE baseline state contains an invalid value");
        }
    }
    TrainerState state;
    state.config = config_;
    state.category_baselines = baselines_;
    state.random_engine_state = random_state.str();
    state.policy_identity = std::string(policy_identity);
    state.reference_identity = std::string(reference_identity);
    state.evaluator = evaluator;
    return Result<TrainerState>::success(std::move(state));
}

Status ReinforceTrainer::restore_state(
    const TrainerState& state, std::string_view expected_policy_identity,
    std::string_view expected_reference_identity,
    const EvaluatorMetadata& expected_evaluator) {
    if (state.version != 1U || !validate_config(state.config) ||
        !same_config(state.config, config_) ||
        state.policy_identity != expected_policy_identity ||
        state.reference_identity != expected_reference_identity ||
        !same_evaluator(state.evaluator, expected_evaluator) ||
        state.policy_identity.empty() || state.reference_identity.empty() ||
        state.policy_identity == state.reference_identity ||
        state.evaluator.identity.empty() ||
        state.evaluator.rubric_version.empty() ||
        state.evaluator.label_origin.empty()) {
        return failure(ErrorCode::model_version_mismatch, "checkpoint.metadata",
                       "REINFORCE checkpoint does not match the active policy, reference, evaluator or config");
    }
    for (const auto& [category, value] : state.category_baselines) {
        if (category.empty() || !std::isfinite(value) ||
            value < kScoreMinimum || value > kScoreMaximum) {
            return failure(ErrorCode::checkpoint_corrupt, "checkpoint.baseline",
                           "REINFORCE checkpoint contains an invalid category baseline");
        }
    }
    std::mt19937_64 restored_random;
    std::istringstream random_state(state.random_engine_state);
    random_state >> restored_random;
    if (random_state.fail()) {
        return failure(ErrorCode::checkpoint_corrupt, "checkpoint.random",
                       "REINFORCE checkpoint random engine state is malformed");
    }
    random_state >> std::ws;
    if (!random_state.eof()) {
        return failure(ErrorCode::checkpoint_corrupt, "checkpoint.random",
                       "REINFORCE checkpoint random engine state has trailing data");
    }
    baselines_ = state.category_baselines;
    random_ = restored_random;
    return Status::success();
}

Result<TrainingReport> ReinforceTrainer::train_batch(
    std::span<const Prompt> prompts, Policy& policy,
    const InferencePolicy& frozen_sft_reference, const RewardEvaluator& evaluator) {
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
    const auto evaluator_metadata = evaluator.metadata();
    if (evaluator_metadata.identity.empty() ||
        evaluator_metadata.rubric_version.empty() ||
        evaluator_metadata.label_origin.empty() ||
        evaluator_metadata.identity == policy.identity() ||
        evaluator_metadata.identity == frozen_sft_reference.identity()) {
        return failure_result<TrainingReport>(
            ErrorCode::invalid_argument, "train.evaluator",
            "Training requires an identified, versioned and independent evaluator");
    }

    TrainingReport report;
    report.config_snapshot = config_;
    report.evaluator = evaluator_metadata;
    report.prompts = prompts.size();
    std::vector<SequenceGradient> batch_gradients;
    std::unordered_map<std::string, std::pair<double, std::size_t>> category_rewards;
    double total_reward = 0.0;
    double total_kl = 0.0;
    double total_tokens = 0.0;
    std::size_t empty_count = 0;
    std::size_t repeated_count = 0;

    for (const auto& prompt : prompts) {
        if (prompt.id.empty() || prompt.category.empty() || prompt.tokens.empty() ||
            std::any_of(prompt.tokens.begin(), prompt.tokens.end(),
                [](TokenId token) { return !morok::lang::ByteTokenizer::is_valid_token(token); })) {
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
            const double reward = reward_result.value_if()->score;
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
            std::vector<double> sampled_log_probabilities;
            sampled_log_probabilities.reserve(generated.response.size());
            for (std::size_t step = 0; step < generated.response.size(); ++step) {
                const TokenId selected = generated.response[step];
                sampled_log_probabilities.push_back(std::log(std::max(
                    generated.sampling[step][selected], config_.kl_epsilon)));
            }
            report.candidates_metrics.push_back(CandidateMetrics{
                prompt.id, prompt.category, reward_result.value_if()->components,
                generated.response, std::move(sampled_log_probabilities),
                reward, advantage,
                generated.kl / static_cast<double>(
                    std::max<std::size_t>(generated.response.size(), 1U)),
                visible.size(), generated.ended_with_eos});
            auto& category = category_rewards[prompt.category];
            category.first += reward;
            ++category.second;
        }
    }

    const float batch_scale = 1.0F /
        static_cast<float>(std::max<std::size_t>(batch_gradients.size(), 1U));
    for (auto& sequence : batch_gradients) {
        for (float& gradient : sequence.logit_gradients) {
            gradient *= batch_scale;
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
    const InferencePolicy& policy, const RewardEvaluator& evaluator,
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
        total_reward += score.value_if()->score;
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
    const InferencePolicy& frozen_sft_reference, const InferencePolicy& reinforcement_policy,
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
