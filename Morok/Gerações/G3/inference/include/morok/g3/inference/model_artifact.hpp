#pragma once

#include "morok/common/result.hpp"

#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <span>
#include <vector>

namespace morok::g3::inference {

inline constexpr std::uint16_t kG3ArchitectureGruRmsNormTied = 0x0302;
inline constexpr std::uint16_t kModelFormatVersion = 1;
inline constexpr std::size_t kModelHeaderBytes = 64;
inline constexpr std::size_t kMaximumModelPayloadBytes = 512U * 1024U * 1024U;

enum class PrecisionMode : std::uint16_t {
    fp32 = 0,
    int8_symmetric_per_row = 1
};

struct ModelArtifactMetadata final {
    std::uint16_t generation_id{3};
    std::uint16_t architecture_id{kG3ArchitectureGruRmsNormTied};
    PrecisionMode precision{PrecisionMode::fp32};
    std::uint32_t vocabulary_size{261};
    std::uint16_t embedding_dim{60};
    std::uint16_t hidden_dim{97};
    std::uint16_t hidden_dim_second{68};
    std::uint16_t recurrent_layers{2};
    std::uint32_t state_size_bytes{0};
    std::uint64_t parameter_count{0};
    std::uint32_t scale_count{0};
};

struct ModelArtifact final {
    ModelArtifactMetadata metadata;
    std::vector<std::byte> payload;
};

[[nodiscard]] common::Result<std::vector<std::byte>> serialize_model_artifact(
    const ModelArtifact& artifact);

[[nodiscard]] common::Result<ModelArtifact> deserialize_model_artifact(
    std::span<const std::byte> bytes);

[[nodiscard]] common::Status save_model_artifact_atomic(
    const std::filesystem::path& destination, const ModelArtifact& artifact);

[[nodiscard]] common::Result<ModelArtifact> read_model_artifact(
    const std::filesystem::path& source);

[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> bytes) noexcept;

} // namespace morok::g3::inference
