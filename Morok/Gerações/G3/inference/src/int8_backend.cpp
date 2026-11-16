#include "morok/g3/inference/int8_backend.hpp"

#include "morok/common/error.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace morok::g3::inference {
namespace {

constexpr std::size_t kStateFloats =
    neural::kLayer1HiddenDim + neural::kLayer2HiddenDim;
constexpr std::size_t kStateBytes = kStateFloats * sizeof(float);
constexpr std::size_t kScaleCount =
    neural::kVocabularySize +
    2U * neural::kGateCount * neural::kLayer1HiddenDim +
    2U * neural::kGateCount * neural::kLayer2HiddenDim +
    neural::kEmbeddingDim;
constexpr std::size_t kMatrixParameterCount =
    neural::kVocabularySize * neural::kEmbeddingDim +
    neural::kGateCount * neural::kLayer1HiddenDim * neural::kEmbeddingDim +
    neural::kGateCount * neural::kLayer1HiddenDim * neural::kLayer1HiddenDim +
    neural::kGateCount * neural::kLayer2HiddenDim * neural::kLayer1HiddenDim +
    neural::kGateCount * neural::kLayer2HiddenDim * neural::kLayer2HiddenDim +
    neural::kEmbeddingDim * neural::kLayer2HiddenDim;
constexpr std::size_t kAuxiliaryParameterCount =
    neural::kTargetParameterCount - kMatrixParameterCount;

common::Error make_error(common::ErrorCode code, const char* stage,
                         const char* message) {
    return common::Error::make(code, message, "g3.int8", stage);
}

template <typename T>
common::Result<T> failure(common::ErrorCode code, const char* stage,
                          const char* message) {
    return common::Result<T>::failure(make_error(code, stage, message));
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4U; ++i)
        out.push_back(static_cast<std::byte>((value >> (i * 8U)) & 0xffU));
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t& offset) {
    if (bytes.size() - offset < 4U) throw std::out_of_range("truncated FP32 payload");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4U; ++i)
        value |= static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(bytes[offset++])) << (i * 8U);
    return value;
}

void append_tensor(std::vector<std::byte>& out, const QuantizedTensor& tensor) {
    for (const auto value : tensor.values)
        out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(value)));
    for (const float scale : tensor.scales)
        append_u32(out, std::bit_cast<std::uint32_t>(scale));
}

QuantizedTensor read_tensor(std::span<const std::byte> bytes,
                            std::size_t& offset,
                            std::size_t rows, std::size_t columns) {
    QuantizedTensor tensor;
    tensor.rows = rows;
    tensor.columns = columns;
    const std::size_t count = rows * columns;
    if (count > bytes.size() - offset)
        throw std::out_of_range("truncated INT8 tensor");
    tensor.values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = std::bit_cast<std::int8_t>(
            std::to_integer<std::uint8_t>(bytes[offset++]));
        if (value == std::numeric_limits<std::int8_t>::min())
            throw std::invalid_argument("INT8 value -128 is outside symmetric range");
        tensor.values.push_back(value);
    }
    if (rows > (bytes.size() - offset) / sizeof(float))
        throw std::out_of_range("truncated INT8 scales");
    tensor.scales.reserve(rows);
    for (std::size_t i = 0; i < rows; ++i) {
        const float scale = std::bit_cast<float>(read_u32(bytes, offset));
        if (!std::isfinite(scale) || scale <= 0.0F)
            throw std::invalid_argument("INT8 scale must be finite and positive");
        tensor.scales.push_back(scale);
    }
    return tensor;
}

void append_float_vector(std::vector<std::byte>& out,
                         const std::vector<float>& values) {
    for (float value : values)
        append_u32(out, std::bit_cast<std::uint32_t>(value));
}

std::vector<float> read_float_vector(std::span<const std::byte> bytes,
                                     std::size_t& offset,
                                     std::size_t count) {
    if (count > (bytes.size() - offset) / sizeof(float))
        throw std::out_of_range("truncated FP32 auxiliary tensor");
    std::vector<float> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float value = std::bit_cast<float>(read_u32(bytes, offset));
        if (!std::isfinite(value))
            throw std::invalid_argument("FP32 auxiliary tensor contains non-finite value");
        values.push_back(value);
    }
    return values;
}

float quantized_dot(const QuantizedTensor& tensor, std::size_t row,
                    std::span<const float> input) noexcept {
    const std::size_t offset = row * tensor.columns;
    float sum = 0.0F;
    for (std::size_t column = 0; column < tensor.columns; ++column)
        sum += static_cast<float>(tensor.values[offset + column]) * input[column];
    return sum * tensor.scales[row];
}

float sigmoid(float value) noexcept {
    if (value >= 0.0F) {
        const float exponential = std::exp(-value);
        return 1.0F / (1.0F + exponential);
    }
    const float exponential = std::exp(value);
    return exponential / (1.0F + exponential);
}

void gru_int8_step(std::span<const float> input,
                   std::span<const float> previous,
                   const QuantizedTensor& input_weights,
                   const QuantizedTensor& recurrent_weights,
                   const std::vector<float>& bias,
                   std::span<float> output) noexcept {
    const std::size_t hidden = output.size();
    std::array<float, neural::kGateCount * neural::kLayer1HiddenDim> gates{};
    std::array<float, neural::kLayer1HiddenDim> reset_previous{};

    for (std::size_t row = 0; row < hidden; ++row) {
        // Match the FP32 backend gate convention: gate 0 is update,
        // gate 1 is reset and gates the recurrent candidate input.
        gates[row] = sigmoid(bias[row] +
            quantized_dot(input_weights, row, input) +
            quantized_dot(recurrent_weights, row, previous));
        gates[hidden + row] = sigmoid(bias[hidden + row] +
            quantized_dot(input_weights, hidden + row, input) +
            quantized_dot(recurrent_weights, hidden + row, previous));
        reset_previous[row] = gates[hidden + row] * previous[row];
    }

    const std::span<const float> reset_state(reset_previous.data(), hidden);
    const std::size_t candidate_offset = 2U * hidden;
    for (std::size_t row = 0; row < hidden; ++row) {
        const float candidate = std::tanh(
            bias[candidate_offset + row] +
            quantized_dot(input_weights, candidate_offset + row, input) +
            quantized_dot(recurrent_weights, candidate_offset + row, reset_state));
        const float update = gates[row];
        output[row] = (1.0F - update) * candidate + update * previous[row];
    }
}

void rms_normalize(std::span<const float> input,
                   const std::vector<float>& scale,
                   std::span<float> output) noexcept {
    double sum_squares = 0.0;
    for (float value : input) sum_squares += static_cast<double>(value) * value;
    const float inverse_rms = static_cast<float>(
        1.0 / std::sqrt(sum_squares / static_cast<double>(input.size()) + 1.0e-5));
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = input[i] * inverse_rms * scale[i];
}

} // namespace

G3Int8Backend::G3Int8Backend(const ModelArtifact& artifact) {
    if (artifact.metadata.precision != PrecisionMode::int8_symmetric_per_row ||
        artifact.metadata.scale_count != kScaleCount ||
        artifact.metadata.parameter_count != neural::kTargetParameterCount)
        throw std::invalid_argument("Artifact is not a compatible G3 symmetric INT8 model");

    std::size_t offset = 0U;
    const auto bytes = std::span<const std::byte>(artifact.payload);
    embedding_ = read_tensor(bytes, offset, neural::kVocabularySize,
                             neural::kEmbeddingDim);
    layer1_input_ = read_tensor(bytes, offset,
        neural::kGateCount * neural::kLayer1HiddenDim, neural::kEmbeddingDim);
    layer1_recurrent_ = read_tensor(bytes, offset,
        neural::kGateCount * neural::kLayer1HiddenDim, neural::kLayer1HiddenDim);
    layer2_input_ = read_tensor(bytes, offset,
        neural::kGateCount * neural::kLayer2HiddenDim, neural::kLayer1HiddenDim);
    layer2_recurrent_ = read_tensor(bytes, offset,
        neural::kGateCount * neural::kLayer2HiddenDim, neural::kLayer2HiddenDim);
    projection_ = read_tensor(bytes, offset, neural::kEmbeddingDim,
                              neural::kLayer2HiddenDim);

    layer1_bias_ = read_float_vector(bytes, offset,
        neural::kGateCount * neural::kLayer1HiddenDim);
    layer2_bias_ = read_float_vector(bytes, offset,
        neural::kGateCount * neural::kLayer2HiddenDim);
    rms_scale_ = read_float_vector(bytes, offset, neural::kLayer1HiddenDim);
    projection_bias_ = read_float_vector(bytes, offset, neural::kEmbeddingDim);
    output_bias_ = read_float_vector(bytes, offset, neural::kVocabularySize);
    if (offset != bytes.size())
        throw std::invalid_argument("INT8 artifact contains trailing payload bytes");
}

model::ModelDescriptor G3Int8Backend::descriptor() const noexcept {
    return {3U, neural::kArchitectureId,
            static_cast<std::uint32_t>(neural::kVocabularySize),
            neural::kTargetParameterCount, static_cast<std::uint32_t>(kStateBytes),
            static_cast<std::uint32_t>(alignof(float)), true};
}

common::Status G3Int8Backend::reset_state(std::span<std::byte> state) const {
    if (state.size() != kStateBytes)
        return common::Status::failure(make_error(common::ErrorCode::invalid_argument,
            "reset_state", "G3 INT8 state must contain both hidden vectors"));
    std::fill(state.begin(), state.end(), std::byte{0});
    return common::Status::success();
}

common::Status G3Int8Backend::consume_byte(std::span<std::byte> state,
                                           std::uint8_t byte) const {
    if (state.size() != kStateBytes)
        return common::Status::failure(make_error(common::ErrorCode::invalid_argument,
            "consume_byte", "G3 INT8 state has an invalid size"));
    std::array<float, kStateFloats> previous{};
    std::memcpy(previous.data(), state.data(), kStateBytes);

    std::array<float, neural::kEmbeddingDim> embedded{};
    const std::size_t embedding_offset = static_cast<std::size_t>(byte) *
                                         neural::kEmbeddingDim;
    for (std::size_t i = 0; i < neural::kEmbeddingDim; ++i)
        embedded[i] = static_cast<float>(embedding_.values[embedding_offset + i]) *
                       embedding_.scales[byte];

    const std::span<const float> previous1(previous.data(), neural::kLayer1HiddenDim);
    const std::span<const float> previous2(previous.data() + neural::kLayer1HiddenDim,
                                           neural::kLayer2HiddenDim);
    std::array<float, neural::kLayer1HiddenDim> layer1{};
    std::array<float, neural::kLayer1HiddenDim> normalized{};
    std::array<float, neural::kLayer2HiddenDim> layer2{};

    gru_int8_step(embedded, previous1, layer1_input_, layer1_recurrent_,
                  layer1_bias_, layer1);
    rms_normalize(layer1, rms_scale_, normalized);
    gru_int8_step(normalized, previous2, layer2_input_, layer2_recurrent_,
                  layer2_bias_, layer2);

    for (float value : layer1)
        if (!std::isfinite(value))
            return common::Status::failure(make_error(common::ErrorCode::internal_error,
                "consume.layer1", "G3 INT8 first layer produced non-finite state"));
    for (float value : layer2)
        if (!std::isfinite(value))
            return common::Status::failure(make_error(common::ErrorCode::internal_error,
                "consume.layer2", "G3 INT8 second layer produced non-finite state"));

    std::memcpy(state.data(), layer1.data(), layer1.size() * sizeof(float));
    std::memcpy(state.data() + layer1.size() * sizeof(float),
                layer2.data(), layer2.size() * sizeof(float));
    return common::Status::success();
}

common::Status G3Int8Backend::next_token_log_probabilities(
    std::span<const std::byte> state,
    std::span<float> log_probabilities) const {
    if (state.size() != kStateBytes ||
        log_probabilities.size() != neural::kVocabularySize)
        return common::Status::failure(make_error(common::ErrorCode::invalid_argument,
            "next_token", "G3 INT8 state or output dimensions are invalid"));

    std::array<float, kStateFloats> hidden{};
    std::memcpy(hidden.data(), state.data(), kStateBytes);
    const std::span<const float> final_hidden(
        hidden.data() + neural::kLayer1HiddenDim, neural::kLayer2HiddenDim);
    std::array<float, neural::kEmbeddingDim> projected{};
    for (std::size_t row = 0; row < neural::kEmbeddingDim; ++row)
        projected[row] = projection_bias_[row] +
                         quantized_dot(projection_, row, final_hidden);

    float maximum = -std::numeric_limits<float>::infinity();
    for (std::size_t token = 0; token < neural::kVocabularySize; ++token) {
        const std::span<const float> projected_span(projected);
        const float logit = output_bias_[token] +
                            quantized_dot(embedding_, token, projected_span);
        if (!std::isfinite(logit))
            return common::Status::failure(make_error(common::ErrorCode::internal_error,
                "next_token", "G3 INT8 output produced a non-finite logit"));
        log_probabilities[token] = logit;
        maximum = std::max(maximum, logit);
    }

    double denominator = 0.0;
    for (float logit : log_probabilities)
        denominator += std::exp(static_cast<double>(logit - maximum));
    if (!(denominator > 0.0) || !std::isfinite(denominator))
        return common::Status::failure(make_error(common::ErrorCode::internal_error,
            "next_token.softmax", "G3 INT8 softmax normalizer is invalid"));
    const float log_normalizer = maximum + static_cast<float>(std::log(denominator));
    for (float& value : log_probabilities) value -= log_normalizer;
    return common::Status::success();
}

common::Result<ModelArtifact> export_g3_int8_artifact(
    const neural::G3Backend& backend) {
    if (backend.parameter_count() != neural::kTargetParameterCount)
        return failure<ModelArtifact>(common::ErrorCode::model_version_mismatch,
            "export.architecture", "G3 backend does not match the fixed 100000-parameter architecture");
    const auto& w = backend.weights();
    const auto embedding = quantize_per_row(w.embedding, neural::kVocabularySize,
                                             neural::kEmbeddingDim);
    const auto layer1_input = quantize_per_row(w.layer1_input,
        neural::kGateCount * neural::kLayer1HiddenDim, neural::kEmbeddingDim);
    const auto layer1_recurrent = quantize_per_row(w.layer1_recurrent,
        neural::kGateCount * neural::kLayer1HiddenDim, neural::kLayer1HiddenDim);
    const auto layer2_input = quantize_per_row(w.layer2_input,
        neural::kGateCount * neural::kLayer2HiddenDim, neural::kLayer1HiddenDim);
    const auto layer2_recurrent = quantize_per_row(w.layer2_recurrent,
        neural::kGateCount * neural::kLayer2HiddenDim, neural::kLayer2HiddenDim);
    const auto projection = quantize_per_row(w.projection,
        neural::kEmbeddingDim, neural::kLayer2HiddenDim);
    if (!embedding || !layer1_input || !layer1_recurrent || !layer2_input ||
        !layer2_recurrent || !projection)
        return failure<ModelArtifact>(common::ErrorCode::invalid_argument,
            "export.quantize", "Unable to quantize one or more G3 matrix tensors");

    ModelArtifact artifact;
    artifact.metadata.precision = PrecisionMode::int8_symmetric_per_row;
    artifact.metadata.state_size_bytes = static_cast<std::uint32_t>(kStateBytes);
    artifact.metadata.parameter_count = neural::kTargetParameterCount;
    artifact.metadata.scale_count = static_cast<std::uint32_t>(kScaleCount);
    auto& payload = artifact.payload;
    payload.reserve(kMatrixParameterCount + kScaleCount * sizeof(float) +
                    kAuxiliaryParameterCount * sizeof(float));
    append_tensor(payload, *embedding.value_if());
    append_tensor(payload, *layer1_input.value_if());
    append_tensor(payload, *layer1_recurrent.value_if());
    append_tensor(payload, *layer2_input.value_if());
    append_tensor(payload, *layer2_recurrent.value_if());
    append_tensor(payload, *projection.value_if());
    append_float_vector(payload, w.layer1_bias);
    append_float_vector(payload, w.layer2_bias);
    append_float_vector(payload, w.rms_scale);
    append_float_vector(payload, w.projection_bias);
    append_float_vector(payload, w.output_bias);
    const auto verified = serialize_model_artifact(artifact);
    if (!verified)
        return common::Result<ModelArtifact>::failure(*verified.error_if());
    return common::Result<ModelArtifact>::success(std::move(artifact));
}

common::Result<std::unique_ptr<G3Int8Backend>> load_g3_int8_backend(
    const ModelArtifact& artifact) {
    const auto serialized = serialize_model_artifact(artifact);
    if (!serialized)
        return common::Result<std::unique_ptr<G3Int8Backend>>::failure(
            *serialized.error_if());
    try {
        return common::Result<std::unique_ptr<G3Int8Backend>>::success(
            std::make_unique<G3Int8Backend>(artifact));
    } catch (const std::exception&) {
        return failure<std::unique_ptr<G3Int8Backend>>(
            common::ErrorCode::model_version_mismatch, "load.backend",
            "Unable to construct G3 INT8 backend from the supplied artifact");
    }
}

} // namespace morok::g3::inference
