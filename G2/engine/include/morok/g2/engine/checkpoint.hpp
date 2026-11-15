#pragma once
#include "morok/g2/engine/optimizer.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace morok::g2::engine {
struct G2ExpertCheckpoint {
 G2ExpertParameters parameters{};
 G2ExpertOptimizerState optimizer{G2ExpertAdamWState{}};
 bool has_optimizer{false};
 std::array<std::uint64_t,4> rng_state{};
 bool has_rng{false};
 std::uint32_t epoch{0};
 std::uint64_t block_index{0};
 std::vector<std::uint32_t> document_order{};
 std::uint32_t document_position{0};
 std::uint64_t byte_offset{0};
 std::string metadata{};
};
[[nodiscard]] std::uint32_t g2_expert_crc32(std::span<const std::byte>) noexcept;
void save_g2_expert_checkpoint(const std::filesystem::path&,const G2ExpertCheckpoint&);
[[nodiscard]] G2ExpertCheckpoint load_g2_expert_checkpoint(const std::filesystem::path&);
} // namespace morok::g2::engine