#include "morok/g2/tsukuyomi/model.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <sstream>
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

bool parse_size_value(const char* text, std::size_t& value) {
    const std::string input(text ? text : "");
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == input.data() + input.size();
}

bool parse_double_value(const char* text, double& value) {
    if (!text) return false;
    char* end = nullptr;
    value = std::strtod(text, &end);
    return end == text + std::char_traits<char>::length(text) && std::isfinite(value);
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
    const bool batch = argc >= 2 && std::string(argv[1]) == "--batch";
    const bool batch_individual = argc >= 2 && std::string(argv[1]) == "--batch-question-checkpoints";
    const bool individual = argc >= 2 && std::string(argv[1]) == "--question-checkpoints";
    if ((batch && argc != 4) || (batch_individual && argc != 4) ||
        (individual && (argc < 5 || argc > 19)) ||
        (!batch && !batch_individual && !individual && (argc < 4 || argc > 18))) {
        std::cerr << "Uso: morok-g2-tsukuyomi-generate <checkpoint> <prompt> <max_bytes> [opções]\n"
                  << "Ou: --batch <checkpoint> <max_bytes>\n"
                  << "Ou: --question-checkpoints <diretório> <pergunta> <max_bytes> [opções]\n"
                  << "Ou: --batch-question-checkpoints <diretório> <max_bytes>\n";
        return 2;
    }

    const std::filesystem::path checkpoint_directory =
        (individual || batch_individual) ? argv[2] : std::filesystem::path{};
    const std::string prompt = individual ? argv[3] : "";
    const char* checkpoint = batch ? argv[2] : (individual ? nullptr : argv[1]);
    const char* limit_text = (batch || batch_individual) ? argv[3] :
                             (individual ? argv[4] : argv[3]);
    std::size_t maximum = 0;
    if (!parse_limit(limit_text, maximum)) {
        std::cerr << "Limite de geração inválido (1 a 4096 bytes).\n";
        return 2;
    }

    if (batch || batch_individual) {
        std::string question;
        while (std::getline(std::cin, question)) {
            if (question.empty()) {
                std::cout << '\n';
                continue;
            }
            morok::g2::tsukuyomi::Model model;
            const auto path = batch_individual
                ? checkpoint_directory / question_checkpoint_name(question)
                : std::filesystem::path(checkpoint);
            if (!morok::g2::tsukuyomi::load_checkpoint(model, path)) {
                std::cerr << "Checkpoint inválido ou ausente para a pergunta: " << question << '\n';
                return 1;
            }
            std::cout << answer_for(model, question, maximum) << '\n';
            if (!std::cout.good()) return 1;
        }
        return std::cin.eof() ? 0 : 1;
    }

    const auto selected_checkpoint = individual
        ? checkpoint_directory / question_checkpoint_name(prompt)
        : std::filesystem::path(checkpoint);
    morok::g2::tsukuyomi::Model model;
    if (!morok::g2::tsukuyomi::load_checkpoint(model, selected_checkpoint)) {
        std::cerr << "Checkpoint inválido ou incompatível para a pergunta selecionada.\n";
        return 1;
    }

    morok::g2::tsukuyomi::GenerationConfig generation_config;
    for (int index = individual ? 5 : 4; index < argc; ++index) {
        const std::string option(argv[index]);
        if (option == "--sample") {
            generation_config.sample = true;
            continue;
        }
        if (index + 1 >= argc) {
            std::cerr << "Opção sem valor: " << option << '\n';
            return 2;
        }
        const char* value = argv[++index];
        if (option == "--temperature") {
            if (!parse_double_value(value, generation_config.temperature)) return 2;
        } else if (option == "--top-k") {
            if (!parse_size_value(value, generation_config.top_k)) return 2;
        } else if (option == "--top-p") {
            if (!parse_double_value(value, generation_config.top_p)) return 2;
        } else if (option == "--repetition-penalty") {
            if (!parse_double_value(value, generation_config.repetition_penalty)) return 2;
        } else if (option == "--repetition-window") {
            if (!parse_size_value(value, generation_config.repetition_window)) return 2;
        } else if (option == "--seed") {
            if (!parse_size_value(value, generation_config.seed)) return 2;
        } else {
            std::cerr << "Opção desconhecida: " << option << '\n';
            return 2;
        }
    }

    const std::string selected_prompt = individual ? "U: " + prompt : std::string(argv[2]);
    const std::vector<std::uint8_t> input(selected_prompt.begin(), selected_prompt.end());
    const auto result = morok::g2::tsukuyomi::generate(
        model, input, maximum, generation_config);
    for (std::size_t i = input.size(); i < result.size(); ++i) {
        const char byte = static_cast<char>(result[i]);
        if (byte == '\n' || byte == '\r' || byte == '\0') break;
        std::cout.put(byte);
    }
    std::cout.put('\n');
    return std::cout.good() ? 0 : 1;
}
