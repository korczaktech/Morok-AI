#pragma once
#include "morok/g2/engine/parameters.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace morok::g2::model {
inline constexpr std::size_t kG2ParameterCount = 2000;
struct G2Parameters {
    engine::G2ExpertParameters expert_a{};
    engine::G2ExpertParameters expert_b{};
    [[nodiscard]] std::array<float, kG2ParameterCount> flatten() const;
    [[nodiscard]] bool assign(std::span<const float> values) noexcept;
    [[nodiscard]] static constexpr std::size_t parameter_count() noexcept { return kG2ParameterCount; }
};
static_assert(engine::G2ExpertParameters::parameter_count() * 2 == kG2ParameterCount);
void initialize_g2_parameters(G2Parameters& parameters, std::uint64_t seed);
}
