#include "morok/g3/neural/gru_backend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace morok::g3::neural {
namespace {

constexpr std::size_t kStateFloats = 2U * kHiddenDim;
constexpr std::size_t kStateBytes = kStateFloats * sizeof(float);

[[nodiscard]] common::Status fail(common::ErrorCode code,
                                  std::string_view stage,
                                  std::string_view message) {
    return common::Status::failure(common::Error::make(
        code, std::string(message), "g3.neural", std::string(stage)));
}

void initialize_xavier(std::vector<float>& values, std::size_t rows,
                       std::size_t columns, std::mt19937_64& random) {
    const float bound = std::sqrt(6.0F /
        static_cast<float>(rows + columns));
    std::uniform_real_distribution<float> distribution(-bound, bound);
    for (float& value : values) value = distribution(random);
}

// Initializes each HxH recurrent gate matrix independently with an
// orthogonal basis. Modified Gram-Schmidt is performed in double precision.
void initialize_xavier_gates(std::vector<float>& values,
                              std::size_t input_dim,
                              std::mt19937_64& random) {
    const float bound = std::sqrt(6.0F /
        static_cast<float>(kHiddenDim + input_dim));
    std::uniform_real_distribution<float> distribution(-bound, bound);
    for (float& value : values) value = distribution(random);
}

void initialize_orthogonal_gates(std::vector<float>& values,
                                 std::mt19937_64& random) {
    std::normal_distribution<double> normal(0.0, 1.0);
    std::array<double, kHiddenDim * kHiddenDim> matrix{};
    for (std::size_t gate = 0; gate < kGateCount; ++gate) {
        for (double& value : matrix) value = normal(random);
        for (std::size_t column = 0; column < kHiddenDim; ++column) {
            for (std::size_t previous = 0; previous < column; ++previous) {
                double dot = 0.0;
                for (std::size_t row = 0; row < kHiddenDim; ++row) {
                    dot += matrix[row * kHiddenDim + column] *
                           matrix[row * kHiddenDim + previous];
                }
                for (std::size_t row = 0; row < kHiddenDim; ++row) {
                    matrix[row * kHiddenDim + column] -=
                        dot * matrix[row * kHiddenDim + previous];
                }
            }
            double norm_squared = 0.0;
            for (std::size_t row = 0; row < kHiddenDim; ++row) {
                const double value = matrix[row * kHiddenDim + column];
                norm_squared += value * value;
            }
            const double norm = std::sqrt(norm_squared);
            if (!(norm > 1.0e-12)) {
                for (std::size_t row = 0; row < kHiddenDim; ++row) {
                    matrix[row * kHiddenDim + column] =
                        row == column ? 1.0 : 0.0;
                }
            } else {
                for (std::size_t row = 0; row < kHiddenDim; ++row) {
                    matrix[row * kHiddenDim + column] /= norm;
                }
            }
        }
        const std::size_t offset = gate * kHiddenDim * kHiddenDim;
        for (std::size_t i = 0; i < matrix.size(); ++i) {
            values[offset + i] = static_cast<float>(matrix[i]);
        }
    }
}

[[nodiscard]] G3Weights initialize_weights(std::uint64_t seed) {
    G3Weights weights;
    weights.embedding.resize(kVocabularySize * kEmbeddingDim);
    weights.layer1_input.resize(kGateCount * kHiddenDim * kEmbeddingDim);
    weights.layer1_recurrent.resize(kGateCount * kHiddenDim * kHiddenDim);
    weights.layer1_bias.assign(kGateCount * kHiddenDim, 0.0F);
    weights.layer2_input.resize(kGateCount * kHiddenDim * kHiddenDim);
    weights.layer2_recurrent.resize(kGateCount * kHiddenDim * kHiddenDim);
    weights.layer2_bias.assign(kGateCount * kHiddenDim, 0.0F);
    weights.rms_scale.assign(kHiddenDim, 1.0F);
    weights.projection.resize(kEmbeddingDim * kHiddenDim);
    weights.projection_bias.assign(kEmbeddingDim, 0.0F);
    weights.output_bias.assign(kVocabularySize, 0.0F);

    std::mt19937_64 random(seed);
    initialize_xavier(weights.embedding, kVocabularySize, kEmbeddingDim, random);
    initialize_xavier_gates(weights.layer1_input, kEmbeddingDim, random);
    initialize_orthogonal_gates(weights.layer1_recurrent, random);
    initialize_xavier_gates(weights.layer2_input, kHiddenDim, random);
    initialize_orthogonal_gates(weights.layer2_recurrent, random);
    initialize_xavier(weights.projection, kEmbeddingDim, kHiddenDim, random);

    // Gate order is fixed as update, reset, candidate. Positive update bias
    // initially favors retaining the previous hidden state.
    std::fill_n(weights.layer1_bias.begin(), kHiddenDim, 1.0F);
    std::fill_n(weights.layer2_bias.begin(), kHiddenDim, 1.0F);
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
              std::array<float, kHiddenDim>& output) {
    std::array<float, kGateCount * kHiddenDim> gates{};
    for (std::size_t gate = 0; gate < 2U; ++gate) {
        for (std::size_t row = 0; row < kHiddenDim; ++row) {
            float sum = bias[gate * kHiddenDim + row];
            const std::size_t input_offset =
                (gate * kHiddenDim + row) * input.size();
            for (std::size_t column = 0; column < input.size(); ++column) {
                sum += input_weights[input_offset + column] * input[column];
            }
            const std::size_t recurrent_offset =
                (gate * kHiddenDim + row) * kHiddenDim;
            for (std::size_t column = 0; column < kHiddenDim; ++column) {
                sum += recurrent_weights[recurrent_offset + column] *
                       previous[column];
            }
            gates[gate * kHiddenDim + row] = sigmoid(sum);
        }
    }

    for (std::size_t row = 0; row < kHiddenDim; ++row) {
        float sum = bias[2U * kHiddenDim + row];
        const std::size_t input_offset =
            (2U * kHiddenDim + row) * input.size();
        for (std::size_t column = 0; column < input.size(); ++column) {
            sum += input_weights[input_offset + column] * input[column];
        }
        const std::size_t recurrent_offset =
            (2U * kHiddenDim + row) * kHiddenDim;
        for (std::size_t column = 0; column < kHiddenDim; ++column) {
            const float reset_hidden =
                gates[kHiddenDim + column] * previous[column];
            sum += recurrent_weights[recurrent_offset + column] *
                   reset_hidden;
        }
        gates[2U * kHiddenDim + row] = std::tanh(sum);
    }

    for (std::size_t i = 0; i < kHiddenDim; ++i) {
        const float update = gates[i];
        const float candidate = gates[2U * kHiddenDim + i];
        output[i] = (1.0F - update) * candidate + update * previous[i];
    }
}

void rms_normalize(std::span<const float> input,
                   const std::vector<float>& scale, float epsilon,
                   std::array<float, kHiddenDim>& output) {
    double sum_squares = 0.0;
    for (float value : input) {
        sum_squares += static_cast<double>(value) * value;
    }
    const float inverse_rms = static_cast<float>(
        1.0 / std::sqrt(sum_squares / static_cast<double>(kHiddenDim) +
                        static_cast<double>(epsilon)));
    for (std::size_t i = 0; i < kHiddenDim; ++i) {
        output[i] = input[i] * inverse_rms * scale[i];
    }
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
      rms_epsilon_(config.rms_epsilon) {
    if (!std::isfinite(rms_epsilon_) || rms_epsilon_ <= 0.0F) {
        throw std::invalid_argument("G3 RMSNorm epsilon must be finite and positive");
    }
}

model::ModelDescriptor G3Backend::descriptor() const noexcept {
    return {3U, kArchitectureId, static_cast<std::uint32_t>(kVocabularySize),
            parameter_count(), static_cast<std::uint32_t>(kStateBytes),
            static_cast<std::uint32_t>(alignof(float)), true};
}

common::Status G3Backend::reset_state(std::span<std::byte> state) const {
    if (state.size() != kStateBytes) {
        return fail(common::ErrorCode::invalid_argument, "reset_state",
                    "G3 state must contain exactly two FP32 hidden vectors");
    }
    std::fill(state.begin(), state.end(), std::byte{0});
    return common::Status::success();
}

common::Status G3Backend::consume_byte(std::span<std::byte> state,
                                       std::uint8_t byte) const {
    if (state.size() != kStateBytes) {
        return fail(common::ErrorCode::invalid_argument, "consume_byte",
                    "G3 state has an invalid size");
    }
    if (weights_.embedding.size() != kVocabularySize * kEmbeddingDim ||
        weights_.layer1_input.size() != kGateCount * kHiddenDim * kEmbeddingDim ||
        weights_.layer1_recurrent.size() != kGateCount * kHiddenDim * kHiddenDim ||
        weights_.layer1_bias.size() != kGateCount * kHiddenDim ||
        weights_.layer2_input.size() != kGateCount * kHiddenDim * kHiddenDim ||
        weights_.layer2_recurrent.size() != kGateCount * kHiddenDim * kHiddenDim ||
        weights_.layer2_bias.size() != kGateCount * kHiddenDim ||
        weights_.rms_scale.size() != kHiddenDim) {
        return fail(common::ErrorCode::invalid_argument, "consume_byte",
                    "G3 weight tensor dimensions are invalid");
    }

    std::array<float, kStateFloats> previous{};
    std::memcpy(previous.data(), state.data(), kStateBytes);
    const std::span<const float> input_embedding(
        weights_.embedding.data() + static_cast<std::size_t>(byte) * kEmbeddingDim,
        kEmbeddingDim);
    std::array<float, kHiddenDim> layer1{};
    std::array<float, kHiddenDim> normalized{};
    std::array<float, kHiddenDim> layer2{};
    gru_step(input_embedding,
             std::span<const float>(previous.data(), kHiddenDim),
             weights_.layer1_input, weights_.layer1_recurrent,
             weights_.layer1_bias, layer1);
    rms_normalize(layer1, weights_.rms_scale, rms_epsilon_, normalized);
    gru_step(normalized,
             std::span<const float>(previous.data() + kHiddenDim, kHiddenDim),
             weights_.layer2_input, weights_.layer2_recurrent,
             weights_.layer2_bias, layer2);

    for (float value : layer1) {
        if (!std::isfinite(value)) {
            return fail(common::ErrorCode::internal_error, "consume_byte",
                        "G3 first recurrent layer produced a non-finite state");
        }
    }
    for (float value : layer2) {
        if (!std::isfinite(value)) {
            return fail(common::ErrorCode::internal_error, "consume_byte",
                        "G3 second recurrent layer produced a non-finite state");
        }
    }
    std::array<float, kStateFloats> next{};
    std::copy(layer1.begin(), layer1.end(), next.begin());
    std::copy(layer2.begin(), layer2.end(), next.begin() + kHiddenDim);
    std::memcpy(state.data(), next.data(), kStateBytes);
    return common::Status::success();
}

common::Status G3Backend::next_token_log_probabilities(
    std::span<const std::byte> state,
    std::span<float> log_probabilities) const {
    if (state.size() != kStateBytes ||
        log_probabilities.size() != kVocabularySize) {
        return fail(common::ErrorCode::invalid_argument, "next_token",
                    "G3 state or output dimensions are invalid");
    }
    if (weights_.embedding.size() != kVocabularySize * kEmbeddingDim ||
        weights_.projection.size() != kEmbeddingDim * kHiddenDim ||
        weights_.projection_bias.size() != kEmbeddingDim ||
        weights_.output_bias.size() != kVocabularySize) {
        return fail(common::ErrorCode::invalid_argument, "next_token",
                    "G3 output weight tensor dimensions are invalid");
    }

    std::array<float, kStateFloats> hidden{};
    std::memcpy(hidden.data(), state.data(), kStateBytes);
    std::array<float, kEmbeddingDim> projected{};
    const float* final_hidden = hidden.data() + kHiddenDim;
    for (std::size_t row = 0; row < kEmbeddingDim; ++row) {
        float value = weights_.projection_bias[row];
        const std::size_t offset = row * kHiddenDim;
        for (std::size_t column = 0; column < kHiddenDim; ++column) {
            value += weights_.projection[offset + column] * final_hidden[column];
        }
        projected[row] = value;
    }

    float maximum = -std::numeric_limits<float>::infinity();
    for (std::size_t token = 0; token < kVocabularySize; ++token) {
        float logit = weights_.output_bias[token];
        const std::size_t offset = token * kEmbeddingDim;
        for (std::size_t dimension = 0; dimension < kEmbeddingDim; ++dimension) {
            logit += weights_.embedding[offset + dimension] * projected[dimension];
        }
        if (!std::isfinite(logit)) {
            return fail(common::ErrorCode::internal_error, "next_token",
                        "G3 produced a non-finite output logit");
        }
        log_probabilities[token] = logit;
        maximum = std::max(maximum, logit);
    }

    double denominator = 0.0;
    for (float logit : log_probabilities) {
        denominator += std::exp(static_cast<double>(logit - maximum));
    }
    if (!(denominator > 0.0) || !std::isfinite(denominator)) {
        return fail(common::ErrorCode::internal_error, "next_token",
                    "G3 softmax normalization is invalid");
    }
    const float log_normalizer =
        maximum + static_cast<float>(std::log(denominator));
    for (float& value : log_probabilities) value -= log_normalizer;
    return common::Status::success();
}

} // namespace morok::g3::neural
