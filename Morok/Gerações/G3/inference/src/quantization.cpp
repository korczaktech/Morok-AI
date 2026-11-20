#include "morok/g3/inference/quantization.hpp"

#include "morok/common/error.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace morok::g3::inference {
namespace {

template <typename T>
common::Result<T> failure(common::ErrorCode code, const char* stage,
                           const char* message) {
    return common::Result<T>::failure(
        common::Error::make(code, message, "g3.inference", stage));
}

bool valid_shape(std::size_t rows, std::size_t columns,
                 std::size_t count) noexcept {
    return rows > 0U && columns > 0U &&
           rows <= std::numeric_limits<std::size_t>::max() / columns &&
           rows * columns == count;
}

} // namespace

common::Result<QuantizedTensor> quantize_per_row(
    std::span<const float> values, std::size_t rows, std::size_t columns) {
    if (!valid_shape(rows, columns, values.size())) {
        return failure<QuantizedTensor>(
            common::ErrorCode::invalid_argument, "quantize.shape",
            "Matrix dimensions do not match the input element count");
    }
    if (std::any_of(values.begin(), values.end(),
                    [](float value) { return !std::isfinite(value); })) {
        return failure<QuantizedTensor>(
            common::ErrorCode::invalid_argument, "quantize.non_finite",
            "Quantization input contains NaN or infinity");
    }

    QuantizedTensor result;
    result.rows = rows;
    result.columns = columns;
    result.values.resize(values.size());
    result.scales.resize(rows, 1.0F);

    for (std::size_t row = 0; row < rows; ++row) {
        const auto begin = row * columns;
        float maximum = 0.0F;
        for (std::size_t column = 0; column < columns; ++column) {
            maximum = std::max(maximum, std::abs(values[begin + column]));
        }
        const float scale = maximum == 0.0F ? 1.0F :
            std::max(maximum / 127.0F,
                     std::numeric_limits<float>::denorm_min());
        if (!std::isfinite(scale) || scale <= 0.0F) {
            return failure<QuantizedTensor>(
                common::ErrorCode::internal_error, "quantize.scale",
                "Unable to calculate a finite positive quantization scale");
        }
        result.scales[row] = scale;
        for (std::size_t column = 0; column < columns; ++column) {
            const float normalized = values[begin + column] / scale;
            const long rounded = std::lround(normalized);
            const long clamped = std::clamp(rounded, -127L, 127L);
            result.values[begin + column] =
                static_cast<std::int8_t>(clamped);
        }
    }
    return common::Result<QuantizedTensor>::success(std::move(result));
}

common::Result<std::vector<float>> dequantize_per_row(
    const QuantizedTensor& tensor) {
    if (!valid_shape(tensor.rows, tensor.columns, tensor.values.size()) ||
        tensor.scales.size() != tensor.rows ||
        std::any_of(tensor.scales.begin(), tensor.scales.end(),
            [](float scale) { return !std::isfinite(scale) || scale <= 0.0F; })) {
        return failure<std::vector<float>>(
            common::ErrorCode::invalid_argument, "dequantize.shape",
            "Quantized tensor dimensions or scales are invalid");
    }

    std::vector<float> result(tensor.values.size());
    for (std::size_t row = 0; row < tensor.rows; ++row) {
        for (std::size_t column = 0; column < tensor.columns; ++column) {
            const auto index = row * tensor.columns + column;
            const float restored = static_cast<float>(tensor.values[index]) *
                                   tensor.scales[row];
            if (!std::isfinite(restored)) {
                return failure<std::vector<float>>(
                    common::ErrorCode::invalid_argument, "dequantize.overflow",
                    "Dequantization produced a non-finite FP32 value");
            }
            result[index] = restored;
        }
    }
    return common::Result<std::vector<float>>::success(std::move(result));
}

} // namespace morok::g3::inference
