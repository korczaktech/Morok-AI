#include "morok/g2/tsukuyomi/model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <string>

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

static_assert(kStartOffset + 16 == kParameterCount);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

float at(const Model& model, std::size_t offset) {
    return model.parameters[offset];
}

struct Cache {
    std::array<float, kHiddenSize> input{};
    std::array<float, kHiddenSize> activation{};
    std::array<float, kHiddenSize> hidden{};
    std::array<float, kVocabularySize> probabilities{};
    std::array<float, kVocabularySize> logits{};
    std::array<float, kContextSize> context_features{};
    std::size_t current_byte{0};
    bool has_current{false};
    bool at_start{false};
};

Cache calculate(const Model& model, std::span<const std::uint8_t> context) {
    Cache cache;
    cache.has_current = !context.empty();
    cache.current_byte = cache.has_current ? context.back() : 0;
    cache.at_start = context.empty();

    if (cache.has_current) {
        const auto base = kEmbeddingOffset + cache.current_byte * kHiddenSize;
        for (std::size_t h = 0; h < kHiddenSize; ++h)
            cache.input[h] = at(model, base + h);
    }

    const std::size_t available = context.empty() ? 0 :
        std::min(kContextSize, context.size() - 1);
    for (std::size_t j = 0; j < available; ++j) {
        const auto byte = context[context.size() - 2 - j];
        const float feature = (static_cast<float>(byte) - 127.5F) / 127.5F;
        cache.context_features[j] = feature;
        for (std::size_t h = 0; h < kHiddenSize; ++h) {
            cache.input[h] += feature *
                at(model, kContextWeightOffset + j * kHiddenSize + h);
        }
    }

    for (std::size_t h = 0; h < kHiddenSize; ++h) {
        float value = at(model, kHiddenBiasOffset + h);
        for (std::size_t k = 0; k < kHiddenSize; ++k)
            value += at(model, kHiddenWeightOffset + h * kHiddenSize + k) *
                     cache.input[k];
        if (cache.at_start && h < 16)
            value += at(model, kStartOffset + h);
        cache.activation[h] = std::tanh(value);
        cache.hidden[h] = at(model, kScaleOffset + h) * cache.activation[h] +
                          at(model, kShiftOffset + h);
    }

    double maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t token = 0; token < kVocabularySize; ++token) {
        float logit = at(model, kOutputBiasOffset + token);
        const auto base = kEmbeddingOffset + token * kHiddenSize;
        for (std::size_t h = 0; h < kHiddenSize; ++h)
            logit += at(model, base + h) * cache.hidden[h];
        cache.logits[token] = logit;
        maximum = std::max(maximum, static_cast<double>(logit));
    }

    double denominator = 0.0;
    for (std::size_t token = 0; token < kVocabularySize; ++token) {
        const double value = std::exp(
            static_cast<double>(cache.logits[token]) - maximum);
        cache.probabilities[token] = static_cast<float>(value);
        denominator += value;
    }
    if (denominator > 0.0) {
        for (auto& probability : cache.probabilities)
            probability = static_cast<float>(probability / denominator);
    }
    return cache;
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
    std::normal_distribution<float> context(0.0F, 0.015F);
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

 
bool compute_target_gradient(const Model& model,
                             std::span<const std::uint8_t> bytes,
                             std::size_t target_index,
                             std::array<float, kParameterCount>& gradient) {
    if (target_index >= bytes.size()) return false;
    gradient.fill(0.0F);
    const auto cache = calculate(model, bytes.first(target_index));
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
    std::array<float, kHiddenSize> pre_gradient{};
    for (std::size_t h = 0; h < kHiddenSize; ++h) {
        gradient[kScaleOffset + h] += hidden_gradient[h] * cache.activation[h];
        gradient[kShiftOffset + h] += hidden_gradient[h];
        pre_gradient[h] = hidden_gradient[h] * at(model, kScaleOffset + h) *
            (1.0F - cache.activation[h] * cache.activation[h]);
        gradient[kHiddenBiasOffset + h] += pre_gradient[h];
        if (cache.at_start && h < 16)
            gradient[kStartOffset + h] += pre_gradient[h];
    }
    std::array<float, kHiddenSize> input_gradient{};
    for (std::size_t h = 0; h < kHiddenSize; ++h) {
        for (std::size_t k = 0; k < kHiddenSize; ++k) {
            const auto index = kHiddenWeightOffset + h * kHiddenSize + k;
            gradient[index] += pre_gradient[h] * cache.input[k];
            input_gradient[k] += pre_gradient[h] * at(model, index);
        }
    }
    if (cache.has_current) {
        const auto base = kEmbeddingOffset + cache.current_byte * kHiddenSize;
        for (std::size_t h = 0; h < kHiddenSize; ++h)
            gradient[base + h] += input_gradient[h];
    }
    for (std::size_t j = 0; j < kContextSize; ++j) {
        for (std::size_t h = 0; h < kHiddenSize; ++h)
            gradient[kContextWeightOffset + j * kHiddenSize + h] +=
                pre_gradient[h] * cache.context_features[j];
    }
    return true;
}

TrainingReport train(Model& model, std::span<const std::uint8_t> bytes,
                    const TrainingConfig& config, TrainingState* supplied_state,
                    std::span<const std::uint8_t> target_mask) {
    TrainingReport report;
    if (bytes.size() < 2 || config.epochs == 0 || config.batch_size == 0 ||
        !std::isfinite(config.learning_rate) || config.learning_rate <= 0.0 ||
        !std::isfinite(config.weight_decay) || config.weight_decay < 0.0 ||
        !std::isfinite(config.gradient_clip) || config.gradient_clip <= 0.0 ||
        (!target_mask.empty() && target_mask.size() != bytes.size()))
        return report;
    if (!target_mask.empty() &&
        std::none_of(target_mask.begin(), target_mask.end(), [](std::uint8_t value) { return value != 0; }))
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
    const auto apply_batch = [&]() {
        if (accumulated_targets == 0) return true;
        if (state.step == std::numeric_limits<std::uint64_t>::max() ||
            !std::all_of(gradient.begin(), gradient.end(),
                         [](float value) { return std::isfinite(value); }))
            return false;
        const float inverse_batch = 1.0F / static_cast<float>(accumulated_targets);
        for (auto& value : gradient) value *= inverse_batch;
        ++state.step;
        apply_adam(model, gradient, state, config);
        gradient.fill(0.0F);
        accumulated_targets = 0;
        return std::all_of(model.parameters.begin(), model.parameters.end(),
                           [](float value) { return std::isfinite(value); });
    };

    for (std::size_t epoch = 0; epoch < config.epochs; ++epoch) {
        std::uint64_t epoch_targets = 0;
        for (std::size_t target = 0; target < bytes.size(); ++target) {
            if (!target_mask.empty() && target_mask[target] == 0) continue;
            std::array<float, kParameterCount> target_gradient{};
            if (!compute_target_gradient(model, bytes, target, target_gradient))
                return report;
            for (std::size_t i = 0; i < kParameterCount; ++i)
                gradient[i] += target_gradient[i];

            ++accumulated_targets;
            ++epoch_targets;
            if (accumulated_targets >= config.batch_size && !apply_batch())
                return report;
        }
        if (!apply_batch()) return report;
        ++report.epochs_completed;
        report.target_bytes += epoch_targets;
    }

    report.final_loss = evaluate_loss(model, bytes, target_mask);
    report.final_bpb = report.final_loss / std::log(2.0);
    return report;
}

std::vector<std::uint8_t> generate(const Model& model,
                                   std::span<const std::uint8_t> prompt,
                                   std::size_t max_new_bytes) {
    std::vector<std::uint8_t> output(prompt.begin(), prompt.end());
    output.reserve(output.size() + max_new_bytes);
    Utf8State utf8;
    if (!valid_utf8_prefix(output, utf8) ||
        static_cast<std::size_t>(utf8.remaining) > max_new_bytes)
        return output;

    for (std::size_t i = 0; i < max_new_bytes; ++i) {
        const auto cache = calculate(model, output);
        std::size_t best = kVocabularySize;
        float best_logit = -std::numeric_limits<float>::infinity();
        const std::size_t slots_after = max_new_bytes - i - 1;
        for (std::size_t token = 0; token < kVocabularySize; ++token) {
            const auto byte = static_cast<std::uint8_t>(token);
            const bool allowed = utf8.remaining > 0
                ? byte >= utf8.next_min && byte <= utf8.next_max
                : valid_utf8_start(byte, slots_after);
            if (allowed && cache.logits[token] > best_logit) {
                best = token;
                best_logit = cache.logits[token];
            }
        }
        if (best == kVocabularySize) break;
        const auto byte = static_cast<std::uint8_t>(best);
        if (!consume_utf8(utf8, byte)) break;
        output.push_back(byte);
    }
    return output;
}

bool save_checkpoint(const Model& model, const std::filesystem::path& path) {
    if (path.empty()) return false;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    constexpr char magic[8] = {'M','O','R','O','K','G','2','T'};
    const std::uint32_t version = kModelVersion;
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
    constexpr std::uint32_t version = 1;
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
        version != 1 || count != kParameterCount)
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
        version != kModelVersion || count != kParameterCount)
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
