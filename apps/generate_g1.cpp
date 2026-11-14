#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/learn/g1_generation.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
constexpr std::size_t kMaxPromptBytes = 64U * 1024U;

std::string read_text(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("não foi possível abrir o prompt");
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) throw std::runtime_error("falha ao ler o prompt");
    if (text.size() > kMaxPromptBytes) throw std::runtime_error("prompt excede 64 KiB");
    return text;
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("não foi possível criar o arquivo de saída");
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    if (!file) throw std::runtime_error("falha ao gravar a saída");
}

std::size_t parse_size(const std::string& value, const char* option) {
    if (value.empty() || value.front() == '-') throw std::invalid_argument(option);
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed);
    if (consumed != value.size() || parsed > std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument(option);
    return static_cast<std::size_t>(parsed);
}

double parse_double(const std::string& value, const char* option) {
    std::size_t consumed = 0;
    const double parsed = std::stod(value, &consumed);
    if (consumed != value.size() || !std::isfinite(parsed))
        throw std::invalid_argument(option);
    return parsed;
}

void print_usage() {
    std::cerr
        << "uso: morok_generate <checkpoint> <prompt.txt> <saida.txt> [max_bytes]"
        << " [--temperature n] [--top-k n] [--seed n]"
        << " [--repetition-penalty n] [--repetition-window n]"
        << " [--eos-threshold n] [--min-new-bytes n]\n"
        << "temperature 0 mantém geração gulosa; valor positivo ativa amostragem.\n";
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        print_usage();
        return 2;
    }
    try {
        const auto checkpoint = morok::model::load_g1_checkpoint(argv[1]);
        auto prompt_result = morok::common::Utf8Text::from_utf8(read_text(argv[2]));
        if (!prompt_result) throw std::runtime_error("prompt não é UTF-8 válido");
        auto prompt = std::move(*prompt_result.value_if());

        morok::model::G1GenerationConfig config;
        bool positional_limit_seen = false;
        for (int i = 4; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--temperature" || option == "--top-k" ||
                option == "--seed" || option == "--repetition-penalty" ||
                option == "--repetition-window" || option == "--eos-threshold" ||
                option == "--min-new-bytes") {
                if (i + 1 >= argc) throw std::invalid_argument("opção sem valor: " + option);
                const std::string value = argv[++i];
                if (option == "--temperature")
                    config.temperature = parse_double(value, "temperature inválida");
                else if (option == "--top-k")
                    config.top_k = parse_size(value, "top-k inválido");
                else if (option == "--seed")
                    config.seed = static_cast<std::uint64_t>(parse_size(value, "seed inválida"));
                else if (option == "--repetition-penalty")
                    config.repetition_penalty = parse_double(value, "penalidade inválida");
                else if (option == "--repetition-window")
                    config.repetition_window = parse_size(value, "janela de repetição inválida");
                else if (option == "--eos-threshold")
                    config.eos_threshold = parse_double(value, "limiar EOS inválido");
                else
                    config.min_new_bytes = parse_size(value, "mínimo de bytes inválido");
            } else if (!option.empty() && option.front() != '-' && !positional_limit_seen) {
                config.max_new_bytes = parse_size(option, "limite de bytes inválido");
                positional_limit_seen = true;
            } else {
                throw std::invalid_argument("opção desconhecida: " + option);
            }
        }

        const auto report = morok::model::generate_g1(checkpoint.parameters, prompt, config);
        write_text(argv[3], report.continuation.string());
        std::cout << "{\"generated_bytes\":" << report.generated_bytes
                  << ",\"candidate_evaluations\":" << report.candidate_evaluations
                  << ",\"reached_byte_limit\":"
                  << (report.reached_byte_limit ? "true" : "false")
                  << ",\"stopped_by_eos\":"
                  << (report.stopped_by_eos ? "true" : "false") << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok_generate: " << error.what() << '\n';
        return 1;
    }
}
