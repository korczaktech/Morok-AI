#include "morok/common/utf8_text.hpp"
#include "morok/model/learn/g1_bptt.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/learn/optimizer.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
std::string read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error(std::string("cannot open ") + path);
    std::string value((std::istreambuf_iterator<char>(file)), {});
    if (file.bad()) throw std::runtime_error(std::string("cannot read ") + path);
    return value;
}
std::size_t parse_steps(const char* value) {
    const std::string text(value);
    std::size_t consumed = 0;
    const auto parsed = std::stoull(text, &consumed);
    if (consumed != text.size() || parsed < 1 || parsed > 1000)
        throw std::invalid_argument("steps must be between 1 and 1000");
    return static_cast<std::size_t>(parsed);
}
void usage() {
    std::cerr << "usage: morok-reinforce <checkpoint> <prompt.txt> <response.txt> <reward:-1|1> [learning-rate] [steps]\n";
}
}

int main(int argc, char** argv) {
    if (argc < 5 || argc > 7) {
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
        const double learning_rate = argc >= 6 ? std::stod(argv[5]) : 0.0001;
        const std::size_t update_steps = argc >= 7 ? parse_steps(argv[6]) : 1;
        if (!std::isfinite(learning_rate) || learning_rate <= 0.0 || learning_rate > 0.01)
            throw std::invalid_argument("learning-rate must be finite and in (0, 0.01]");

        auto prompt_result = morok::common::Utf8Text::from_utf8(prompt_text);
        auto response_result = morok::common::Utf8Text::from_utf8(response_text);
        if (!prompt_result || !response_result)
            throw std::invalid_argument("prompt and response must be valid UTF-8");

        auto checkpoint = morok::model::load_g1_checkpoint(argv[1]);
        const auto& prompt = prompt_result.value_if()->string();
        const auto& response = response_result.value_if()->string();

        std::vector<std::uint8_t> sequence;
        sequence.reserve(response.size() + 1);
        sequence.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(prompt.back())));
        for (const unsigned char byte : response) sequence.push_back(byte);

        // Positive reward minimizes the sampled answer's byte-level NLL;
        // negative reward reverses that gradient to reduce its likelihood.
        // Recompute BPTT after each small step rather than reusing stale gradients.
        morok::model::G1OptimizerConfig config =
            morok::model::G1OptimizerConfig::sgd_momentum();
        config.learning_rate = learning_rate;
        config.momentum = 0.0;
        config.weight_decay = 0.0;
        config.max_gradient_norm = 0.25;
        morok::model::G1OptimizerState optimizer =
            morok::model::G1SGDMomentumState{};

        const auto before = checkpoint.parameters.flatten();
        double first_gradient_norm = 0.0;
        double last_gradient_norm = 0.0;
        morok::model::G1OptimizerStepResult last_step{};
        for (std::size_t iteration = 0; iteration < update_steps; ++iteration) {
            morok::model::G1State initial_state{};
            for (std::size_t i = 0; i + 1 < prompt.size(); ++i) {
                morok::model::advance_g1_state(
                    checkpoint.parameters, initial_state,
                    static_cast<std::uint8_t>(static_cast<unsigned char>(prompt[i])));
            }
            const auto result = morok::model::compute_g1_bptt_gradients(
                checkpoint.parameters, sequence, 64, initial_state, prompt.size() - 1);
            last_gradient_norm = result.gradients.global_norm();
            if (iteration == 0) first_gradient_norm = last_gradient_norm;
            morok::model::G1Gradients rewarded = result.gradients;
            for (double& value : rewarded.values) value *= reward;
            last_step = morok::model::apply_g1_optimizer_step(
                checkpoint.parameters, rewarded, config, optimizer);
        }

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
                  << ",\"target_bytes\":" << response.size()
                  << ",\"gradient_norm_first\":" << first_gradient_norm
                  << ",\"gradient_norm_last\":" << last_gradient_norm
                  << ",\"parameter_delta_l2\":" << std::sqrt(delta_squared)
                  << ",\"optimizer_steps\":" << update_steps
                  << ",\"optimizer_step\":" << last_step.step
                  << ",\"trainable_parameters\":"
                  << morok::model::kG1ParameterCount << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok-reinforce: " << error.what() << '\n';
        return 1;
    }
}
