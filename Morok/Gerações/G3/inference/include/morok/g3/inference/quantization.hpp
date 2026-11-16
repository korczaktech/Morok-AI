#pragma once

#include "morok/common/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace morok::g3::inference {

struct QuantizedTensor final {
    std::size_t rows{0};
    std::size_t columns{0};
    // Row-major INT8 values. One scale per output row.
    std::vector<std::int8_t> values;
    std::vector<float> scales;

    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return values.size() * sizeof(std::int8_t) +
               scales.size() * sizeof(float);
    }
};

// Symmetric per-row quantization to [-127, 127]. Zero rows use scale 1.
[[nodiscard]] common::Result<QuantizedTensor> quantize_per_row(
    std::span<const float> values, std::size_t rows, std::size_t columns);

[[nodiscard]] common::Result<std::vector<float>> dequantize_per_row(
    const QuantizedTensor& tensor);

} // namespace morok::g3::inference
