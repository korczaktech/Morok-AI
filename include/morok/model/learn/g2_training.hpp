#pragma once
#include "morok/model/learn/g1_training.hpp"
#include "morok/model/parameters/g2_parameters.hpp"
#include <filesystem>
namespace morok::model {
struct G2TrainingConfig {
    G1TrainingConfig base{};
    std::filesystem::path checkpoint_prefix{};
};
struct G2TrainingReport { G1TrainingReport expert_a{}, expert_b{}; };
[[nodiscard]] G2TrainingReport train_g2_corpus(const G2TrainingConfig&,G2Parameters&);
}
