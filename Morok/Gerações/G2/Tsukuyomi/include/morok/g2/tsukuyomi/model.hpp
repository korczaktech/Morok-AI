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
inline constexpr std::size_t kParameterCount = 10000;
inline constexpr std::uint32_t kModelVersion = 1;

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
    double learning_rate{0.001};
    double weight_decay{0.00001};
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
[[nodiscard]] ForwardResult forward(const Model& model,
                                    std::span<const std::uint8_t> context);
[[nodiscard]] double evaluate_loss(const Model& model,
                                   std::span<const std::uint8_t> bytes);
[[nodiscard]] TrainingReport train(Model& model,
                                    std::span<const std::uint8_t> bytes,
                                    const TrainingConfig& config, TrainingState* state = nullptr);
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
