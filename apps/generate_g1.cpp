#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/learn/g1_generation.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string read_text(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("não foi possível abrir o prompt");
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) throw std::runtime_error("falha ao ler o prompt");
    return text;
}
void write_text(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("não foi possível criar o arquivo de saída");
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    if (!file) throw std::runtime_error("falha ao gravar a saída");
}
}
int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::cerr << "uso: morok_generate <checkpoint> <prompt.txt> <saida.txt> [max_bytes]\n";
        return 2;
    }
    try {
        const auto checkpoint = morok::model::load_g1_checkpoint(argv[1]);
        auto prompt_result = morok::common::Utf8Text::from_utf8(read_text(argv[2]));
        if (!prompt_result) throw std::runtime_error("prompt não é UTF-8 válido");
        auto prompt = std::move(prompt_result).value();
        std::size_t max_bytes = 256;
        if (argc == 5) max_bytes = static_cast<std::size_t>(std::stoull(argv[4]));
        const auto report = morok::model::generate_g1(
            checkpoint.parameters, prompt, {max_bytes});
        write_text(argv[3], report.continuation.string());
        std::cout << "{\"generated_bytes\":" << report.generated_bytes
                  << ",\"candidate_evaluations\":" << report.candidate_evaluations
                  << ",\"reached_byte_limit\":"
                  << (report.reached_byte_limit ? "true" : "false") << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok_generate: " << error.what() << '\n';
        return 1;
    }
}
