#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace morok::g2::tsukuyomi {

inline constexpr std::size_t kVocabularySize = 256;
inline constexpr std::size_t kHiddenSize = 32;
inline constexpr std::size_t kContextSize = 13;
inline constexpr std::size_t kBpttTruncation = 64;
inline constexpr std::size_t kParameterCount = 10000;
// Formato interno recorrente; a versão pública do modelo permanece 0.1.
inline constexpr std::uint32_t kCheckpointFormatVersion = 8;

struct Model {
    std::array<float, kParameterCount> parameters{};
};

struct ForwardResult {
    std::array<float, kVocabularySize> logits{};
    std::array<float, kHiddenSize> hidden{};
};

struct TrainingConfig {
    std::size_t epochs{1};
    std::size_t batch_size{16};
    std::size_t bptt_steps{kBpttTruncation};
    double learning_rate{0.001};
    double weight_decay{0.01};
    double gradient_clip{1.0};
    std::uint64_t seed{42};
};

struct TrainingState {
    std::array<double, kParameterCount> first_moment{};
    std::array<double, kParameterCount> second_moment{};
    std::uint64_t step{0};
};

struct TrainingReport {
    std::size_t epochs_completed{0};
    std::uint64_t target_bytes{0};
    double initial_loss{0.0};
    double final_loss{0.0};
    double initial_bpb{0.0};
    double final_bpb{0.0};
};

void initialize(Model& model, std::uint64_t seed);
// Gradiente analítico da perda de um único byte-alvo, usado em testes numéricos.
[[nodiscard]] bool compute_target_gradient(
    const Model& model, std::span<const std::uint8_t> bytes,
    std::size_t target_index,
    std::array<float, kParameterCount>& gradient);
[[nodiscard]] ForwardResult forward(const Model& model,
                                    std::span<const std::uint8_t> context);
[[nodiscard]] double evaluate_loss(const Model& model,
                                   std::span<const std::uint8_t> bytes,
                                   std::span<const std::uint8_t> target_mask = {});
[[nodiscard]] TrainingReport train(Model& model,
                                    std::span<const std::uint8_t> bytes,
                                    const TrainingConfig& config, TrainingState* state = nullptr,
                                    std::span<const std::uint8_t> target_mask = {});
[[nodiscard]] std::vector<std::uint8_t> generate(
    const Model& model, std::span<const std::uint8_t> prompt,
    std::size_t max_new_bytes);
[[nodiscard]] bool save_checkpoint(const Model& model,
                                   const std::filesystem::path& path);
[[nodiscard]] bool save_training_state(const TrainingState& state,
                                       const std::filesystem::path& path);
[[nodiscard]] bool load_training_state(TrainingState& state,
                                       const std::filesystem::path& path);
[[nodiscard]] bool load_checkpoint(Model& model,
                                   const std::filesystem::path& path);

} // namespace morok::g2::tsukuyomi
