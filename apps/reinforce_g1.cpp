#include "morok/common/utf8_text.hpp"
#include "morok/model/learn/g1_bptt.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/learn/optimizer.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error(std::string("cannot open ") + path);
    std::string value((std::istreambuf_iterator<char>(file)), {});
    if (file.bad()) throw std::runtime_error(std::string("cannot read ") + path);
    return value;
}
void usage() {
    std::cerr << "usage: morok-reinforce <checkpoint> <prompt.txt> <response.txt> <reward:-1|1> [learning-rate]\n";
}
}

int main(int argc, char** argv) {
    if (argc < 5 || argc > 6) {
        usage();
        return 2;
    }
    try {
        const std::string prompt_text = read_file(argv[2]);
        const std::string response_text = read_file(argv[3]);
        if (prompt_text.empty() || response_text.size() < 2)
            throw std::invalid_argument("prompt must be non-empty and response must contain at least two bytes");
        const std::string reward_text = argv[4];
        if (reward_text != "1" && reward_text != "-1")
            throw std::invalid_argument("reward must be exactly 1 or -1");
        const double reward = reward_text == "1" ? 1.0 : -1.0;
        const double learning_rate = argc == 6 ? std::stod(argv[5]) : 0.0001;
        if (!std::isfinite(learning_rate) || learning_rate <= 0.0 || learning_rate > 0.01)
            throw std::invalid_argument("learning-rate must be finite and in (0, 0.01]");

        auto prompt_result = morok::common::Utf8Text::from_utf8(prompt_text);
        auto response_result = morok::common::Utf8Text::from_utf8(response_text);
        if (!prompt_result || !response_result)
            throw std::invalid_argument("prompt and response must be valid UTF-8");

        auto checkpoint = morok::model::load_g1_checkpoint(argv[1]);
        const auto& prompt = prompt_result.value_if()->string();
        const auto& response = response_result.value_if()->string();

        morok::model::G1State initial_state{};
        for (std::size_t i = 0; i + 1 < prompt.size(); ++i) {
            morok::model::advance_g1_state(
                checkpoint.parameters, initial_state,
                static_cast<std::uint8_t>(static_cast<unsigned char>(prompt[i])));
        }

        std::vector<std::uint8_t> sequence;
        sequence.reserve(response.size() + 1);
        sequence.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(prompt.back())));
        for (const unsigned char byte : response) sequence.push_back(byte);

        const auto result = morok::model::compute_g1_bptt_gradients(
            checkpoint.parameters, sequence, 64, initial_state, prompt.size() - 1);
        morok::model::G1Gradients rewarded = result.gradients;
        for (double& value : rewarded.values) value *= reward;

        // A separate SGD state keeps feedback updates small and directly
        // proportional to the signed reward, independent of prior Adam moments.
        morok::model::G1OptimizerConfig config =
            morok::model::G1OptimizerConfig::sgd_momentum();
        config.learning_rate = learning_rate;
        config.momentum = 0.0;
        config.weight_decay = 0.0;
        config.max_gradient_norm = 0.25;
        morok::model::G1OptimizerState optimizer =
            morok::model::G1SGDMomentumState{};
        const auto before = checkpoint.parameters.flatten();
        const auto step = morok::model::apply_g1_optimizer_step(
            checkpoint.parameters, rewarded, config, optimizer);
        const auto after = checkpoint.parameters.flatten();
        double delta_squared = 0.0;
        for (std::size_t i = 0; i < before.size(); ++i) {
            const double delta = static_cast<double>(after[i]) - before[i];
            delta_squared += delta * delta;
        }
        checkpoint.optimizer = std::move(optimizer);
        checkpoint.has_optimizer = true;
        save_g1_checkpoint(argv[1], checkpoint);

        std::cout << "{\"reward\":" << (reward > 0.0 ? 1 : -1)
                  << ",\"target_bytes\":" << result.target_byte_count
                  << ",\"gradient_norm\":" << result.gradients.global_norm()
                  << ",\"applied_gradient_norm\":" << step.gradient_norm_after_clipping
                  << ",\"parameter_delta_l2\":" << std::sqrt(delta_squared)
                  << ",\"optimizer_step\":" << step.step
                  << ",\"trainable_parameters\":"
                  << morok::model::kG1ParameterCount << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok-reinforce: " << error.what() << '\n';
        return 1;
    }
}
