#pragma once
#include "morok/g2/engine/training.hpp"
#include "morok/g2/model/parameters/g2_parameters.hpp"
#include <filesystem>
namespace morok::g2::model {
struct G2TrainingConfig {
    engine::G2ExpertTrainingConfig base{};
    std::filesystem::path checkpoint_prefix{};
    bool resume{false};
};
struct G2TrainingReport { engine::G2ExpertTrainingReport expert_a{}, expert_b{}; };
[[nodiscard]] G2TrainingReport train_g2_corpus(const G2TrainingConfig&,G2Parameters&);
}
