#include "morok/g2/tsukuyomi/model.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
bool parse_limit(const char* text, std::size_t& value) {
    const std::string count(text ? text : "");
    const auto parsed = std::from_chars(count.data(),
        count.data() + count.size(), value);
    return parsed.ec == std::errc{} &&
        parsed.ptr == count.data() + count.size() &&
        value > 0 && value <= 4096;
}

std::string answer_for(const morok::g2::tsukuyomi::Model& model,
                       const std::string& question,
                       std::size_t maximum) {
    const std::string prompt = "U: " + question + "\nIA:";
    const std::vector<std::uint8_t> input(prompt.begin(), prompt.end());
    const auto result = morok::g2::tsukuyomi::generate(model, input, maximum);
    std::string answer;
    answer.reserve(result.size() - input.size());
    for (std::size_t i = input.size(); i < result.size(); ++i) {
        const char byte = static_cast<char>(result[i]);
        if (byte == '\n' || byte == '\r' || byte == '\0') break;
        answer.push_back(byte);
    }
    return answer;
}
}

int main(int argc, char** argv) {
    const bool batch = argc == 4 && std::string(argv[1]) == "--batch";
    if ((!batch && argc != 4) || (batch && argc != 4)) {
        std::cerr << "Uso: morok-g2-tsukuyomi-generate <checkpoint> <prompt> <max_bytes>\n"
                  << "Ou: morok-g2-tsukuyomi-generate --batch <checkpoint> <max_bytes>\n";
        return 2;
    }

    const char* checkpoint = batch ? argv[2] : argv[1];
    const char* limit_text = batch ? argv[3] : argv[3];
    std::size_t maximum = 0;
    if (!parse_limit(limit_text, maximum)) {
        std::cerr << "Limite de geração inválido (1 a 4096 bytes).\n";
        return 2;
    }

    morok::g2::tsukuyomi::Model model;
    if (!morok::g2::tsukuyomi::load_checkpoint(model, checkpoint)) {
        std::cerr << "Checkpoint inválido ou incompatível.\n";
        return 1;
    }

    if (batch) {
        std::string question;
        while (std::getline(std::cin, question)) {
            if (question.empty()) {
                std::cout << '\n';
                continue;
            }
            std::cout << answer_for(model, question, maximum) << '\n';
            if (!std::cout.good()) return 1;
        }
        return std::cin.eof() ? 0 : 1;
    }

    const std::string prompt(argv[2]);
    const std::vector<std::uint8_t> input(prompt.begin(), prompt.end());
    const auto result = morok::g2::tsukuyomi::generate(model, input, maximum);
    for (std::size_t i = input.size(); i < result.size(); ++i) {
        const char byte = static_cast<char>(result[i]);
        if (byte == '\n' || byte == '\r' || byte == '\0') break;
        std::cout.put(byte);
    }
    std::cout.put('\n');
    return std::cout.good() ? 0 : 1;
}
