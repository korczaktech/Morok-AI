#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace morok::math {

using Scalar = double;
using Vector = std::vector<Scalar>;

class Matrix {
public:
    Matrix(std::size_t rows, std::size_t columns, Scalar value = 0.0)
        : rows_(rows), columns_(columns), values_(checked_size(rows, columns), value) {}

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
    [[nodiscard]] std::span<Scalar> values() noexcept { return values_; }
    [[nodiscard]] std::span<const Scalar> values() const noexcept { return values_; }

    Scalar& operator()(std::size_t row, std::size_t column) {
        return values_.at(index(row, column));
    }
    const Scalar& operator()(std::size_t row, std::size_t column) const {
        return values_.at(index(row, column));
    }

private:
    [[nodiscard]] static std::size_t checked_size(std::size_t rows,
                                                  std::size_t columns) {
        if (rows == 0 || columns == 0) {
            throw std::invalid_argument("matrix dimensions must be positive");
        }
        if (rows > std::numeric_limits<std::size_t>::max() / columns) {
            throw std::length_error("matrix dimensions overflow addressable storage");
        }
        return rows * columns;
    }

    [[nodiscard]] std::size_t index(std::size_t row, std::size_t column) const {
        if (row >= rows_ || column >= columns_) {
            throw std::out_of_range("matrix index out of range");
        }
        return row * columns_ + column;
    }

    std::size_t rows_;
    std::size_t columns_;
    Vector values_;
};

inline Scalar sigmoid(Scalar x) noexcept {
    if (x >= 0.0) {
        const Scalar z = std::exp(-x);
        return 1.0 / (1.0 + z);
    }
    const Scalar z = std::exp(x);
    return z / (1.0 + z);
}

inline Scalar tanh_activation(Scalar x) noexcept { return std::tanh(x); }

inline Scalar softplus(Scalar x) noexcept {
    return std::max(x, 0.0) + std::log1p(std::exp(-std::abs(x)));
}

inline Scalar binary_cross_entropy_with_logits(Scalar logit, Scalar target) {
    if (!std::isfinite(logit)) {
        throw std::invalid_argument("BCE logit must be finite");
    }
    if (!std::isfinite(target) || target < 0.0 || target > 1.0) {
        throw std::invalid_argument("BCE target must be finite and in [0, 1]");
    }
    return std::max(logit, 0.0) - logit * target + std::log1p(std::exp(-std::abs(logit)));
}

inline Scalar log_sum_exp(std::span<const Scalar> values) {
    if (values.empty()) {
        throw std::invalid_argument("log_sum_exp requires at least one value");
    }
    Scalar maximum = -std::numeric_limits<Scalar>::infinity();
    for (const Scalar value : values) {
        if (std::isnan(value)) {
            throw std::invalid_argument("log_sum_exp does not accept NaN");
        }
        maximum = std::max(maximum, value);
    }
    if (maximum == std::numeric_limits<Scalar>::infinity()) {
        return maximum;
    }
    if (maximum == -std::numeric_limits<Scalar>::infinity()) {
        return maximum;
    }
    Scalar sum = 0.0;
    for (const Scalar value : values) {
        sum += std::exp(value - maximum);
    }
    return maximum + std::log(sum);
}

inline Vector softmax(std::span<const Scalar> logits) {
    if (logits.empty()) {
        throw std::invalid_argument("softmax requires at least one logit");
    }
    for (const Scalar value : logits) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("softmax logits must be finite");
        }
    }
    const Scalar normalizer = log_sum_exp(logits);
    Vector probabilities;
    probabilities.reserve(logits.size());
    for (const Scalar value : logits) {
        probabilities.push_back(std::exp(value - normalizer));
    }
    return probabilities;
}

inline Scalar global_norm(std::span<const Scalar> values) {
    Scalar norm = 0.0;
    for (const Scalar value : values) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("gradient contains a non-finite value");
        }
        norm = std::hypot(norm, value);
    }
    return norm;
}

inline Scalar clip_global_norm(std::span<Scalar> gradients, Scalar max_norm) {
    if (!std::isfinite(max_norm) || max_norm <= 0.0) {
        throw std::invalid_argument("max_norm must be finite and positive");
    }
    const Scalar norm = global_norm(gradients);
    if (norm > max_norm) {
        const Scalar scale = max_norm / norm;
        for (Scalar& gradient : gradients) {
            gradient *= scale;
        }
    }
    return norm;
}

inline Vector matvec(const Matrix& matrix, std::span<const Scalar> vector) {
    if (matrix.columns() != vector.size()) {
        throw std::invalid_argument("matvec dimension mismatch");
    }
    Vector result(matrix.rows(), 0.0);
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
        Scalar sum = 0.0;
        for (std::size_t column = 0; column < matrix.columns(); ++column) {
            sum += matrix(row, column) * vector[column];
        }
        result[row] = sum;
    }
    return result;
}

} // namespace morok::math
