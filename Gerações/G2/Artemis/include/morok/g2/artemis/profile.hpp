#pragma once
#include "morok/common/utf8_text.hpp"
#include "morok/g2/model/learn/g2_generation.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace morok::g2::artemis {
inline constexpr std::size_t kAgentCount=3;
struct Policy { std::size_t max_new_bytes{192}; std::uint64_t seed{42}; };
[[nodiscard]] std::array<model::G2GenerationReport,kAgentCount> run_agents(const std::array<const model::G2Parameters*,kAgentCount>&,const common::Utf8Text&,const Policy& = {});
}
