#pragma once

#include "morok/model/model_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace morok::g3::neural {

inline constexpr std::size_t kVocabularySize = 261U;
inline constexpr std::size_t kEmbeddingDim = 64U;
inline constexpr std::size_t kHiddenDim = 128U;
inline constexpr std::size_t kRecurrentLayers = 2U;
inline constexpr std::size_t kGateCount = 3U;
inline constexpr std::size_t kReferenceParameterCount = 198149U;
inline constexpr std::uint16_t kArchitectureId = 0x0301U;

struct G3Config final {
    std::uint64_t seed{0x4D4F524F4B4733ULL};
    float rms_epsilon{1.0e-5F};
};

// All matrices are contiguous row-major arrays. The output head deliberately
// has no second vocabulary matrix: it reads the transpose of embedding.
struct G3Weights final {
    std::vector<float> embedding;          // [V, E]
    std::vector<float> layer1_input;       // [3H, E]
    std::vector<float> layer1_recurrent;   // [3H, H]
    std::vector<float> layer1_bias;        // [3H]
    std::vector<float> layer2_input;       // [3H, H]
    std::vector<float> layer2_recurrent;   // [3H, H]
    std::vector<float> layer2_bias;        // [3H]
    std::vector<float> rms_scale;          // [H]
    std::vector<float> projection;         // [E, H]
    std::vector<float> projection_bias;    // [E]
    std::vector<float> output_bias;        // [V]
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
        std::span<std::byte> state, std::uint16_t token_id) const;
    [[nodiscard]] common::Status next_token_log_probabilities(
        std::span<const std::byte> state,
        std::span<float> log_probabilities) const override;

    [[nodiscard]] const G3Weights& weights() const noexcept { return weights_; }
    // Mutable access is intentionally explicit for generation-owned trainers.
    // The embedding vector is the sole vocabulary matrix used by both lookup
    // and output projection, so weight tying cannot silently drift apart.
    [[nodiscard]] G3Weights& mutable_weights() noexcept { return weights_; }
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
