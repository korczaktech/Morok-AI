#pragma once
#include "morok/model/learn/optimizer.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace morok::model {
struct G1Checkpoint {
 G1Parameters parameters{};
 G1OptimizerState optimizer{G1AdamWState{}};
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
[[nodiscard]] std::uint32_t g1_crc32(std::span<const std::byte>) noexcept;
void save_g1_checkpoint(const std::filesystem::path&,const G1Checkpoint&);
[[nodiscard]] G1Checkpoint load_g1_checkpoint(const std::filesystem::path&);
} // namespace morok::model