#include "morok/g3/inference/model_artifact.hpp"

#include "morok/common/error.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace morok::g3::inference {
namespace {

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'M'}, std::byte{'R'}, std::byte{'K'}, std::byte{'3'}};

template <typename T>
common::Result<T> failure(common::ErrorCode code, const char* stage,
                           const char* message) {
    return common::Result<T>::failure(
        common::Error::make(code, message, "g3.artifact", stage));
}

void put_u16(std::span<std::byte> out, std::size_t offset,
             std::uint16_t value) {
    out[offset] = static_cast<std::byte>(value & 0xffU);
    out[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void put_u32(std::span<std::byte> out, std::size_t offset,
             std::uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        out[offset + shift / 8U] =
            static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void put_u64(std::span<std::byte> out, std::size_t offset,
             std::uint64_t value) {
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
        out[offset + shift / 8U] =
            static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

std::uint16_t get_u16(std::span<const std::byte> in, std::size_t offset) {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(in[offset]) |
        (static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(in[offset + 1U])) << 8U));
}

std::uint32_t get_u32(std::span<const std::byte> in, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        value |= static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(in[offset + shift / 8U])) << shift;
    }
    return value;
}

std::uint64_t get_u64(std::span<const std::byte> in, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
        value |= static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(in[offset + shift / 8U])) << shift;
    }
    return value;
}

std::uint32_t artifact_crc32(std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const std::uint8_t value =
            index >= 52U && index < 56U ? 0U :
            std::to_integer<std::uint8_t>(bytes[index]);
        crc ^= value;
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

bool valid_metadata(const ModelArtifactMetadata& metadata,
                    std::size_t payload_size) noexcept {
    if (metadata.generation_id != 3U ||
        metadata.architecture_id != kG3ArchitectureGruRmsNormTied ||
        metadata.vocabulary_size != 261U ||
        metadata.embedding_dim != 64U || metadata.hidden_dim != 128U ||
        metadata.recurrent_layers != 2U ||
        metadata.state_size_bytes != 1024U ||
        metadata.parameter_count == 0U ||
        metadata.parameter_count > 100'000'000ULL ||
        payload_size > kMaximumModelPayloadBytes) {
        return false;
    }

    if (metadata.precision == PrecisionMode::fp32) {
        return metadata.scale_count == 0U &&
            metadata.parameter_count <=
                std::numeric_limits<std::uint64_t>::max() / sizeof(float) &&
            payload_size == metadata.parameter_count * sizeof(float);
    }
    if (metadata.precision == PrecisionMode::int8_symmetric_per_row) {
        if (metadata.scale_count == 0U ||
            metadata.scale_count > metadata.parameter_count ||
            metadata.parameter_count >
                std::numeric_limits<std::size_t>::max() -
                    static_cast<std::size_t>(metadata.scale_count) * sizeof(float)) {
            return false;
        }
        const auto expected = static_cast<std::size_t>(metadata.parameter_count) +
            static_cast<std::size_t>(metadata.scale_count) * sizeof(float);
        return payload_size == expected;
    }
    return false;
}

} // namespace

std::uint32_t crc32(std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

common::Result<std::vector<std::byte>> serialize_model_artifact(
    const ModelArtifact& artifact) {
    if (!valid_metadata(artifact.metadata, artifact.payload.size())) {
        return failure<std::vector<std::byte>>(
            common::ErrorCode::invalid_argument, "serialize.metadata",
            "Model metadata or payload size is invalid for the G3 format");
    }
    if (artifact.payload.size() >
        std::numeric_limits<std::size_t>::max() - kModelHeaderBytes) {
        return failure<std::vector<std::byte>>(
            common::ErrorCode::resource_limit, "serialize.size",
            "Model artifact size overflows the host address space");
    }

    std::vector<std::byte> output(kModelHeaderBytes + artifact.payload.size(),
                                  std::byte{0});
    std::copy(kMagic.begin(), kMagic.end(), output.begin());
    auto header = std::span<std::byte>(output).first(kModelHeaderBytes);
    put_u16(header, 4U, kModelFormatVersion);
    put_u16(header, 6U, static_cast<std::uint16_t>(kModelHeaderBytes));
    put_u16(header, 8U, artifact.metadata.generation_id);
    put_u16(header, 10U, artifact.metadata.architecture_id);
    put_u16(header, 12U, static_cast<std::uint16_t>(artifact.metadata.precision));
    put_u32(header, 16U, artifact.metadata.vocabulary_size);
    put_u16(header, 20U, artifact.metadata.embedding_dim);
    put_u16(header, 22U, artifact.metadata.hidden_dim);
    put_u16(header, 24U, artifact.metadata.recurrent_layers);
    put_u32(header, 28U, artifact.metadata.state_size_bytes);
    put_u64(header, 32U, artifact.metadata.parameter_count);
    put_u64(header, 40U, static_cast<std::uint64_t>(artifact.payload.size()));
    put_u32(header, 48U, artifact.metadata.scale_count);
    put_u32(header, 52U, 0U);
    std::copy(artifact.payload.begin(), artifact.payload.end(),
              output.begin() + static_cast<std::ptrdiff_t>(kModelHeaderBytes));
    put_u32(header, 52U, artifact_crc32(output));
    return common::Result<std::vector<std::byte>>::success(std::move(output));
}

common::Result<ModelArtifact> deserialize_model_artifact(
    std::span<const std::byte> bytes) {
    if (bytes.size() < kModelHeaderBytes ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        return failure<ModelArtifact>(
            common::ErrorCode::checkpoint_corrupt, "load.magic",
            "Model file is truncated or has an invalid G3 magic number");
    }
    const auto header = bytes.first(kModelHeaderBytes);
    if (get_u16(header, 4U) != kModelFormatVersion ||
        get_u16(header, 6U) != kModelHeaderBytes) {
        return failure<ModelArtifact>(
            common::ErrorCode::model_version_mismatch, "load.version",
            "Model format version or header size is unsupported");
    }
    for (std::size_t index : {14U, 15U, 26U, 27U, 56U, 57U, 58U, 59U,
                              60U, 61U, 62U, 63U}) {
        if (header[index] != std::byte{0}) {
            return failure<ModelArtifact>(
                common::ErrorCode::checkpoint_corrupt, "load.reserved",
                "Reserved model header bytes must be zero");
        }
    }

    ModelArtifact artifact;
    artifact.metadata.generation_id = get_u16(header, 8U);
    artifact.metadata.architecture_id = get_u16(header, 10U);
    artifact.metadata.precision =
        static_cast<PrecisionMode>(get_u16(header, 12U));
    artifact.metadata.vocabulary_size = get_u32(header, 16U);
    artifact.metadata.embedding_dim = get_u16(header, 20U);
    artifact.metadata.hidden_dim = get_u16(header, 22U);
    artifact.metadata.recurrent_layers = get_u16(header, 24U);
    artifact.metadata.state_size_bytes = get_u32(header, 28U);
    artifact.metadata.parameter_count = get_u64(header, 32U);
    const auto payload_size = get_u64(header, 40U);
    artifact.metadata.scale_count = get_u32(header, 48U);
    const auto expected_crc = get_u32(header, 52U);

    if (payload_size > kMaximumModelPayloadBytes ||
        payload_size > std::numeric_limits<std::size_t>::max() ||
        bytes.size() != kModelHeaderBytes + static_cast<std::size_t>(payload_size)) {
        return failure<ModelArtifact>(
            common::ErrorCode::resource_limit, "load.payload_size",
            "Model payload length is invalid or exceeds the configured limit");
    }
    const auto payload = bytes.subspan(kModelHeaderBytes);
    if (!valid_metadata(artifact.metadata, payload.size())) {
        return failure<ModelArtifact>(
            common::ErrorCode::model_version_mismatch, "load.compatibility",
            "Model architecture, dimensions, precision or payload layout is incompatible");
    }
    if (artifact_crc32(bytes) != expected_crc) {
        return failure<ModelArtifact>(
            common::ErrorCode::checkpoint_corrupt, "load.checksum",
            "Model payload checksum validation failed");
    }
    artifact.payload.assign(payload.begin(), payload.end());
    return common::Result<ModelArtifact>::success(std::move(artifact));
}

} // namespace morok::g3::inference
