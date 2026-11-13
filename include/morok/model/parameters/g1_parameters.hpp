#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace morok::model {

inline constexpr std::size_t kG1InputDimension = 11;
inline constexpr std::size_t kG1FastStateDimension = 11;
inline constexpr std::size_t kG1SlowStateDimension = 11;
inline constexpr std::size_t kG1PredictionBits = 8;
inline constexpr std::size_t kG1CombinedStateDimension = 22;

inline constexpr std::size_t kG1FastParameterCount = 506;
inline constexpr std::size_t kG1SlowParameterCount = 253;
inline constexpr std::size_t kG1PredictionParameterCount = 212;
inline constexpr std::size_t kG1SegmenterParameterCount = 29;
inline constexpr std::size_t kG1ParameterCount =
    kG1FastParameterCount + kG1SlowParameterCount +
    kG1PredictionParameterCount + kG1SegmenterParameterCount;

static_assert(kG1ParameterCount == 1000);

struct G1Parameters {
    // Fast state: four 11x11 matrices and two 11-element biases.
    std::array<float, 121> W_ax{};
    std::array<float, 121> W_ah{};
    std::array<float, 121> W_cx{};
    std::array<float, 121> W_ch{};
    std::array<float, 11> b_a{};
    std::array<float, 11> b_c{};

    // Slow state: two 11x11 matrices and one 11-element bias.
    std::array<float, 121> W_gh{};
    std::array<float, 121> W_gs{};
    std::array<float, 11> b_g{};

    // Binary prediction head: 8x22 matrix, 8 biases, 28 strict-triangular terms.
    std::array<float, 176> O{};
    std::array<float, 8> c{};
    std::array<float, 28> A{};

    // Adaptive segmenter scorer: 22 features and seven length biases.
    std::array<float, 22> v{};
    std::array<float, 7> theta_len{};

    [[nodiscard]] std::array<float, kG1ParameterCount> flatten() const;
    [[nodiscard]] bool assign(std::span<const float> values) noexcept;
    [[nodiscard]] static constexpr std::size_t parameter_count() noexcept {
        return kG1ParameterCount;
    }
};

void initialize_g1_parameters(G1Parameters& parameters, std::uint64_t seed);

} // namespace morok::model
