#include "morok/g3/neural/gru_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace morok::g3::neural {
namespace {

constexpr std::size_t kStateFloats = kLayer1HiddenDim + kLayer2HiddenDim;
constexpr std::size_t kStateBytes = kStateFloats * sizeof(float);

[[nodiscard]] common::Status fail(common::ErrorCode code,
                                  std::string_view stage,
                                  std::string_view message) {
    return common::Status::failure(common::Error::make(
        code, std::string(message), "g3.neural", std::string(stage)));
}

void initialize_xavier(std::vector<float>& values, std::size_t rows,
                       std::size_t columns, std::mt19937_64& random) {
    const float bound = std::sqrt(6.0F / static_cast<float>(rows + columns));
    std::uniform_real_distribution<float> distribution(-bound, bound);
    for (float& value : values) value = distribution(random);
}

void initialize_orthogonal_gates(std::vector<float>& values,
                                 std::size_t hidden,
                                 std::mt19937_64& random) {
    std::normal_distribution<double> normal(0.0, 1.0);
    std::vector<double> matrix(hidden * hidden);
    for (std::size_t gate = 0; gate < kGateCount; ++gate) {
        for (double& value : matrix) value = normal(random);
        for (std::size_t column = 0; column < hidden; ++column) {
            for (std::size_t previous = 0; previous < column; ++previous) {
                double dot = 0.0;
                for (std::size_t row = 0; row < hidden; ++row)
                    dot += matrix[row * hidden + column] *
                           matrix[row * hidden + previous];
                for (std::size_t row = 0; row < hidden; ++row)
                    matrix[row * hidden + column] -=
                        dot * matrix[row * hidden + previous];
            }
            double norm_squared = 0.0;
            for (std::size_t row = 0; row < hidden; ++row) {
                const double value = matrix[row * hidden + column];
                norm_squared += value * value;
            }
            const double norm = std::sqrt(norm_squared);
            if (!(norm > 1.0e-12)) {
                for (std::size_t row = 0; row < hidden; ++row)
                    matrix[row * hidden + column] = row == column ? 1.0 : 0.0;
            } else {
                for (std::size_t row = 0; row < hidden; ++row)
                    matrix[row * hidden + column] /= norm;
            }
        }
        const std::size_t offset = gate * hidden * hidden;
        for (std::size_t i = 0; i < matrix.size(); ++i)
            values[offset + i] = static_cast<float>(matrix[i]);
    }
}

void initialize_input_gates(std::vector<float>& values, std::size_t hidden,
                            std::size_t input, std::mt19937_64& random) {
    const float bound = std::sqrt(6.0F / static_cast<float>(hidden + input));
    std::uniform_real_distribution<float> distribution(-bound, bound);
    for (float& value : values) value = distribution(random);
}

[[nodiscard]] G3Weights initialize_weights(std::uint64_t seed) {
    G3Weights weights;
    weights.embedding.resize(kVocabularySize * kEmbeddingDim);
    weights.layer1_input.resize(kGateCount * kLayer1HiddenDim * kEmbeddingDim);
    weights.layer1_recurrent.resize(kGateCount * kLayer1HiddenDim * kLayer1HiddenDim);
    weights.layer1_bias.assign(kGateCount * kLayer1HiddenDim, 0.0F);
    weights.layer2_input.resize(kGateCount * kLayer2HiddenDim * kLayer1HiddenDim);
    weights.layer2_recurrent.resize(kGateCount * kLayer2HiddenDim * kLayer2HiddenDim);
    weights.layer2_bias.assign(kGateCount * kLayer2HiddenDim, 0.0F);
    weights.rms_scale.assign(kLayer1HiddenDim, 1.0F);
    weights.projection.resize(kEmbeddingDim * kLayer2HiddenDim);
    weights.projection_bias.assign(kEmbeddingDim, 0.0F);
    weights.output_bias.assign(kVocabularySize, 0.0F);

    std::mt19937_64 random(seed);
    initialize_xavier(weights.embedding, kVocabularySize, kEmbeddingDim, random);
    initialize_input_gates(weights.layer1_input, kLayer1HiddenDim, kEmbeddingDim, random);
    initialize_orthogonal_gates(weights.layer1_recurrent, kLayer1HiddenDim, random);
    initialize_input_gates(weights.layer2_input, kLayer2HiddenDim, kLayer1HiddenDim, random);
    initialize_orthogonal_gates(weights.layer2_recurrent, kLayer2HiddenDim, random);
    initialize_xavier(weights.projection, kEmbeddingDim, kLayer2HiddenDim, random);

    std::fill_n(weights.layer1_bias.begin(), kLayer1HiddenDim, 1.0F);
    std::fill_n(weights.layer2_bias.begin(), kLayer2HiddenDim, 1.0F);
    return weights;
}

[[nodiscard]] float sigmoid(float value) noexcept {
    if (value >= 0.0F) {
        const float exponential = std::exp(-value);
        return 1.0F / (1.0F + exponential);
    }
    const float exponential = std::exp(value);
    return exponential / (1.0F + exponential);
}

void gru_step(std::span<const float> input,
              std::span<const float> previous,
              const std::vector<float>& input_weights,
              const std::vector<float>& recurrent_weights,
              const std::vector<float>& bias,
              std::vector<float>& output) {
    const std::size_t hidden = output.size();
    const std::size_t input_dim = input.size();
    std::vector<float> gates(kGateCount * hidden, 0.0F);

    for (std::size_t gate = 0; gate < 2U; ++gate) {
        for (std::size_t row = 0; row < hidden; ++row) {
            float sum = bias[gate * hidden + row];
            const std::size_t input_offset = (gate * hidden + row) * input_dim;
            for (std::size_t column = 0; column < input_dim; ++column)
                sum += input_weights[input_offset + column] * input[column];
            const std::size_t recurrent_offset = (gate * hidden + row) * hidden;
            for (std::size_t column = 0; column < hidden; ++column)
                sum += recurrent_weights[recurrent_offset + column] * previous[column];
            gates[gate * hidden + row] = sigmoid(sum);
        }
    }

    for (std::size_t row = 0; row < hidden; ++row) {
        float sum = bias[2U * hidden + row];
        const std::size_t input_offset = (2U * hidden + row) * input_dim;
        for (std::size_t column = 0; column < input_dim; ++column)
            sum += input_weights[input_offset + column] * input[column];
        const std::size_t recurrent_offset = (2U * hidden + row) * hidden;
        for (std::size_t column = 0; column < hidden; ++column)
            sum += recurrent_weights[recurrent_offset + column] *
                   gates[hidden + column] * previous[column];
        gates[2U * hidden + row] = std::tanh(sum);
    }

    for (std::size_t i = 0; i < hidden; ++i) {
        const float update = gates[i];
        const float candidate = gates[2U * hidden + i];
        output[i] = (1.0F - update) * candidate + update * previous[i];
    }
}

void rms_normalize(std::span<const float> input,
                   const std::vector<float>& scale, float epsilon,
                   std::vector<float>& output) {
    double sum_squares = 0.0;
    for (float value : input) sum_squares += static_cast<double>(value) * value;
    const float inverse_rms = static_cast<float>(
        1.0 / std::sqrt(sum_squares / static_cast<double>(input.size()) + epsilon));
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = input[i] * inverse_rms * scale[i];
}

[[nodiscard]] bool valid_weight_shapes(const G3Weights& w) noexcept {
    return w.embedding.size() == kVocabularySize * kEmbeddingDim &&
        w.layer1_input.size() == kGateCount * kLayer1HiddenDim * kEmbeddingDim &&
        w.layer1_recurrent.size() == kGateCount * kLayer1HiddenDim * kLayer1HiddenDim &&
        w.layer1_bias.size() == kGateCount * kLayer1HiddenDim &&
        w.layer2_input.size() == kGateCount * kLayer2HiddenDim * kLayer1HiddenDim &&
        w.layer2_recurrent.size() == kGateCount * kLayer2HiddenDim * kLayer2HiddenDim &&
        w.layer2_bias.size() == kGateCount * kLayer2HiddenDim &&
        w.rms_scale.size() == kLayer1HiddenDim &&
        w.projection.size() == kEmbeddingDim * kLayer2HiddenDim &&
        w.projection_bias.size() == kEmbeddingDim &&
        w.output_bias.size() == kVocabularySize;
}

} // namespace

std::uint64_t count_parameters(const G3Weights& weights) noexcept {
    return static_cast<std::uint64_t>(weights.embedding.size()) +
           static_cast<std::uint64_t>(weights.layer1_input.size()) +
           static_cast<std::uint64_t>(weights.layer1_recurrent.size()) +
           static_cast<std::uint64_t>(weights.layer1_bias.size()) +
           static_cast<std::uint64_t>(weights.layer2_input.size()) +
           static_cast<std::uint64_t>(weights.layer2_recurrent.size()) +
           static_cast<std::uint64_t>(weights.layer2_bias.size()) +
           static_cast<std::uint64_t>(weights.rms_scale.size()) +
           static_cast<std::uint64_t>(weights.projection.size()) +
           static_cast<std::uint64_t>(weights.projection_bias.size()) +
           static_cast<std::uint64_t>(weights.output_bias.size());
}

G3Backend::G3Backend(G3Config config)
    : weights_(initialize_weights(config.seed)),
      rms_epsilon_(config.rms_epsilon), seed_(config.seed) {
    if (!std::isfinite(rms_epsilon_) || rms_epsilon_ <= 0.0F)
        throw std::invalid_argument("G3 RMSNorm epsilon must be finite and positive");
    if (parameter_count() != kTargetParameterCount)
        throw std::logic_error("G3 architecture must contain exactly 100000 parameters");
}

model::ModelDescriptor G3Backend::descriptor() const noexcept {
    return {3U, kArchitectureId, static_cast<std::uint32_t>(kVocabularySize),
            parameter_count(), static_cast<std::uint32_t>(kStateBytes),
            static_cast<std::uint32_t>(alignof(float)), true};
}

common::Status G3Backend::reset_state(std::span<std::byte> state) const {
    if (state.size() != kStateBytes)
        return fail(common::ErrorCode::invalid_argument, "reset_state",
                    "G3 state must contain the two configured hidden vectors");
    std::fill(state.begin(), state.end(), std::byte{0});
    return common::Status::success();
}

common::Status G3Backend::consume_byte(std::span<std::byte> state,
                                       std::uint8_t byte) const {
    return consume_token(state, static_cast<std::uint16_t>(byte));
}

common::Status G3Backend::consume_token(std::span<std::byte> state,
                                        std::uint16_t token_id) const {
    if (token_id >= kVocabularySize)
        return fail(common::ErrorCode::invalid_argument, "consume_token",
                    "G3 token ID is outside the 261-entry vocabulary");
    if (state.size() != kStateBytes)
        return fail(common::ErrorCode::invalid_argument, "consume_token",
                    "G3 state has an invalid size");
    if (!valid_weight_shapes(weights_))
        return fail(common::ErrorCode::invalid_argument, "consume_token",
                    "G3 weight tensor dimensions are invalid");

    std::vector<float> previous(kStateFloats);
    std::memcpy(previous.data(), state.data(), kStateBytes);
    const std::span<const float> embedding(
        weights_.embedding.data() + static_cast<std::size_t>(token_id) * kEmbeddingDim,
        kEmbeddingDim);
    const std::span<const float> previous1(previous.data(), kLayer1HiddenDim);
    const std::span<const float> previous2(previous.data() + kLayer1HiddenDim,
                                           kLayer2HiddenDim);
    std::vector<float> layer1(kLayer1HiddenDim);
    std::vector<float> normalized(kLayer1HiddenDim);
    std::vector<float> layer2(kLayer2HiddenDim);

    gru_step(embedding, previous1, weights_.layer1_input,
             weights_.layer1_recurrent, weights_.layer1_bias, layer1);
    rms_normalize(layer1, weights_.rms_scale, rms_epsilon_, normalized);
    gru_step(normalized, previous2, weights_.layer2_input,
             weights_.layer2_recurrent, weights_.layer2_bias, layer2);

    for (float value : layer1)
        if (!std::isfinite(value))
            return fail(common::ErrorCode::internal_error, "consume_token",
                        "G3 first recurrent layer produced a non-finite state");
    for (float value : layer2)
        if (!std::isfinite(value))
            return fail(common::ErrorCode::internal_error, "consume_token",
                        "G3 second recurrent layer produced a non-finite state");

    std::vector<float> next;
    next.reserve(kStateFloats);
    next.insert(next.end(), layer1.begin(), layer1.end());
    next.insert(next.end(), layer2.begin(), layer2.end());
    std::memcpy(state.data(), next.data(), kStateBytes);
    return common::Status::success();
}

common::Status G3Backend::next_token_log_probabilities(
    std::span<const std::byte> state,
    std::span<float> log_probabilities) const {
    if (state.size() != kStateBytes || log_probabilities.size() != kVocabularySize)
        return fail(common::ErrorCode::invalid_argument, "next_token",
                    "G3 state or output dimensions are invalid");
    if (!valid_weight_shapes(weights_))
        return fail(common::ErrorCode::invalid_argument, "next_token",
                    "G3 output weight tensor dimensions are invalid");

    std::vector<float> hidden(kStateFloats);
    std::memcpy(hidden.data(), state.data(), kStateBytes);
    const float* final_hidden = hidden.data() + kLayer1HiddenDim;
    std::vector<float> projected(kEmbeddingDim);
    for (std::size_t row = 0; row < kEmbeddingDim; ++row) {
        float value = weights_.projection_bias[row];
        const std::size_t offset = row * kLayer2HiddenDim;
        for (std::size_t column = 0; column < kLayer2HiddenDim; ++column)
            value += weights_.projection[offset + column] * final_hidden[column];
        projected[row] = value;
    }

    float maximum = -std::numeric_limits<float>::infinity();
    for (std::size_t token = 0; token < kVocabularySize; ++token) {
        float logit = weights_.output_bias[token];
        const std::size_t offset = token * kEmbeddingDim;
        for (std::size_t dimension = 0; dimension < kEmbeddingDim; ++dimension)
            logit += weights_.embedding[offset + dimension] * projected[dimension];
        if (!std::isfinite(logit))
            return fail(common::ErrorCode::internal_error, "next_token",
                        "G3 produced a non-finite output logit");
        log_probabilities[token] = logit;
        maximum = std::max(maximum, logit);
    }

    double denominator = 0.0;
    for (float logit : log_probabilities)
        denominator += std::exp(static_cast<double>(logit - maximum));
    if (!(denominator > 0.0) || !std::isfinite(denominator))
        return fail(common::ErrorCode::internal_error, "next_token",
                    "G3 softmax normalization is invalid");
    const float log_normalizer = maximum + static_cast<float>(std::log(denominator));
    for (float& value : log_probabilities) value -= log_normalizer;
    return common::Status::success();
}

} // namespace morok::g3::neural
