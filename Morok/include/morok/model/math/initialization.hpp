#pragma once

#include "morok/model/math/primitives.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace morok::math {

inline void initialize_xavier_uniform(Matrix& matrix, std::uint64_t seed) {
    const Scalar denominator = static_cast<Scalar>(matrix.rows() + matrix.columns());
    const Scalar limit = std::sqrt(6.0 / denominator);
    std::mt19937_64 generator(seed);
    std::uniform_real_distribution<Scalar> distribution(-limit, limit);
    for (Scalar& value : matrix.values()) {
        value = distribution(generator);
    }
}

inline void initialize_orthogonal(Matrix& matrix, std::uint64_t seed,
                                  Scalar gain = 1.0) {
    if (!std::isfinite(gain) || gain <= 0.0) {
        throw std::invalid_argument("orthogonal gain must be finite and positive");
    }

    const std::size_t rows = matrix.rows();
    const std::size_t columns = matrix.columns();
    const std::size_t vectors = rows < columns ? rows : columns;
    const std::size_t width = rows < columns ? columns : rows;

    std::mt19937_64 generator(seed);
    std::normal_distribution<Scalar> distribution(0.0, 1.0);
    std::vector<Vector> basis;
    basis.reserve(vectors);

    for (std::size_t i = 0; i < vectors; ++i) {
        Vector candidate(width);
        for (Scalar& value : candidate) {
            value = distribution(generator);
        }

        for (const Vector& previous : basis) {
            Scalar projection = 0.0;
            for (std::size_t j = 0; j < width; ++j) {
                projection += candidate[j] * previous[j];
            }
            for (std::size_t j = 0; j < width; ++j) {
                candidate[j] -= projection * previous[j];
            }
        }

        Scalar norm = global_norm(candidate);
        if (norm <= std::numeric_limits<Scalar>::epsilon()) {
            throw std::runtime_error("orthogonal initialization became degenerate");
        }
        for (Scalar& value : candidate) {
            value = (value / norm) * gain;
        }
        basis.push_back(std::move(candidate));
    }

    if (rows >= columns) {
        for (std::size_t column = 0; column < columns; ++column) {
            for (std::size_t row = 0; row < rows; ++row) {
                matrix(row, column) = basis[column][row];
            }
        }
    } else {
        for (std::size_t row = 0; row < rows; ++row) {
            for (std::size_t column = 0; column < columns; ++column) {
                matrix(row, column) = basis[row][column];
            }
        }
    }
}

} // namespace morok::math
