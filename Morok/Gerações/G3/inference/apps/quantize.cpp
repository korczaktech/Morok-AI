#include "morok/g3/inference/int8_backend.hpp"
#include "morok/g3/inference/model_artifact.hpp"
#include "morok/g3/neural/gru_backend.hpp"
#include "morok/g3/training/checkpoint.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0U;
    for (unsigned i = 0; i < 4U; ++i)
        value |= static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(bytes[offset + i])) << (i * 8U);
    return value;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Uso: morok-g3-quantize <modelo-fp32.morok> <modelo-int8.morok>\n";
        return 2;
    }
    try {
        const auto source = morok::g3::inference::read_model_artifact(argv[1]);
        if (!source) throw std::runtime_error("Unable to load or validate source model");
        if (source.value_if()->metadata.precision !=
            morok::g3::inference::PrecisionMode::fp32)
            throw std::invalid_argument("Source model must use FP32 precision");

        if (source.value_if()->payload.size() !=
            morok::g3::neural::kTargetParameterCount * sizeof(float))
            throw std::invalid_argument("Source FP32 payload has an incompatible size");

        std::vector<float> weights;
        weights.reserve(morok::g3::neural::kTargetParameterCount);
        const auto payload = std::span<const std::byte>(source.value_if()->payload);
        for (std::size_t offset = 0; offset < payload.size(); offset += sizeof(float)) {
            const float value = std::bit_cast<float>(read_u32(payload, offset));
            if (!std::isfinite(value))
                throw std::invalid_argument("Source model contains non-finite weights");
            weights.push_back(value);
        }

        morok::g3::neural::G3Backend backend;
        const auto restored = morok::g3::training::restore_g3_model_weights(
            weights, backend);
        if (!restored) throw std::runtime_error("Source weights do not match G3 architecture");

        const auto quantized = morok::g3::inference::export_g3_int8_artifact(backend);
        if (!quantized) throw std::runtime_error("G3 INT8 quantization failed");
        const auto saved = morok::g3::inference::save_model_artifact_atomic(
            argv[2], *quantized.value_if());
        if (!saved) throw std::runtime_error("Unable to save INT8 model artifact");

        std::cout << "{\"precision\":\"INT8\",\"source_payload_bytes\":"
                  << source.value_if()->payload.size()
                  << ",\"int8_payload_bytes\":"
                  << quantized.value_if()->payload.size()
                  << ",\"scale_count\":"
                  << quantized.value_if()->metadata.scale_count
                  << ",\"output\":\"" << argv[2] << "\"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Falha na quantização G3: " << error.what() << '\n';
        return 1;
    }
}
