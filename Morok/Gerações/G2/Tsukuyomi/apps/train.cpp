#include "morok/g2/tsukuyomi/model.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> make_dialogue_target_mask(
    const std::vector<std::uint8_t>& bytes) {
    std::vector<std::uint8_t> mask(bytes.size(), 0);
    bool awaiting_answer = false;
    std::size_t line_start = 0;
    // BOM UTF-8 opcional no início do arquivo.
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        line_start = 3;
    while (line_start < bytes.size()) {
        std::size_t line_end = line_start;
        while (line_end < bytes.size() && bytes[line_end] != '\n' &&
               bytes[line_end] != '\r')
            ++line_end;
        std::size_t marker = line_start;
        while (marker < line_end && (bytes[marker] == ' ' || bytes[marker] == '\t'))
            ++marker;
        const std::size_t length = line_end - marker;
        if (length >= 2 && bytes[marker] == 'U' && bytes[marker + 1] == ':') {
            awaiting_answer = true;
        } else if (length >= 3 && bytes[marker] == 'I' &&
                   bytes[marker + 1] == 'A' && bytes[marker + 2] == ':' &&
                   awaiting_answer) {
            for (std::size_t i = marker + 3; i < line_end; ++i)
                mask[i] = 1;
            // O modelo também precisa aprender o terminador que a inferência consome.
            std::size_t terminator = line_end;
            if (terminator < bytes.size() && bytes[terminator] == '\r') {
                mask[terminator] = 1;
                ++terminator;
            }
            if (terminator < bytes.size() && bytes[terminator] == '\n')
                mask[terminator] = 1;
            awaiting_answer = false;
        }
        line_start = line_end;
        while (line_start < bytes.size() &&
               (bytes[line_start] == '\r' || bytes[line_start] == '\n'))
            ++line_start;
    }
    if (std::none_of(mask.begin(), mask.end(),
                     [](std::uint8_t value) { return value != 0; }))
        return {};
    return mask;
}

bool parse_size(const char* text, std::size_t& value) {
    const std::string input(text ? text : "");
    const auto result = std::from_chars(input.data(),
        input.data() + input.size(), value);
    return result.ec == std::errc{} && result.ptr == input.data() + input.size();
}
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 7) {
        std::cerr << "Uso: morok-g2-tsukuyomi-train <corpus.bin> <checkpoint> [epocas] [seed] [learning_rate] [--resume]\n";
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

    bool resume = false;
    if (argc >= 6) {
        const std::string option(argv[5]);
        if (option == "--resume") {
            resume = true;
        } else {
            char* end = nullptr;
            const double learning_rate = std::strtod(option.c_str(), &end);
            if (end != option.c_str() + option.size() ||
                !std::isfinite(learning_rate) || learning_rate <= 0.0 ||
                learning_rate > 0.1) {
                std::cerr << "Learning rate inválido (0 < valor <= 0,1).\n";
                return 2;
            }
            config.learning_rate = learning_rate;
            if (argc == 7) {
                resume = std::string(argv[6]) == "--resume";
                if (!resume) {
                    std::cerr << "Opção inválida: use --resume.\n";
                    return 2;
                }
            }
        }
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

    const auto target_mask = make_dialogue_target_mask(bytes);
    const auto report = morok::g2::tsukuyomi::train(
        model, bytes, config, &optimizer_state, target_mask);
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
              << "Objetivo: " << (target_mask.empty() ? "todos os bytes" : "somente respostas IA:") << '\n'
              << "Mini-lote: " << config.batch_size << '\n'
              << "BPTT truncado: " << config.bptt_steps << " etapas (máximo "
              << morok::g2::tsukuyomi::kBpttTruncation << ')' << '\\n'
              << "Estado recorrente: " << morok::g2::tsukuyomi::kHiddenSize
              << " dimensões" << '\\n'
              << "Learning rate: " << config.learning_rate << " ("
              << config.learning_rate * 100.0 << "%)" << '\n'
              << "Weight decay: " << config.weight_decay << " (seletivo)" << '\n'
              << "Dropout: não utilizado" << '\n'
              << "Gradient clipping: " << config.gradient_clip << '\n'
              << "Atualizações Adam: " << optimizer_state.step << '\n'
              << "Bytes-alvo: " << report.target_bytes << '\n'
              << "Loss inicial: " << report.initial_loss << '\n'
              << "Loss final: " << report.final_loss << '\n'
              << "train_bpb inicial: " << report.initial_bpb << '\n'
              << "train_bpb final: " << report.final_bpb << '\n';
    return 0;
}
