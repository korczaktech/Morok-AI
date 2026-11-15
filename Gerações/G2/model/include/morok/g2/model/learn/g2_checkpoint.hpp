#pragma once
#include "morok/g2/model/parameters/g2_parameters.hpp"
#include <filesystem>
namespace morok::g2::model {
// A G2 checkpoint is an atomic-per-expert pair: <path>.expert-a and <path>.expert-b.
void save_g2_checkpoint(const std::filesystem::path& path,const G2Parameters& parameters);
[[nodiscard]] G2Parameters load_g2_checkpoint(const std::filesystem::path& path);
}
