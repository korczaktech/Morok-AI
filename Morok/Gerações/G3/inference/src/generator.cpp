#include "morok/g3/inference/generator.hpp"

#include "morok/common/error.hpp"
#include "morok/model/model_session.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace morok::g3::inference {
namespace {

using TokenId = lang::TokenId;
constexpr TokenId kNoCandidate = std::numeric_limits<TokenId>::max();

template <typename T>
common::Result<T> failure(common::ErrorCode code, const char* stage,
                           const char* message) {
    return common::Result<T>::failure(
        common::Error::make(code, message, "g3.inference", stage));
}

bool would_repeat(std::span<const TokenId> history, TokenId candidate,
                  std::size_t minimum, std::size_t maximum) noexcept {
    const std::size_t total = history.size() + 1U;
    const std::size_t upper = std::min(maximum, total / 2U);
    for (std::size_t n = minimum; n <= upper; ++n) {
        bool equal = true;
        // Compare the candidate-terminated suffix with the immediately
        // preceding n-byte sequence. No temporary buffers are allocated.
        for (std::size_t offset = 0; offset < n; ++offset) {
            const std::size_t current_index = total - n + offset;
            const TokenId current = current_index == history.size()
                ? candidate : history[current_index];
            const std::size_t previous_index = total - 2U * n + offset;
            if (current != history[previous_index]) {
                equal = false;
                break;
            }
        }
        if (equal) return true;
    }
    return false;
}

common::Result<TokenId> sample_next(
    std::span<const float> log_probabilities,
    std::span<const TokenId> history,
    const GenerationConfig& config,
    std::mt19937_64& random) {
    if (log_probabilities.size() != lang::kVocabularySize) {
        return failure<TokenId>(common::ErrorCode::invalid_argument,
                                "decode.vocabulary",
                                "Backend output does not match the G3 vocabulary");
    }

    std::array<std::pair<double, TokenId>, lang::kVocabularySize> candidates{};
    std::size_t candidate_count = 0U;
    for (std::size_t index = 0; index < log_probabilities.size(); ++index) {
        const auto token = static_cast<TokenId>(index);
        const double value = log_probabilities[index];
        if (token == lang::kPadToken || token == lang::kBosToken ||
            token == lang::kUsrToken || token == lang::kBotToken ||
            (std::isinf(value) && value < 0.0)) {
            continue;
        }
        if (!std::isfinite(value)) {
            return failure<TokenId>(common::ErrorCode::internal_error,
                                    "decode.non_finite",
                                    "Backend returned NaN or positive infinity");
        }
        if (lang::ByteTokenizer::is_byte_token(token) &&
            would_repeat(history, token, config.repetition_ngram_min,
                         config.repetition_ngram_max)) {
            continue;
        }
        candidates[candidate_count++] = {value / config.temperature, token};
    }
    if (candidate_count == 0U) {
        return common::Result<TokenId>::success(kNoCandidate);
    }

    const std::size_t selected_count = config.top_k > 0U
        ? std::min(config.top_k, candidate_count) : candidate_count;
    std::partial_sort(candidates.begin(),
                      candidates.begin() + static_cast<std::ptrdiff_t>(selected_count),
                      candidates.begin() + static_cast<std::ptrdiff_t>(candidate_count),
        [](const auto& left, const auto& right) {
            return left.first > right.first;
        });

    const double maximum = candidates[0].first;
    std::array<double, lang::kVocabularySize> weights{};
    double total = 0.0;
    for (std::size_t i = 0; i < selected_count; ++i) {
        const double weight = std::exp(candidates[i].first - maximum);
        weights[i] = weight;
        total += weight;
    }
    if (!std::isfinite(total) || total <= 0.0) {
        return failure<TokenId>(common::ErrorCode::internal_error,
                                "decode.softmax",
                                "Stable softmax produced an invalid normalizer");
    }

    const double threshold =
        std::generate_canonical<double, 53>(random) * total;
    double cumulative = 0.0;
    for (std::size_t i = 0; i < selected_count; ++i) {
        cumulative += weights[i];
        if (threshold < cumulative)
            return common::Result<TokenId>::success(candidates[i].second);
    }
    return common::Result<TokenId>::success(candidates[selected_count - 1U].second);
}

} // namespace

common::Result<GenerationResult> generate(
    const model::ModelBackend& backend,
    std::span<std::byte> session_storage,
    std::string_view input,
    const GenerationConfig& config,
    std::mt19937_64& random,
    const std::function<bool()>& cancellation_requested) {
    if (!std::isfinite(config.temperature) || config.temperature <= 0.0 ||
        config.max_input_bytes == 0U || config.max_output_bytes == 0U ||
        config.timeout.count() < 0 ||
        config.top_k > lang::kVocabularySize ||
        config.repetition_ngram_min < 3U ||
        config.repetition_ngram_max < config.repetition_ngram_min ||
        config.repetition_ngram_max > 6U) {
        return failure<GenerationResult>(
            common::ErrorCode::invalid_argument, "generation.config",
            "Generation configuration contains invalid sampling, repetition or size limits");
    }

    const auto started = std::chrono::steady_clock::now();
    const auto deadline = config.timeout.count() == 0
        ? std::chrono::steady_clock::time_point::max()
        : started + config.timeout;

    const auto encoded = lang::ByteTokenizer::encode_text(input, config.text_codec);
    if (!encoded) return common::Result<GenerationResult>::failure(*encoded.error_if());
    if (encoded.value_if()->byte_tokens.size() > config.max_input_bytes) {
        return failure<GenerationResult>(
            common::ErrorCode::input_too_large, "generation.input_limit",
            "Normalized input exceeds the configured byte limit");
    }

    model::ModelSession session(backend, session_storage);
    const auto initialized = config.reset_session_state
        ? session.reset() : session.resume_existing_state();
    if (!initialized)
        return common::Result<GenerationResult>::failure(*initialized.error_if());

    for (const auto token : encoded.value_if()->byte_tokens) {
        const auto consumed = session.consume_byte(static_cast<std::uint8_t>(token));
        if (!consumed)
            return common::Result<GenerationResult>::failure(*consumed.error_if());
    }

    GenerationResult result;
    result.input_bytes = encoded.value_if()->byte_tokens.size();
    result.generated_tokens.reserve(config.max_output_bytes);
    std::array<float, lang::kVocabularySize> log_probabilities{};

    while (result.output_bytes < config.max_output_bytes) {
        if (cancellation_requested && cancellation_requested())
            return failure<GenerationResult>(common::ErrorCode::cancelled,
                "generation.cancelled", "Generation was cancelled by the caller");
        if (std::chrono::steady_clock::now() >= deadline)
            return failure<GenerationResult>(common::ErrorCode::tool_timeout,
                "generation.timeout", "Generation exceeded its configured timeout");

        const auto predicted = session.next_token_log_probabilities(log_probabilities);
        if (!predicted)
            return common::Result<GenerationResult>::failure(*predicted.error_if());
        const auto selected = sample_next(log_probabilities, result.generated_tokens,
                                          config, random);
        if (!selected)
            return common::Result<GenerationResult>::failure(*selected.error_if());
        const TokenId token = *selected.value_if();
        if (token == kNoCandidate) {
            result.repetition_detected = true;
            break;
        }
        if (token == lang::kEosToken) {
            result.stopped_on_eos = true;
            break;
        }
        if (!lang::ByteTokenizer::is_byte_token(token))
            return failure<GenerationResult>(common::ErrorCode::internal_error,
                "generation.control_token",
                "A control token other than EOS escaped the decoder filter");

        result.generated_tokens.push_back(token);
        ++result.output_bytes;
        const auto consumed = session.consume_byte(static_cast<std::uint8_t>(token));
        if (!consumed)
            return common::Result<GenerationResult>::failure(*consumed.error_if());
    }

    result.reached_output_limit = !result.stopped_on_eos &&
        !result.repetition_detected && result.output_bytes == config.max_output_bytes;
    if (result.generated_tokens.empty() &&
        config.empty_response == EmptyResponsePolicy::reject)
        return failure<GenerationResult>(common::ErrorCode::internal_error,
            "generation.empty", "The model produced an empty response and policy rejects it");

    const auto decoded = lang::ByteTokenizer::decode_text(
        result.generated_tokens, config.text_codec, config.max_output_bytes);
    if (!decoded)
        return common::Result<GenerationResult>::failure(*decoded.error_if());
    result.text = decoded.value_if()->text;
    result.replaced_invalid_utf8 = decoded.value_if()->replaced_invalid_utf8;
    result.output_bytes = result.text.size();
    result.elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started);
    return common::Result<GenerationResult>::success(std::move(result));
}

} // namespace morok::g3::inference
