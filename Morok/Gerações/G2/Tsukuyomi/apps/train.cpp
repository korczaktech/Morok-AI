#include "morok/g2/tsukuyomi/model.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {
bool parse_size(const char* text, std::size_t& value) {
    const std::string input(text ? text : "");
    const auto result = std::from_chars(input.data(),
        input.data() + input.size(), value);
    return result.ec == std::errc{} && result.ptr == input.data() + input.size();
}
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        std::cerr << "Uso: morok-g2-tsukuyomi-train <corpus.bin> <checkpoint> [epocas] [seed]\n";
        return 2;
    }

    std::error_code error;
    const auto size = std::filesystem::file_size(argv[1], error);
    constexpr std::uintmax_t kMaximumCorpusBytes = 512ULL * 1024ULL * 1024ULL;
    if (error || size < 2 || size > kMaximumCorpusBytes) {
        std::cerr << "Corpus inválido: tamanho deve estar entre 2 bytes e 512 MiB.\n";
        return 2;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "Não foi possível abrir o corpus.\n";
        return 2;
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(size));
    char value = 0;
    while (input.get(value))
        bytes.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(value)));
    if (bytes.size() != size) {
        std::cerr << "Falha ao ler o corpus integralmente.\n";
        return 2;
    }

    morok::g2::tsukuyomi::TrainingConfig config;
    if (argc >= 4 && !parse_size(argv[3], config.epochs)) {
        std::cerr << "Número de épocas inválido.\n";
        return 2;
    }
    if (argc >= 5) {
        std::uint64_t seed = 0;
        const std::string text(argv[4]);
        const auto parsed = std::from_chars(text.data(),
            text.data() + text.size(), seed);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
            std::cerr << "Seed inválida.\n";
            return 2;
        }
        config.seed = seed;
    }

    morok::g2::tsukuyomi::Model model;
    morok::g2::tsukuyomi::initialize(model, config.seed);
    const auto report = morok::g2::tsukuyomi::train(model, bytes, config);
    if (report.epochs_completed != config.epochs ||
        !morok::g2::tsukuyomi::save_checkpoint(model, argv[2])) {
        std::cerr << "Treinamento ou gravação do checkpoint falhou.\n";
        return 1;
    }

    std::cout << "Tsukuyomi G2 v0.1\n"
              << "Parâmetros: "
              << morok::g2::tsukuyomi::kParameterCount << '\n'
              << "Épocas: " << report.epochs_completed << '\n'
              << "Bytes-alvo: " << report.target_bytes << '\n'
              << "Loss inicial: " << report.initial_loss << '\n'
              << "Loss final: " << report.final_loss << '\n';
    return 0;
}
