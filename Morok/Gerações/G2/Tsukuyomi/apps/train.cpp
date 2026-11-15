#include "morok/g2/tsukuyomi/model.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    if (argc < 3 || argc > 6) {
        std::cerr << "Uso: morok-g2-tsukuyomi-train <corpus.bin> <checkpoint> [epocas] [seed] [--resume]\n";
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

    const bool resume = argc == 6 && std::string(argv[5]) == "--resume";
    if (argc == 6 && !resume) {
        std::cerr << "Opção inválida: use --resume.\n";
        return 2;
    }

    morok::g2::tsukuyomi::Model model;
    morok::g2::tsukuyomi::TrainingState optimizer_state;
    const std::filesystem::path optimizer_path = std::string(argv[2]) + ".adam";
    if (resume) {
        if (!morok::g2::tsukuyomi::load_checkpoint(model, argv[2])) {
            std::cerr << "Não foi possível retomar: checkpoint ausente ou inválido.\n";
            return 1;
        }
        if (!morok::g2::tsukuyomi::load_training_state(optimizer_state, optimizer_path)) {
            std::cerr << "Estado Adam ausente ou inválido; não é possível retomar com segurança.\n";
            return 1;
        }
    } else {
        morok::g2::tsukuyomi::initialize(model, config.seed);
    }

    const auto report = morok::g2::tsukuyomi::train(model, bytes, config, &optimizer_state);
    if (report.epochs_completed != config.epochs ||
        !morok::g2::tsukuyomi::save_checkpoint(model, argv[2]) ||
        !morok::g2::tsukuyomi::save_training_state(optimizer_state, optimizer_path)) {
        std::cerr << "Treinamento ou gravação do checkpoint/estado Adam falhou.\n";
        return 1;
    }

    std::cout << "Tsukuyomi G2 v0.1\n"
              << "Parâmetros: "
              << morok::g2::tsukuyomi::kParameterCount << '\n'
              << "Modo: " << (resume ? "continuação" : "inicialização") << '\n'
              << "Épocas: " << report.epochs_completed << '\n'
              << "Mini-lote: " << config.batch_size << '\n'
              << "Atualizações Adam: " << optimizer_state.step << '\n'
              << "Bytes-alvo: " << report.target_bytes << '\n'
              << "Loss inicial: " << report.initial_loss << '\n'
              << "Loss final: " << report.final_loss << '\n'
              << "train_bpb inicial: " << report.initial_bpb << '\n'
              << "train_bpb final: " << report.final_bpb << '\n';
    return 0;
}
