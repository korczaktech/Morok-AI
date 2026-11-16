#include "morok/g3/training/trainer.hpp"
#include "morok/g3/inference/model_artifact.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
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
    if (argc < 4 || argc > 9) {
        std::cerr << "Uso: morok-g3-train <dataset.jsonl> <metrics.jsonl> "
                     "<modelo.morok> [epocas] [seed] [--checkpoint=arquivo] "
                     "[--resume=arquivo] [--max-epochs-this-run=N]\n";
        return 2;
    }
    try {
        auto dataset = morok::g3::training::load_jsonl(argv[1]);
        morok::g3::training::TrainingConfig config;
        config.metrics_jsonl = argv[2];
        std::size_t positional = 0U;
        for (int i = 4; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument.starts_with("--checkpoint=")) {
                config.checkpoint_path = std::string(argument.substr(13U));
            } else if (argument.starts_with("--resume=")) {
                config.resume_from_checkpoint = std::string(argument.substr(9U));
            } else if (argument.starts_with("--max-epochs-this-run=")) {
                config.max_epochs_this_run = static_cast<std::size_t>(
                    std::stoull(std::string(argument.substr(22U))));
            } else if (positional == 0U) {
                config.epochs = static_cast<std::size_t>(std::stoull(std::string(argument)));
                ++positional;
            } else if (positional == 1U) {
                config.seed = std::stoull(std::string(argument));
                ++positional;
            } else {
                throw std::invalid_argument("unexpected training argument");
            }
        }
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
        std::cout << "{\"completed_epochs\":" << report.completed_epochs
                  << ",\"resumed\":" << (report.resumed ? "true" : "false")
                  << ",\"optimizer_steps\":" << report.optimizer_steps << "}\n";
        std::cout << "Modelo salvo: " << argv[3] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Falha no treinamento G3: " << error.what() << '\n';
        return 1;
    }
}
