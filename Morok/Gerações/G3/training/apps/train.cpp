#include "morok/g3/training/trainer.hpp"
#include "morok/g3/inference/model_artifact.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void append_fp32_little_endian(const std::vector<float>& tensor,
                               std::vector<std::byte>& payload) {
    for (const float value : tensor) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned shift = 0; shift < 32U; shift += 8U)
            payload.push_back(static_cast<std::byte>((bits >> shift) & 0xffU));
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc > 6) {
        std::cerr << "Uso: morok-g3-train <dataset.jsonl> <metrics.jsonl> "
                     "<modelo.morok> [epocas] [seed]\n";
        return 2;
    }
    try {
        auto dataset = morok::g3::training::load_jsonl(argv[1]);
        morok::g3::training::TrainingConfig config;
        config.metrics_jsonl = argv[2];
        if (argc >= 5) config.epochs = static_cast<std::size_t>(std::stoull(argv[4]));
        if (argc >= 6) config.seed = std::stoull(argv[5]);
        morok::g3::neural::G3Backend backend(
            morok::g3::neural::G3Config{config.seed, 1.0e-5F});
        const auto report = morok::g3::training::train(backend, dataset, config);

        morok::g3::inference::ModelArtifact artifact;
        artifact.metadata.generation_id = 3U;
        artifact.metadata.architecture_id = morok::g3::neural::kArchitectureId;
        artifact.metadata.embedding_dim = static_cast<std::uint16_t>(
            morok::g3::neural::kEmbeddingDim);
        artifact.metadata.hidden_dim = static_cast<std::uint16_t>(
            morok::g3::neural::kLayer1HiddenDim);
        artifact.metadata.hidden_dim_second = static_cast<std::uint16_t>(
            morok::g3::neural::kLayer2HiddenDim);
        artifact.metadata.recurrent_layers = 2U;
        artifact.metadata.vocabulary_size = static_cast<std::uint32_t>(
            morok::g3::neural::kVocabularySize);
        artifact.metadata.state_size_bytes = backend.descriptor().state_size_bytes;
        artifact.metadata.parameter_count = backend.parameter_count();
        artifact.metadata.precision = morok::g3::inference::PrecisionMode::fp32;
        artifact.metadata.scale_count = 0U;

        const auto& weights = backend.weights();
        artifact.payload.reserve(static_cast<std::size_t>(backend.parameter_count()) *
                                 sizeof(float));
        append_fp32_little_endian(weights.embedding, artifact.payload);
        append_fp32_little_endian(weights.layer1_input, artifact.payload);
        append_fp32_little_endian(weights.layer1_recurrent, artifact.payload);
        append_fp32_little_endian(weights.layer1_bias, artifact.payload);
        append_fp32_little_endian(weights.layer2_input, artifact.payload);
        append_fp32_little_endian(weights.layer2_recurrent, artifact.payload);
        append_fp32_little_endian(weights.layer2_bias, artifact.payload);
        append_fp32_little_endian(weights.rms_scale, artifact.payload);
        append_fp32_little_endian(weights.projection, artifact.payload);
        append_fp32_little_endian(weights.projection_bias, artifact.payload);
        append_fp32_little_endian(weights.output_bias, artifact.payload);

        const auto saved = morok::g3::inference::save_model_artifact_atomic(
            argv[3], artifact);
        if (!saved) throw std::runtime_error("failed to save final G3 .morok model");

        for (const auto& epoch : report.epochs) {
            std::cout << "{\"epoch\":" << epoch.epoch
                      << ",\"training_loss\":" << epoch.training_loss
                      << ",\"validation_loss\":" << epoch.validation_loss
                      << ",\"optimizer_steps\":" << epoch.optimizer_steps << "}\n";
        }
        std::cout << "Modelo salvo: " << argv[3] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Falha no treinamento G3: " << error.what() << '\n';
        return 1;
    }
}
