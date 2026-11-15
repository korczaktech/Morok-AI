#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace morok::model {

inline constexpr std::size_t kG2ExpertInputDimension = 11;
inline constexpr std::size_t kG2ExpertFastStateDimension = 11;
inline constexpr std::size_t kG2ExpertSlowStateDimension = 11;
inline constexpr std::size_t kG2ExpertPredictionBits = 8;
inline constexpr std::size_t kG2ExpertCombinedStateDimension = 22;

inline constexpr std::size_t kG2ExpertFastParameterCount = 506;
inline constexpr std::size_t kG2ExpertSlowParameterCount = 253;
inline constexpr std::size_t kG2ExpertPredictionParameterCount = 212;
inline constexpr std::size_t kG2ExpertEosParameterCount = 29;
inline constexpr std::size_t kG2ExpertParameterCount =
    kG2ExpertFastParameterCount + kG2ExpertSlowParameterCount +
    kG2ExpertPredictionParameterCount + kG2ExpertEosParameterCount;

static_assert(kG2ExpertParameterCount == 1000);

struct G2ExpertParameters {
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

    // Learned end-of-sequence head: 22 recurrent-state weights and seven length-bucket biases.
    std::array<float, 22> v{};
    std::array<float, 7> theta_len{};

    [[nodiscard]] std::array<float, kG2ExpertParameterCount> flatten() const;
    [[nodiscard]] bool assign(std::span<const float> values) noexcept;
    [[nodiscard]] static constexpr std::size_t parameter_count() noexcept {
        return kG2ExpertParameterCount;
    }
};

void initialize_g2_expert_parameters(G2ExpertParameters& parameters, std::uint64_t seed);

} // namespace morok::model
