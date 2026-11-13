#include "morok/model/parameters/g1_parameters.hpp"

#include "morok/model/math/initialization.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace morok::model {
namespace {

std::uint64_t splitmix64_next(std::uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t value = state;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

template <std::size_t N>
void copy_matrix_to_parameters(const math::Matrix& matrix,
                               std::array<float, N>& destination) {
    const auto source = matrix.values();
    for (std::size_t i = 0; i < N; ++i) {
        destination[i] = static_cast<float>(source[i]);
    }
}

template <std::size_t N>
void initialize_xavier(std::array<float, N>& destination,
                       std::size_t rows, std::size_t columns,
                       std::uint64_t seed) {
    math::Matrix matrix(rows, columns);
    math::initialize_xavier_uniform(matrix, seed);
    copy_matrix_to_parameters(matrix, destination);
}

template <std::size_t N>
void initialize_orthogonal(std::array<float, N>& destination,
                           std::size_t rows, std::size_t columns,
                           std::uint64_t seed) {
    math::Matrix matrix(rows, columns);
    math::initialize_orthogonal(matrix, seed);
    copy_matrix_to_parameters(matrix, destination);
}

template <typename... Arrays>
void append_array(std::array<float, kG1ParameterCount>& destination,
                  std::size_t& offset, const Arrays&... arrays) {
    const auto append_one = [&](const auto& values) {
        std::copy(values.begin(), values.end(), destination.begin() +
                  static_cast<std::ptrdiff_t>(offset));
        offset += values.size();
    };
    (append_one(arrays), ...);
}

template <typename... Arrays>
void read_arrays(std::span<const float> source, std::size_t& offset,
                 Arrays&... arrays) noexcept {
    const auto read_one = [&](auto& values) {
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(offset),
                    values.size(), values.begin());
        offset += values.size();
    };
    (read_one(arrays), ...);
}

} // namespace

std::array<float, kG1ParameterCount> G1Parameters::flatten() const {
    std::array<float, kG1ParameterCount> result{};
    std::size_t offset = 0;
    append_array(result, offset,
                 W_ax, W_ah, W_cx, W_ch, b_a, b_c,
                 W_gh, W_gs, b_g,
                 O, c, A,
                 v, theta_len);
    return result;
}

bool G1Parameters::assign(std::span<const float> values) noexcept {
    if (values.size() != kG1ParameterCount) {
        return false;
    }
    for (const float value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }

    std::size_t offset = 0;
    read_arrays(values, offset,
                W_ax, W_ah, W_cx, W_ch, b_a, b_c,
                W_gh, W_gs, b_g,
                O, c, A,
                v, theta_len);
    return offset == kG1ParameterCount;
}

void initialize_g1_parameters(G1Parameters& parameters, std::uint64_t seed) {
    parameters = {};
    std::uint64_t seed_state = seed;

    initialize_xavier(parameters.W_ax, 11, 11, splitmix64_next(seed_state));
    initialize_orthogonal(parameters.W_ah, 11, 11, splitmix64_next(seed_state));
    initialize_xavier(parameters.W_cx, 11, 11, splitmix64_next(seed_state));
    initialize_orthogonal(parameters.W_ch, 11, 11, splitmix64_next(seed_state));

    initialize_orthogonal(parameters.W_gh, 11, 11, splitmix64_next(seed_state));
    initialize_orthogonal(parameters.W_gs, 11, 11, splitmix64_next(seed_state));

    initialize_xavier(parameters.O, 8, 22, splitmix64_next(seed_state));

    // Gate biases: fast gate starts neutral; slow gate initially favors retention.
    parameters.b_a.fill(0.0F);
    parameters.b_c.fill(0.0F);
    parameters.b_g.fill(1.0F);

    // Prediction and segmenter biases/weights start at zero.
    parameters.c.fill(0.0F);
    parameters.A.fill(0.0F);
    parameters.v.fill(0.0F);
    parameters.theta_len.fill(0.0F);
}

} // namespace morok::model
