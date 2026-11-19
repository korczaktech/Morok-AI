#pragma once

#include "morok/common/result.hpp"
#include "morok/g3/inference/model_artifact.hpp"
#include "morok/g3/inference/quantization.hpp"
#include "morok/g3/neural/gru_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace morok::g3::inference {

// CPU inference backend that keeps all six G3 matrix tensors in symmetric
// per-output-row INT8. Biases and RMSNorm scale remain FP32.
class G3Int8Backend final : public model::ModelBackend {
public:
    explicit G3Int8Backend(const ModelArtifact& artifact);

    [[nodiscard]] model::ModelDescriptor descriptor() const noexcept override;
    [[nodiscard]] common::Status reset_state(std::span<std::byte> state) const override;
    [[nodiscard]] common::Status consume_byte(std::span<std::byte> state,
                                               std::uint8_t byte) const override;
    [[nodiscard]] common::Status consume_token(std::span<std::byte> state,
                                                std::uint16_t token_id) const override;
    [[nodiscard]] common::Status next_token_log_probabilities(
        std::span<const std::byte> state,
        std::span<float> log_probabilities) const override;

private:
    QuantizedTensor embedding_;
    QuantizedTensor layer1_input_;
    QuantizedTensor layer1_recurrent_;
    QuantizedTensor layer2_input_;
    QuantizedTensor layer2_recurrent_;
    QuantizedTensor projection_;
    std::vector<float> layer1_bias_;
    std::vector<float> layer2_bias_;
    std::vector<float> rms_scale_;
    std::vector<float> projection_bias_;
    std::vector<float> output_bias_;
};

[[nodiscard]] common::Result<ModelArtifact> export_g3_int8_artifact(
    const neural::G3Backend& backend);

[[nodiscard]] common::Result<std::unique_ptr<G3Int8Backend>> load_g3_int8_backend(
    const ModelArtifact& artifact);

} // namespace morok::g3::inference
