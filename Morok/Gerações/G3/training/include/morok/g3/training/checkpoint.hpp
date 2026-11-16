#pragma once

#include "morok/common/result.hpp"
#include "morok/g3/neural/gru_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace morok::g3::training {

inline constexpr std::uint16_t kTrainingCheckpointFormatVersion = 1U;
inline constexpr std::size_t kTrainingCheckpointHeaderBytes = 64U;
inline constexpr std::size_t kTrainingCheckpointMaximumBytes = 512U * 1024U * 1024U;
inline constexpr std::size_t kG3CheckpointParameterCount = 100000U;
inline constexpr std::uint16_t kG3CheckpointArchitectureId = 0x0302U;

struct CheckpointCompatibility final {
    std::uint16_t generation_id{3U};
    std::uint16_t architecture_id{kG3CheckpointArchitectureId};
    std::string code_version{"g3-checkpoint-v1"};
    std::string dataset_fingerprint;
    std::string optimizer_id{"AdamW"};
    std::string configuration_fingerprint;
};

struct G3TrainingCheckpoint final {
    CheckpointCompatibility compatibility;
    std::string run_id;
    std::string configuration_snapshot;
    std::vector<float> model_weights;
    std::vector<float> optimizer_first_moment;
    std::vector<float> optimizer_second_moment;
    std::uint64_t optimizer_step{0U};
    std::uint64_t scheduler_step{0U};
    double scheduler_learning_rate{1.0e-3};
    std::uint64_t completed_epochs{0U};
    std::uint64_t next_batch{0U};
    std::uint64_t sampler_position{0U};
    std::vector<std::uint64_t> sampler_order;
    std::vector<std::uint64_t> train_split;
    std::vector<std::uint64_t> validation_split;
    std::vector<std::uint64_t> test_split;
    std::string cpp_rng_state;
    std::string python_rng_state;
    std::string numpy_rng_state;
    std::string backend_rng_state;
    std::map<std::string, double> rl_category_baselines;
    std::string rl_rng_state;
    std::vector<float> rl_optimizer_first_moment;
    std::vector<float> rl_optimizer_second_moment;
    std::uint64_t rl_optimizer_step{0U};
    double kl_beta{0.02};
    std::vector<std::byte> frozen_sft_reference;
    std::vector<std::byte> replay_buffer_state;
    std::vector<std::byte> training_history;
};

[[nodiscard]] common::Status validate_training_checkpoint(
    const G3TrainingCheckpoint& checkpoint,
    const CheckpointCompatibility* expected = nullptr);

[[nodiscard]] common::Result<std::vector<std::byte>> serialize_training_checkpoint(
    const G3TrainingCheckpoint& checkpoint);

[[nodiscard]] common::Result<G3TrainingCheckpoint> deserialize_training_checkpoint(
    std::span<const std::byte> bytes,
    const CheckpointCompatibility* expected = nullptr);

[[nodiscard]] common::Status save_training_checkpoint_atomic(
    const std::filesystem::path& destination,
    const G3TrainingCheckpoint& checkpoint);

[[nodiscard]] common::Result<G3TrainingCheckpoint> read_training_checkpoint(
    const std::filesystem::path& source,
    const CheckpointCompatibility* expected = nullptr);

[[nodiscard]] std::uint32_t training_checkpoint_crc32(
    std::span<const std::byte> bytes) noexcept;

[[nodiscard]] std::vector<float> capture_g3_model_weights(
    const neural::G3Backend& backend);

[[nodiscard]] common::Status restore_g3_model_weights(
    std::span<const float> weights, neural::G3Backend& backend);

} // namespace morok::g3::training
