#pragma once

#include "morok/model/model_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace morok::g3::neural {

inline constexpr std::size_t kVocabularySize = 261U;
inline constexpr std::size_t kEmbeddingDim = 60U;
inline constexpr std::size_t kLayer1HiddenDim = 97U;
inline constexpr std::size_t kLayer2HiddenDim = 68U;
inline constexpr std::size_t kRecurrentLayers = 2U;
inline constexpr std::size_t kGateCount = 3U;
inline constexpr std::size_t kTargetParameterCount = 100000U;
inline constexpr std::size_t kConfiguredParameterCount =
    kVocabularySize * kEmbeddingDim +
    kGateCount * kLayer1HiddenDim * kEmbeddingDim +
    kGateCount * kLayer1HiddenDim * kLayer1HiddenDim +
    kGateCount * kLayer1HiddenDim +
    kGateCount * kLayer2HiddenDim * kLayer1HiddenDim +
    kGateCount * kLayer2HiddenDim * kLayer2HiddenDim +
    kGateCount * kLayer2HiddenDim +
    kLayer1HiddenDim +
    kEmbeddingDim * kLayer2HiddenDim +
    kEmbeddingDim + kVocabularySize;
static_assert(kConfiguredParameterCount == kTargetParameterCount,
              "G3 architecture must remain exactly 100,000 parameters");
inline constexpr std::uint16_t kArchitectureId = 0x0302U;

struct G3Config final {
    std::uint64_t seed{0x4D4F524F4B4733ULL};
    float rms_epsilon{1.0e-5F};
};

// All matrices are contiguous row-major arrays. The output head reads the
// transpose of embedding; no second vocabulary matrix is allocated.
struct G3Weights final {
    std::vector<float> embedding;          // [V, E]
    std::vector<float> layer1_input;       // [3*H1, E]
    std::vector<float> layer1_recurrent;   // [3*H1, H1]
    std::vector<float> layer1_bias;        // [3*H1]
    std::vector<float> layer2_input;       // [3*H2, H1]
    std::vector<float> layer2_recurrent;   // [3*H2, H2]
    std::vector<float> layer2_bias;        // [3*H2]
    std::vector<float> rms_scale;          // [H1]
    std::vector<float> projection;         // [E, H2]
    std::vector<float> projection_bias;    // [E]
    std::vector<float> output_bias;        // [V]
};

struct G3MutableWeightsView final {
    std::span<float> embedding;
    std::span<float> layer1_input;
    std::span<float> layer1_recurrent;
    std::span<float> layer1_bias;
    std::span<float> layer2_input;
    std::span<float> layer2_recurrent;
    std::span<float> layer2_bias;
    std::span<float> rms_scale;
    std::span<float> projection;
    std::span<float> projection_bias;
    std::span<float> output_bias;
};

[[nodiscard]] std::uint64_t count_parameters(
    const G3Weights& weights) noexcept;

class G3Backend final : public model::ModelBackend {
public:
    explicit G3Backend(G3Config config = {});

    [[nodiscard]] model::ModelDescriptor descriptor() const noexcept override;
    [[nodiscard]] common::Status reset_state(
        std::span<std::byte> state) const override;
    [[nodiscard]] common::Status consume_byte(
        std::span<std::byte> state, std::uint8_t byte) const override;
    [[nodiscard]] common::Status consume_token(
        std::span<std::byte> state, std::uint16_t token_id) const override;
    [[nodiscard]] common::Status next_token_log_probabilities(
        std::span<const std::byte> state,
        std::span<float> log_probabilities) const override;

    [[nodiscard]] const G3Weights& weights() const noexcept { return weights_; }
    // Trainers may update tensor values but cannot resize tensors or change
    // the fixed 100,000-parameter architecture through this view.
    [[nodiscard]] G3MutableWeightsView mutable_weights() noexcept {
        return {weights_.embedding, weights_.layer1_input,
                weights_.layer1_recurrent, weights_.layer1_bias,
                weights_.layer2_input, weights_.layer2_recurrent,
                weights_.layer2_bias, weights_.rms_scale,
                weights_.projection, weights_.projection_bias,
                weights_.output_bias};
    }
    [[nodiscard]] std::uint64_t parameter_count() const noexcept {
        return count_parameters(weights_);
    }
    [[nodiscard]] float rms_epsilon() const noexcept { return rms_epsilon_; }
    [[nodiscard]] std::uint64_t initialization_seed() const noexcept { return seed_; }
    [[nodiscard]] static constexpr std::string_view initialization_method() noexcept {
        return "xavier_glorot+orthogonal_per_gate_gram_schmidt";
    }

private:
    G3Weights weights_;
    float rms_epsilon_{1.0e-5F};
    std::uint64_t seed_{0};
};

} // namespace morok::g3::neural
