#include "morok/g2/tsukuyomi/model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <numeric>
#include <string>
#include <utility>

namespace morok::g2::tsukuyomi {
namespace {

constexpr std::size_t kEmbeddingOffset = 0;
constexpr std::size_t kHiddenWeightOffset = 8192;
constexpr std::size_t kContextWeightOffset = 9216;
constexpr std::size_t kHiddenBiasOffset = 9632;
constexpr std::size_t kOutputBiasOffset = 9664;
constexpr std::size_t kScaleOffset = 9920;
constexpr std::size_t kShiftOffset = 9952;
constexpr std::size_t kStartOffset = 9984;
constexpr std::size_t kEmbeddingWeightEnd = 8192;
constexpr std::size_t kHiddenWeightEnd = 9216;
constexpr std::size_t kContextWeightEnd = 9632;

// Inventário fechado: qualquer alteração de dimensão precisa atualizar
// explicitamente os offsets e continuar dentro do orçamento de 10.000 pesos.
static_assert(kEmbeddingWeightEnd == kVocabularySize * kHiddenSize);
static_assert(kHiddenWeightOffset == kEmbeddingOffset + kEmbeddingWeightEnd);
static_assert(kHiddenWeightEnd == kHiddenWeightOffset + kHiddenSize * kHiddenSize);
static_assert(kContextWeightOffset == kHiddenWeightEnd);
static_assert(kContextWeightEnd == kContextWeightOffset + kContextSize * kHiddenSize);
static_assert(kHiddenBiasOffset == kContextWeightEnd);
static_assert(kOutputBiasOffset == kHiddenBiasOffset + kHiddenSize);
static_assert(kScaleOffset == kOutputBiasOffset + kVocabularySize);
static_assert(kShiftOffset == kScaleOffset + kHiddenSize);
static_assert(kStartOffset == kShiftOffset + kHiddenSize);
static_assert(kStartOffset + 16 == kParameterCount);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

float at(const Model& model, std::size_t offset) {
    return model.parameters[offset];
}

struct LinguisticFeatures {
    // Protótipos em bytes inválidos para UTF-8: não colidem com texto válido.
    std::uint8_t category{0xFB};
    std::uint8_t boundary{0xFF};
};

bool ascii_letter(std::uint8_t byte) {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}
bool word_byte(std::uint8_t byte) {
    return ascii_letter(byte) || (byte >= '0' && byte <= '9') ||
           (byte >= 0x80 && byte <= 0xBF) ||
           (byte >= 0xC2 && byte <= 0xF4);
}
std::size_t question_position_bucket(std::size_t position,
                                    std::size_t begin, std::size_t end) {
    const std::size_t length = end - begin;
    if (length == 0 || position < begin || position >= end) return 0;
    return std::min(kContextSize - 1, ((position - begin) * kContextSize) / length);
}

bool reserved_feature_byte(std::uint8_t byte) {
    return byte == 0xC0 || byte == 0xC1 || byte >= 0xF5;
}

LinguisticFeatures linguistic_features(std::uint8_t byte, std::uint8_t previous) {
    LinguisticFeatures result;
    if (byte >= 'A' && byte <= 'Z') result.category = 0xC0;
    else if (byte >= 'a' && byte <= 'z') result.category = 0xC1;
    else if (byte >= '0' && byte <= '9') result.category = 0xF5;
    else if (byte == ' ' || byte == '\t') result.category = 0xF6;
    else if (byte == '\n' || byte == '\r') result.category = 0xF7;
    else if (byte == '.' || byte == '?' || byte == '!') result.category = 0xF8;
    else if (byte >= 0x80 && byte <= 0xBF) result.category = 0xFB;
    else if (byte >= 0xC2 && byte <= 0xF4) result.category = 0xFA;
    else result.category = 0xF9;

    const bool current_word = word_byte(byte);
    const bool previous_word = word_byte(previous);
    if (current_word && !previous_word) result.boundary = 0xFC;
    else if (current_word && previous_word) result.boundary = 0xFD;
    else if (!current_word && previous_word) result.boundary = 0xFE;
    else result.boundary = 0xFF;
    return result;
}

struct QuestionFeatures {
    std::array<float, kContextSize * kHiddenSize> values{};
    std::array<std::size_t, kContextSize> counts{};
    std::size_t begin{0}, end{0};
    bool available{false};
};

QuestionFeatures prepare_question_features(
    const Model& model, std::span<const std::uint8_t> context) {
    QuestionFeatures result;
    result.begin = result.end = context.size();
    for (std::size_t line = 0; line < context.size();) {
        std::size_t end = line;
        while (end < context.size() && context[end] != '\n' && context[end] != '\r') ++end;
        std::size_t marker = line;
        while (marker < end && (context[marker] == ' ' || context[marker] == '\t')) ++marker;
        if (end - marker >= 2 && context[marker] == 'U' && context[marker + 1] == ':') {
            result.begin = marker + 2;
            result.end = end;
        }
        line = end;
        while (line < context.size() && (context[line] == '\n' || context[line] == '\r')) ++line;
    }
    if (result.begin >= result.end) return result;
    result.available = true;
    for (std::size_t position = result.begin; position < result.end; ++position) {
        const std::size_t bucket = question_position_bucket(position, result.begin, result.end);
        ++result.counts[bucket];
        const auto base = kEmbeddingOffset + static_cast<std::size_t>(context[position]) * kHiddenSize;
        const auto features = linguistic_features(context[position],
            position == 0 ? static_cast<std::uint8_t>(' ') : context[position - 1]);
        const auto category_base = kEmbeddingOffset +
            static_cast<std::size_t>(features.category) * kHiddenSize;
        const auto boundary_base = kEmbeddingOffset +
            static_cast<std::size_t>(features.boundary) * kHiddenSize;
        for (std::size_t h = 0; h < kHiddenSize; ++h)
            result.values[bucket * kHiddenSize + h] +=
                at(model, base + h) + at(model, category_base + h) +
                at(model, boundary_base + h);
    }
    for (std::size_t bucket = 0; bucket < kContextSize; ++bucket) {
        if (result.counts[bucket] == 0) continue;
        const float inverse = 1.0F / static_cast<float>(result.counts[bucket]);
        for (std::size_t h = 0; h < kHiddenSize; ++h)
            result.values[bucket * kHiddenSize + h] *= inverse;
    }
    return result;
}

std::array<float, kHiddenSize> initial_recurrent_state(
    const Model& model, const QuestionFeatures& question) {
    std::array<float, kHiddenSize> state{};
    for (std::size_t h = 0; h < kHiddenSize; ++h) {
        float value = at(model, kHiddenBiasOffset + h);
        if (h < 16) value += at(model, kStartOffset + h);
        if (question.available) {
            for (std::size_t bucket = 0; bucket < kContextSize; ++bucket)
                value += question.values[bucket * kHiddenSize + h] *
                    at(model, kContextWeightOffset + bucket * kHiddenSize + h);
        }
        state[h] = std::tanh(value);
    }
    return state;
}

std::array<float, kHiddenSize> advance_recurrent_state(
    const Model& model, const std::array<float, kHiddenSize>& previous,
    std::uint8_t byte, std::uint8_t previous_byte) {
    std::array<float, kHiddenSize> next{};
    const auto embedding_base = kEmbeddingOffset +
        static_cast<std::size_t>(byte) * kHiddenSize;
    const auto features = linguistic_features(byte, previous_byte);
    const auto category_base = kEmbeddingOffset +
        static_cast<std::size_t>(features.category) * kHiddenSize;
    const auto boundary_base = kEmbeddingOffset +
        static_cast<std::size_t>(features.boundary) * kHiddenSize;
    for (std::size_t h = 0; h < kHiddenSize; ++h) {
        float value = at(model, kHiddenBiasOffset + h);
        for (std::size_t k = 0; k < kHiddenSize; ++k)
            value += at(model, kHiddenWeightOffset + h * kHiddenSize + k) *
                (at(model, embedding_base + k) + at(model, category_base + k) +
                 at(model, boundary_base + k) + previous[k]);
        const float candidate = std::tanh(value);
        const float retention = 1.0F / (1.0F + std::exp(-value));
        next[h] = retention * previous[h] +
                  (1.0F - retention) * candidate;
    }
    return next;
}

std::array<float, kVocabularySize> logits_for_state(
    const Model& model, const std::array<float, kHiddenSize>& state) {
    std::array<float, kVocabularySize> logits{};
    for (std::size_t token = 0; token < kVocabularySize; ++token) {
        float logit = at(model, kOutputBiasOffset + token);
        const auto base = kEmbeddingOffset + token * kHiddenSize;
        for (std::size_t h = 0; h < kHiddenSize; ++h) {
            const float hidden = at(model, kScaleOffset + h) * state[h] +
                                at(model, kShiftOffset + h);
            logit += at(model, base + h) * hidden;
        }
        logits[token] = logit;
    }
    return logits;
}

struct RecurrentStep {
    std::array<float, kHiddenSize> previous_state{};
    std::array<float, kHiddenSize> activation{};
    std::array<float, kHiddenSize> candidate{};
    std::array<float, kHiddenSize> retention{};
    std::uint8_t byte{0};
    std::uint8_t category_feature{16};
    std::uint8_t boundary_feature{24};
};

struct Cache {
    std::array<float, kHiddenSize> initial_activation{}, activation{}, hidden{};
    std::array<float, kVocabularySize> probabilities{}, logits{};
    std::array<float, kContextSize * kHiddenSize> context_features{};
    std::array<std::size_t, kContextSize> context_counts{};
    std::array<RecurrentStep, kBpttTruncation> steps{};
    std::size_t step_count{0}, question_begin{0}, question_end{0};
    std::size_t sequence_begin{0}, processed_position{0}, total_steps{0};
    bool has_question_context{false}, history_truncated{false};
};

std::size_t recurrent_sequence_begin(std::span<const std::uint8_t> context,
                                     bool has_question) {
    if (!has_question) return 0;
    std::size_t begin = context.size();
    for (std::size_t i = 0; i + 2 < context.size(); ++i) {
        const bool line_start = i == 0 || context[i - 1] == '\n' || context[i - 1] == '\r';
        if (line_start && context[i] == 'I' && context[i + 1] == 'A' && context[i + 2] == ':')
            begin = i + 3;
    }
    return begin;
}

void append_recurrent_step(const Model& model, std::span<const std::uint8_t> context,
                           std::size_t position, std::size_t bptt_steps, Cache& cache) {
    RecurrentStep step;
    step.previous_state = cache.activation;
    step.byte = context[position];
    const auto previous = position == 0 ? static_cast<std::uint8_t>(' ') : context[position - 1];
    const auto features = linguistic_features(step.byte, previous);
    step.category_feature = features.category;
    step.boundary_feature = features.boundary;
    const auto embedding_base = kEmbeddingOffset + static_cast<std::size_t>(step.byte) * kHiddenSize;
    const auto category_base = kEmbeddingOffset + static_cast<std::size_t>(step.category_feature) * kHiddenSize;
    const auto boundary_base = kEmbeddingOffset + static_cast<std::size_t>(step.boundary_feature) * kHiddenSize;
    for (std::size_t h = 0; h < kHiddenSize; ++h) {
        float value = at(model, kHiddenBiasOffset + h);
        for (std::size_t k = 0; k < kHiddenSize; ++k)
            value += at(model, kHiddenWeightOffset + h * kHiddenSize + k) *
                (at(model, embedding_base + k) + at(model, category_base + k) +
                 at(model, boundary_base + k) + cache.activation[k]);
        step.candidate[h] = std::tanh(value);
        step.retention[h] = 1.0F / (1.0F + std::exp(-value));
        step.activation[h] = step.retention[h] * cache.activation[h] +
            (1.0F - step.retention[h]) * step.candidate[h];
    }
    cache.activation = step.activation;
    if (cache.step_count == bptt_steps) {
        for (std::size_t i = 1; i < cache.step_count; ++i) cache.steps[i - 1] = cache.steps[i];
        --cache.step_count;
    }
    cache.steps[cache.step_count++] = step;
    ++cache.total_steps;
}

void finish_cache(const Model& model, Cache& cache, bool calculate_probabilities) {
    cache.history_truncated = cache.total_steps > cache.step_count;
    for (std::size_t h = 0; h < kHiddenSize; ++h)
        cache.hidden[h] = at(model, kScaleOffset + h) * cache.activation[h] +
                          at(model, kShiftOffset + h);
    cache.logits = logits_for_state(model, cache.activation);
    cache.probabilities.fill(0.0F);
    if (!calculate_probabilities) return;
    double maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t token = 0; token < kVocabularySize; ++token)
        if (!reserved_feature_byte(static_cast<std::uint8_t>(token)))
            maximum = std::max(maximum, static_cast<double>(cache.logits[token]));
    double denominator = 0.0;
    for (std::size_t token = 0; token < kVocabularySize; ++token) {
        if (reserved_feature_byte(static_cast<std::uint8_t>(token))) continue;
        const double value = std::exp(static_cast<double>(cache.logits[token]) - maximum);
        cache.probabilities[token] = static_cast<float>(value);
        denominator += value;
    }
    if (denominator > 0.0)
        for (auto& probability : cache.probabilities)
            probability = static_cast<float>(probability / denominator);
}

Cache calculate(const Model& model, std::span<const std::uint8_t> context,
                bool calculate_probabilities = true,
                const QuestionFeatures* prepared_question = nullptr,
                std::size_t bptt_steps = kBpttTruncation) {
    Cache cache;
    bptt_steps = std::clamp<std::size_t>(bptt_steps, 1, kBpttTruncation);
    const QuestionFeatures local_question = prepared_question
        ? QuestionFeatures{} : prepare_question_features(model, context);
    const QuestionFeatures& question = prepared_question ? *prepared_question : local_question;

    cache.activation = initial_recurrent_state(model, question);
    cache.initial_activation = cache.activation;
    if (question.available) {
        cache.has_question_context = true;
        cache.question_begin = question.begin;
        cache.question_end = question.end;
        cache.context_features = question.values;
        cache.context_counts = question.counts;
    }
    cache.sequence_begin = recurrent_sequence_begin(context, question.available);
    cache.processed_position = cache.sequence_begin;
    for (std::size_t position = cache.sequence_begin; position < context.size(); ++position)
        append_recurrent_step(model, context, position, bptt_steps, cache);
    cache.processed_position = context.size();
    finish_cache(model, cache, calculate_probabilities);
    return cache;
}

bool extend_cache(const Model& model, std::span<const std::uint8_t> context,
                  std::size_t bptt_steps, Cache& cache) {
    bptt_steps = std::clamp<std::size_t>(bptt_steps, 1, kBpttTruncation);
    if (context.size() < cache.processed_position) return false;
    const QuestionFeatures question = prepare_question_features(model, context);
    if (question.available != cache.has_question_context) return false;
    if (question.available &&
        (question.begin != cache.question_begin || question.end != cache.question_end))
        return false;
    if (recurrent_sequence_begin(context, question.available) != cache.sequence_begin)
        return false;
    for (std::size_t position = cache.processed_position; position < context.size(); ++position)
        append_recurrent_step(model, context, position, bptt_steps, cache);
    cache.processed_position = context.size();
    finish_cache(model, cache, true);
    return true;
}

struct Utf8State {
    int remaining{0};
    std::uint8_t next_min{0x80};
    std::uint8_t next_max{0xBF};
};

bool consume_utf8(Utf8State& state, std::uint8_t byte) {
    if (state.remaining > 0) {
        if (byte < state.next_min || byte > state.next_max) return false;
        --state.remaining;
        state.next_min = 0x80;
        state.next_max = 0xBF;
        return true;
    }
    if (byte <= 0x7F) return true;
    if (byte >= 0xC2 && byte <= 0xDF) {
        state.remaining = 1;
    } else if (byte == 0xE0) {
        state.remaining = 2;
        state.next_min = 0xA0;
    } else if ((byte >= 0xE1 && byte <= 0xEC) ||
               (byte >= 0xEE && byte <= 0xEF)) {
        state.remaining = 2;
    } else if (byte == 0xED) {
        state.remaining = 2;
        state.next_max = 0x9F;
    } else if (byte == 0xF0) {
        state.remaining = 3;
        state.next_min = 0x90;
    } else if (byte >= 0xF1 && byte <= 0xF3) {
        state.remaining = 3;
    } else if (byte == 0xF4) {
        state.remaining = 3;
        state.next_max = 0x8F;
    } else {
        return false;
    }
    return true;
}

bool valid_utf8_prefix(std::span<const std::uint8_t> bytes, Utf8State& state) {
    for (const auto byte : bytes)
        if (!consume_utf8(state, byte)) return false;
    return true;
}

bool valid_utf8_start(std::uint8_t byte, std::size_t slots_after) {
    if (byte <= 0x7F) return true;
    if (byte >= 0xC2 && byte <= 0xDF) return slots_after >= 1;
    if (byte >= 0xE0 && byte <= 0xEF) return slots_after >= 2;
    if (byte >= 0xF0 && byte <= 0xF4) return slots_after >= 3;
    return false;
}

std::uint64_t checksum(const float* values, std::size_t count) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(values);
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < count * sizeof(float); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void apply_adam(Model& model, const std::array<float, kParameterCount>& gradient,
                TrainingState& state, const TrainingConfig& config) {
    constexpr double beta1 = 0.9;
    constexpr double beta2 = 0.999;
    constexpr double epsilon = 1.0e-8;
    const double correction1 = 1.0 - std::pow(beta1, static_cast<double>(state.step));
    const double correction2 = 1.0 - std::pow(beta2, static_cast<double>(state.step));

    double norm_squared = 0.0;
    for (float value : gradient)
        norm_squared += static_cast<double>(value) * value;
    const double norm = std::sqrt(norm_squared);
    const double clip = norm > config.gradient_clip && norm > 0.0
        ? config.gradient_clip / norm : 1.0;

    for (std::size_t i = 0; i < kParameterCount; ++i) {
        const double g = static_cast<double>(gradient[i]) * clip;
        state.first_moment[i] = beta1 * state.first_moment[i] + (1.0 - beta1) * g;
        state.second_moment[i] = beta2 * state.second_moment[i] + (1.0 - beta2) * g * g;
        const double m = state.first_moment[i] / correction1;
        const double v = state.second_moment[i] / correction2;
        // Decaimento desacoplado apenas em matrizes e embeddings; biases,
        // normalização afim e estado inicial não recebem weight decay.
        const bool decay_parameter =
            i < kEmbeddingWeightEnd || (i >= kHiddenWeightOffset && i < kHiddenWeightEnd) ||
            (i >= kContextWeightOffset && i < kContextWeightEnd);
        const double decay = decay_parameter
            ? config.weight_decay * model.parameters[i] : 0.0;
        model.parameters[i] -= static_cast<float>(
            config.learning_rate * (m / (std::sqrt(v) + epsilon) + decay));
    }
}

} // namespace

void initialize(Model& model, std::uint64_t seed) {
    std::mt19937_64 random(seed);
    std::normal_distribution<float> embedding(0.0F, 0.025F);
    std::normal_distribution<float> matrix(0.0F, 0.10F);
    std::normal_distribution<float> context(0.0F, 0.10F);
    model.parameters.fill(0.0F);

    for (std::size_t i = 0; i < 8192; ++i)
        model.parameters[kEmbeddingOffset + i] = embedding(random);
    for (std::size_t i = 0; i < 1024; ++i)
        model.parameters[kHiddenWeightOffset + i] = matrix(random);
    for (std::size_t i = 0; i < 416; ++i)
        model.parameters[kContextWeightOffset + i] = context(random);
    for (std::size_t h = 0; h < kHiddenSize; ++h)
        model.parameters[kScaleOffset + h] = 1.0F;
}

ForwardResult forward(const Model& model,
                      std::span<const std::uint8_t> context) {
    const auto cache = calculate(model, context);
    ForwardResult result;
    result.logits = cache.logits;
    result.hidden = cache.hidden;
    return result;
}

double evaluate_loss(const Model& model, std::span<const std::uint8_t> bytes,
                     std::span<const std::uint8_t> target_mask) {
    if (bytes.size() < 2) return 0.0;
    if (!target_mask.empty() && target_mask.size() != bytes.size())
        return std::numeric_limits<double>::infinity();
    double total = 0.0;
    std::uint64_t count = 0;
    for (std::size_t target = 0; target < bytes.size(); ++target) {
        if (!target_mask.empty() && target_mask[target] == 0) continue;
        const auto cache = calculate(model, bytes.first(target));
        const double probability = std::max(
            static_cast<double>(cache.probabilities[bytes[target]]), 1.0e-30);
        total -= std::log(probability);
        ++count;
    }
    return count == 0 ? 0.0 : total / static_cast<double>(count);
}

 
namespace {
bool accumulate_target_gradient_from_cache(
    const Model& model, std::span<const std::uint8_t> bytes,
    std::size_t target_index, const Cache& cache,
    std::array<float, kParameterCount>& gradient) {
    if (target_index >= bytes.size()) return false;
    std::array<float, kHiddenSize> hidden_gradient{};
    for (std::size_t token = 0; token < kVocabularySize; ++token) {
        const float delta = cache.probabilities[token] -
            (token == bytes[target_index] ? 1.0F : 0.0F);
        gradient[kOutputBiasOffset + token] += delta;
        const auto base = kEmbeddingOffset + token * kHiddenSize;
        for (std::size_t h = 0; h < kHiddenSize; ++h) {
            gradient[base + h] += delta * cache.hidden[h];
            hidden_gradient[h] += delta * at(model, base + h);
        }
    }

    std::array<float, kHiddenSize> state_gradient{};
    for (std::size_t h = 0; h < kHiddenSize; ++h) {
        gradient[kScaleOffset + h] += hidden_gradient[h] * cache.activation[h];
        gradient[kShiftOffset + h] += hidden_gradient[h];
        state_gradient[h] = hidden_gradient[h] * at(model, kScaleOffset + h);
    }

    // BPTT truncado nas últimas N transições recorrentes.
    for (std::size_t reverse = cache.step_count; reverse > 0; --reverse) {
        const RecurrentStep& step = cache.steps[reverse - 1];
        std::array<float, kHiddenSize> pre_gradient{}, previous_gradient{}, input_gradient{};
        for (std::size_t h = 0; h < kHiddenSize; ++h) {
            const float gate = step.retention[h];
            const float candidate = step.candidate[h];
            const float previous = step.previous_state[h];
            const float d_candidate = 1.0F - candidate * candidate;
            const float d_gate = gate * (1.0F - gate);
            const float d_state_d_pre =
                (1.0F - gate) * d_candidate +
                d_gate * (previous - candidate);
            pre_gradient[h] = state_gradient[h] * d_state_d_pre;
            previous_gradient[h] += state_gradient[h] * gate;
            gradient[kHiddenBiasOffset + h] += pre_gradient[h];
        }
        const auto embedding_base = kEmbeddingOffset +
            static_cast<std::size_t>(step.byte) * kHiddenSize;
        const auto category_base = kEmbeddingOffset +
            static_cast<std::size_t>(step.category_feature) * kHiddenSize;
        const auto boundary_base = kEmbeddingOffset +
            static_cast<std::size_t>(step.boundary_feature) * kHiddenSize;
        for (std::size_t h = 0; h < kHiddenSize; ++h) {
            for (std::size_t k = 0; k < kHiddenSize; ++k) {
                const std::size_t weight = kHiddenWeightOffset + h * kHiddenSize + k;
                gradient[weight] += pre_gradient[h] *
                    (at(model, embedding_base + k) + at(model, category_base + k) +
                     at(model, boundary_base + k) + step.previous_state[k]);
                const float contribution = pre_gradient[h] * at(model, weight);
                previous_gradient[k] += contribution;
                input_gradient[k] += contribution;
            }
        }
        for (std::size_t h = 0; h < kHiddenSize; ++h) {
            gradient[embedding_base + h] += input_gradient[h];
            gradient[category_base + h] += input_gradient[h];
            gradient[boundary_base + h] += input_gradient[h];
        }
        state_gradient = previous_gradient;
    }

    if (!cache.history_truncated) {
        std::array<float, kHiddenSize> initial_pre_gradient{};
        for (std::size_t h = 0; h < kHiddenSize; ++h) {
            initial_pre_gradient[h] = state_gradient[h] *
                (1.0F - cache.initial_activation[h] * cache.initial_activation[h]);
            gradient[kHiddenBiasOffset + h] += initial_pre_gradient[h];
            if (h < 16) gradient[kStartOffset + h] += initial_pre_gradient[h];
        }
        if (cache.has_question_context) {
            for (std::size_t bucket = 0; bucket < kContextSize; ++bucket)
                for (std::size_t h = 0; h < kHiddenSize; ++h) {
                    const std::size_t weight = kContextWeightOffset + bucket * kHiddenSize + h;
                    gradient[weight] += initial_pre_gradient[h] *
                        cache.context_features[bucket * kHiddenSize + h];
                }
            for (std::size_t position = cache.question_begin;
                 position < cache.question_end; ++position) {
                const std::size_t bucket =
                    question_position_bucket(position, cache.question_begin, cache.question_end);
                const auto base = kEmbeddingOffset +
                    static_cast<std::size_t>(bytes[position]) * kHiddenSize;
                const auto features = linguistic_features(bytes[position],
                    position == 0 ? static_cast<std::uint8_t>(' ') : bytes[position - 1]);
                const auto category_base = kEmbeddingOffset +
                    static_cast<std::size_t>(features.category) * kHiddenSize;
                const auto boundary_base = kEmbeddingOffset +
                    static_cast<std::size_t>(features.boundary) * kHiddenSize;
                const float inverse_count = 1.0F /
                    static_cast<float>(cache.context_counts[bucket]);
                for (std::size_t h = 0; h < kHiddenSize; ++h) {
                    const float contribution = initial_pre_gradient[h] *
                        at(model, kContextWeightOffset + bucket * kHiddenSize + h) *
                        inverse_count;
                    gradient[base + h] += contribution;
                    gradient[category_base + h] += contribution;
                    gradient[boundary_base + h] += contribution;
                }
            }
        }
    }
    return true;
}

bool accumulate_target_gradient(const Model& model, std::span<const std::uint8_t> bytes,
                                std::size_t target_index,
                                std::array<float, kParameterCount>& gradient,
                                std::size_t bptt_steps = kBpttTruncation) {
    if (target_index >= bytes.size()) return false;
    const Cache cache = calculate(model, bytes.first(target_index), true, nullptr, bptt_steps);
    return accumulate_target_gradient_from_cache(model, bytes, target_index, cache, gradient);
}

} // namespace

bool compute_target_gradient(const Model& model,
                             std::span<const std::uint8_t> bytes,
                             std::size_t target_index,
                             std::array<float, kParameterCount>& gradient) {
    gradient.fill(0.0F);
    return accumulate_target_gradient(model, bytes, target_index, gradient);
}

TrainingReport train(Model& model, std::span<const std::uint8_t> bytes,
                    const TrainingConfig& config, TrainingState* supplied_state,
                    std::span<const std::uint8_t> target_mask,
                    std::span<const double> target_learning_rates) {
    TrainingReport report;
    if (bytes.size() < 2 || config.epochs == 0 || config.batch_size == 0 || config.bptt_steps == 0 ||
        config.bptt_steps > kBpttTruncation ||
        !std::isfinite(config.learning_rate) || config.learning_rate <= 0.0 ||
        !std::isfinite(config.weight_decay) || config.weight_decay < 0.0 ||
        !std::isfinite(config.gradient_clip) || config.gradient_clip <= 0.0 ||
        (!target_mask.empty() && target_mask.size() != bytes.size()) ||
        (!target_learning_rates.empty() && target_learning_rates.size() != bytes.size()))
        return report;
    if (!target_mask.empty() &&
        std::none_of(target_mask.begin(), target_mask.end(), [](std::uint8_t value) { return value != 0; }))
        return report;
    if (!target_learning_rates.empty() &&
        !std::all_of(target_learning_rates.begin(), target_learning_rates.end(),
            [](double value) { return std::isfinite(value) && value > 0.0 && value <= 0.1; }))
        return report;

    report.initial_loss = evaluate_loss(model, bytes, target_mask);
    report.initial_bpb = report.initial_loss / std::log(2.0);
    TrainingState local_state;
    TrainingState& state = supplied_state ? *supplied_state : local_state;
    if (state.step == std::numeric_limits<std::uint64_t>::max() ||
        !std::all_of(model.parameters.begin(), model.parameters.end(),
                     [](float value) { return std::isfinite(value); }))
        return report;
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        if (!std::isfinite(state.first_moment[i]) ||
            !std::isfinite(state.second_moment[i]) || state.second_moment[i] < 0.0)
            return report;
    }
    std::array<float, kParameterCount> gradient{};
    std::size_t accumulated_targets = 0;
    const auto apply_batch = [&](double batch_learning_rate) {
        if (accumulated_targets == 0) return true;
        if (state.step == std::numeric_limits<std::uint64_t>::max() ||
            !std::isfinite(batch_learning_rate) || batch_learning_rate <= 0.0 ||
            batch_learning_rate > 0.1 ||
            !std::all_of(gradient.begin(), gradient.end(),
                         [](float value) { return std::isfinite(value); }))
            return false;
        const float inverse_batch = 1.0F / static_cast<float>(accumulated_targets);
        for (auto& value : gradient) value *= inverse_batch;
        ++state.step;
        TrainingConfig batch_config = config;
        batch_config.learning_rate = batch_learning_rate;
        apply_adam(model, gradient, state, batch_config);
        gradient.fill(0.0F);
        accumulated_targets = 0;
        return std::all_of(model.parameters.begin(), model.parameters.end(),
                           [](float value) { return std::isfinite(value); });
    };

    std::vector<std::size_t> target_order;
    target_order.reserve(bytes.size());
    for (std::size_t target = 0; target < bytes.size(); ++target) {
        if (target_mask.empty() || target_mask[target] != 0)
            target_order.push_back(target);
    }

    for (std::size_t epoch = 0; epoch < config.epochs; ++epoch) {
        std::uint64_t epoch_targets = 0;
        // Agrupa alvos em blocos causais ordenados. Os blocos são embaralhados,
        // mas cada bloco compartilha o forward recorrente enquanto os pesos ficam fixos.
        std::sort(target_order.begin(), target_order.end());
        std::vector<std::vector<std::size_t>> target_batches;
        target_batches.reserve((target_order.size() + config.batch_size - 1) / config.batch_size);
        double current_batch_rate = 0.0;
        for (const std::size_t target : target_order) {
            const double target_rate = target_learning_rates.empty()
                ? config.learning_rate : target_learning_rates[target];
            if (target_batches.empty() || target_batches.back().size() >= config.batch_size ||
                target_rate != current_batch_rate) {
                target_batches.emplace_back();
                current_batch_rate = target_rate;
            }
            target_batches.back().push_back(target);
        }
        std::mt19937_64 shuffle_rng(
            config.seed ^ (state.step + 0x9E3779B97F4A7C15ULL));
        std::shuffle(target_batches.begin(), target_batches.end(), shuffle_rng);

        for (const auto& batch : target_batches) {
            const double batch_learning_rate = target_learning_rates.empty()
                ? config.learning_rate : target_learning_rates[batch.front()];
            Cache cache;
            bool cache_valid = false;
            for (const std::size_t target : batch) {
                const auto prefix = bytes.first(target);
                if (!cache_valid || !extend_cache(model, prefix, config.bptt_steps, cache)) {
                    cache = calculate(model, prefix, true, nullptr, config.bptt_steps);
                    cache_valid = true;
                }
                if (!accumulate_target_gradient_from_cache(
                        model, bytes, target, cache, gradient))
                    return report;
                ++accumulated_targets;
                ++epoch_targets;
            }
            if (!apply_batch(batch_learning_rate)) return report;
        }
        if (!apply_batch(config.learning_rate)) return report;
        ++report.epochs_completed;
        report.target_bytes += epoch_targets;
    }

    report.final_loss = evaluate_loss(model, bytes, target_mask);
    report.final_bpb = report.final_loss / std::log(2.0);
    return report;
}

std::vector<std::uint8_t> generate(const Model& model,
                                   std::span<const std::uint8_t> prompt,
                                   std::size_t max_new_bytes, const GenerationConfig& config) {
    std::vector<std::uint8_t> output(prompt.begin(), prompt.end());
    output.reserve(output.size() + max_new_bytes);
    Utf8State utf8;
    if (!valid_utf8_prefix(output, utf8) ||
        static_cast<std::size_t>(utf8.remaining) > max_new_bytes)
        return output;

    if (!std::isfinite(config.temperature) || config.temperature < 1.0e-6 || config.temperature > 100.0 ||
        !std::isfinite(config.top_p) || config.top_p <= 0.0 || config.top_p > 1.0 ||
        !std::isfinite(config.repetition_penalty) || config.repetition_penalty < 1.0)
        return output;
    std::mt19937_64 sampling_rng(config.seed);

    const QuestionFeatures question = prepare_question_features(model, prompt);
    std::size_t sequence_begin = 0;
    if (question.available) {
        sequence_begin = prompt.size();
        for (std::size_t i = 0; i + 2 < prompt.size(); ++i) {
            const bool line_start = i == 0 || prompt[i - 1] == '\n' ||
                                    prompt[i - 1] == '\r';
            if (line_start && prompt[i] == 'I' && prompt[i + 1] == 'A' &&
                prompt[i + 2] == ':')
                sequence_begin = i + 3;
        }
    }
    auto state = initial_recurrent_state(model, question);
    std::uint8_t previous_byte = sequence_begin == 0
        ? static_cast<std::uint8_t>(' ') : prompt[sequence_begin - 1];
    for (std::size_t position = sequence_begin; position < prompt.size(); ++position) {
        state = advance_recurrent_state(model, state, prompt[position], previous_byte);
        previous_byte = prompt[position];
    }

    for (std::size_t i = 0; i < max_new_bytes; ++i) {
        const auto logits = logits_for_state(model, state);
        const std::size_t slots_after = max_new_bytes - i - 1;
        std::vector<std::pair<std::uint8_t, double>> candidates;
        candidates.reserve(kVocabularySize);
        const std::size_t repetition_begin = output.size() > config.repetition_window
            ? std::max(prompt.size(), output.size() - config.repetition_window)
            : prompt.size();
        for (std::size_t token = 0; token < kVocabularySize; ++token) {
            const auto byte = static_cast<std::uint8_t>(token);
            const bool allowed = utf8.remaining > 0
                ? byte >= utf8.next_min && byte <= utf8.next_max
                : valid_utf8_start(byte, slots_after);
            if (!allowed) continue;
            double score = static_cast<double>(logits[token]);
            if (config.repetition_penalty > 1.0 &&
                std::find(output.begin() + static_cast<std::ptrdiff_t>(repetition_begin),
                          output.end(), byte) != output.end()) {
                score = score >= 0.0 ? score / config.repetition_penalty
                                     : score * config.repetition_penalty;
            }
            if (std::isfinite(score)) candidates.emplace_back(byte, score);
        }
        if (candidates.empty()) break;

        std::uint8_t selected = candidates.front().first;
        if (!config.sample) {
            double best_score = candidates.front().second;
            for (const auto& candidate : candidates) {
                if (candidate.second > best_score ||
                    (candidate.second == best_score && candidate.first < selected)) {
                    selected = candidate.first;
                    best_score = candidate.second;
                }
            }
        } else {
            for (auto& candidate : candidates) candidate.second /= config.temperature;
            if (!std::all_of(candidates.begin(), candidates.end(),
                             [](const auto& candidate) { return std::isfinite(candidate.second); }))
                break;
            std::sort(candidates.begin(), candidates.end(),
                [](const auto& left, const auto& right) {
                    return left.second == right.second
                        ? left.first < right.first : left.second > right.second;
                });
            if (config.top_k > 0 && candidates.size() > config.top_k)
                candidates.resize(config.top_k);
            const double maximum = candidates.front().second;
            std::vector<double> weights;
            weights.reserve(candidates.size());
            double denominator = 0.0;
            for (const auto& candidate : candidates) {
                const double weight = std::exp(candidate.second - maximum);
                weights.push_back(weight);
                denominator += weight;
            }
            if (config.top_p < 1.0) {
                double cumulative = 0.0;
                std::size_t keep = 0;
                for (; keep < weights.size(); ++keep) {
                    cumulative += weights[keep] / denominator;
                    if (cumulative >= config.top_p) { ++keep; break; }
                }
                candidates.resize(std::max<std::size_t>(1, keep));
                weights.resize(candidates.size());
            }
            std::discrete_distribution<std::size_t> distribution(weights.begin(), weights.end());
            selected = candidates[distribution(sampling_rng)].first;
        }
        const auto byte = selected;
        if (!consume_utf8(utf8, byte)) break;
        output.push_back(byte);
        if (byte == '\n' || byte == '\r' || byte == '\0') break;
        state = advance_recurrent_state(model, state, byte, previous_byte);
        previous_byte = byte;
    }
    return output;
}

bool save_checkpoint(const Model& model, const std::filesystem::path& path) {
    if (path.empty()) return false;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    constexpr char magic[8] = {'M','O','R','O','K','G','2','T'};
    const std::uint32_t version = kCheckpointFormatVersion;
    const std::uint32_t count = static_cast<std::uint32_t>(kParameterCount);
    const std::uint64_t hash = checksum(model.parameters.data(),
                                        model.parameters.size());
    stream.write(magic, sizeof(magic));
    stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
    stream.write(reinterpret_cast<const char*>(&count), sizeof(count));
    stream.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
    stream.write(reinterpret_cast<const char*>(model.parameters.data()),
                 static_cast<std::streamsize>(sizeof(float) * kParameterCount));
    return stream.good();
}

bool save_training_state(const TrainingState& state,
                         const std::filesystem::path& path) {
    if (path.empty()) return false;
    constexpr char magic[8] = {'M','O','R','O','K','A','D','M'};
    constexpr std::uint32_t version = 8;
    constexpr std::uint32_t count = static_cast<std::uint32_t>(kParameterCount);
    std::uint64_t hash = 14695981039346656037ULL;
    const auto update_hash = [&hash](const unsigned char* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= data[i];
            hash *= 1099511628211ULL;
        }
    };
    update_hash(reinterpret_cast<const unsigned char*>(&state.step), sizeof(state.step));
    update_hash(reinterpret_cast<const unsigned char*>(state.first_moment.data()),
                sizeof(double) * kParameterCount);
    update_hash(reinterpret_cast<const unsigned char*>(state.second_moment.data()),
                sizeof(double) * kParameterCount);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(magic, sizeof(magic));
    stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
    stream.write(reinterpret_cast<const char*>(&count), sizeof(count));
    stream.write(reinterpret_cast<const char*>(&state.step), sizeof(state.step));
    stream.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
    stream.write(reinterpret_cast<const char*>(state.first_moment.data()),
                 static_cast<std::streamsize>(sizeof(double) * kParameterCount));
    stream.write(reinterpret_cast<const char*>(state.second_moment.data()),
                 static_cast<std::streamsize>(sizeof(double) * kParameterCount));
    return stream.good();
}

bool load_training_state(TrainingState& state,
                         const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    char magic[8]{};
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    std::uint64_t step = 0;
    std::uint64_t expected_hash = 0;
    TrainingState candidate;
    stream.read(magic, sizeof(magic));
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    stream.read(reinterpret_cast<char*>(&count), sizeof(count));
    stream.read(reinterpret_cast<char*>(&step), sizeof(step));
    stream.read(reinterpret_cast<char*>(&expected_hash), sizeof(expected_hash));
    if (!stream || std::memcmp(magic, "MOROKADM", 8) != 0 ||
        version != 8 || count != kParameterCount)
        return false;
    candidate.step = step;
    stream.read(reinterpret_cast<char*>(candidate.first_moment.data()),
                static_cast<std::streamsize>(sizeof(double) * kParameterCount));
    stream.read(reinterpret_cast<char*>(candidate.second_moment.data()),
                static_cast<std::streamsize>(sizeof(double) * kParameterCount));
    if (!stream || stream.peek() != std::char_traits<char>::eof()) return false;
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        if (!std::isfinite(candidate.first_moment[i]) ||
            !std::isfinite(candidate.second_moment[i]) ||
            candidate.second_moment[i] < 0.0) return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    const auto update_hash = [&hash](const unsigned char* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= data[i];
            hash *= 1099511628211ULL;
        }
    };
    update_hash(reinterpret_cast<const unsigned char*>(&candidate.step), sizeof(candidate.step));
    update_hash(reinterpret_cast<const unsigned char*>(candidate.first_moment.data()),
                sizeof(double) * kParameterCount);
    update_hash(reinterpret_cast<const unsigned char*>(candidate.second_moment.data()),
                sizeof(double) * kParameterCount);
    if (hash != expected_hash) return false;
    state = candidate;
    return true;
}

bool load_checkpoint(Model& model, const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    char magic[8]{};
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    std::uint64_t expected_hash = 0;
    Model candidate;
    stream.read(magic, sizeof(magic));
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    stream.read(reinterpret_cast<char*>(&count), sizeof(count));
    stream.read(reinterpret_cast<char*>(&expected_hash), sizeof(expected_hash));
    if (!stream || std::memcmp(magic, "MOROKG2T", 8) != 0 ||
        version != kCheckpointFormatVersion || count != kParameterCount)
        return false;
    stream.read(reinterpret_cast<char*>(candidate.parameters.data()),
                static_cast<std::streamsize>(sizeof(float) * kParameterCount));
    if (!stream || stream.peek() != std::char_traits<char>::eof())
        return false;
    for (float value : candidate.parameters)
        if (!std::isfinite(value)) return false;
    if (checksum(candidate.parameters.data(), candidate.parameters.size()) !=
        expected_hash)
        return false;
    model = candidate;
    return true;
}

} // namespace morok::g2::tsukuyomi