#include "morok/g2/tsukuyomi/model.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

double evaluate_dialogue_corpus_loss(
    const morok::g2::tsukuyomi::Model& model,
    const std::vector<std::uint8_t>& bytes) {
    double weighted_loss = 0.0;
    std::uint64_t total_targets = 0;
    std::vector<std::uint8_t> dialogue;
    std::size_t line_start = 0;
    bool has_dialogue = false;

    const auto evaluate_dialogue = [&]() -> bool {
        if (dialogue.empty()) return true;
        const auto mask = make_dialogue_target_mask(dialogue);
        const auto targets = static_cast<std::uint64_t>(std::count_if(
            mask.begin(), mask.end(), [](std::uint8_t value) { return value != 0; }));
        if (targets == 0) return false;
        const double loss = morok::g2::tsukuyomi::evaluate_loss(model, dialogue, mask);
        if (!std::isfinite(loss)) return false;
        weighted_loss += loss * static_cast<double>(targets);
        total_targets += targets;
        dialogue.clear();
        return true;
    };

    while (line_start < bytes.size()) {
        std::size_t line_end = line_start;
        while (line_end < bytes.size() && bytes[line_end] != '\n' &&
               bytes[line_end] != '\r')
            ++line_end;
        std::size_t marker = line_start;
        while (marker < line_end && (bytes[marker] == ' ' || bytes[marker] == '\t'))
            ++marker;
        const bool starts_question =
            line_end - marker >= 2 && bytes[marker] == 'U' && bytes[marker + 1] == ':';
        if (starts_question) {
            if (!dialogue.empty()) {
                // Ignora cabeçalhos/comentários antes da primeira pergunta.
                // Só segmentos iniciados por U: representam diálogos avaliáveis.
                if (has_dialogue && !evaluate_dialogue())
                    return std::numeric_limits<double>::infinity();
                if (!has_dialogue)
                    dialogue.clear();
            }
            has_dialogue = true;
        }

        std::size_t line_after = line_end;
        if (line_after < bytes.size() && bytes[line_after] == '\r') ++line_after;
        if (line_after < bytes.size() && bytes[line_after] == '\n') ++line_after;
        dialogue.insert(dialogue.end(), bytes.begin() + static_cast<std::ptrdiff_t>(line_start),
                        bytes.begin() + static_cast<std::ptrdiff_t>(line_after));
        line_start = line_after;
    }

    if (!evaluate_dialogue() || total_targets == 0)
        return std::numeric_limits<double>::infinity();
    return weighted_loss / static_cast<double>(total_targets);
}


struct DialogueLoss { double loss = 0.0; std::uint64_t targets = 0; };

std::vector<DialogueLoss> evaluate_each_dialogue(
    const morok::g2::tsukuyomi::Model& model,
    const std::vector<std::uint8_t>& bytes) {
    std::vector<DialogueLoss> results;
    std::vector<std::uint8_t> dialogue;
    std::size_t line_start = 0;
    bool has_dialogue = false;
    const auto finish = [&]() -> bool {
        if (dialogue.empty()) return true;
        const auto mask = make_dialogue_target_mask(dialogue);
        const auto targets = static_cast<std::uint64_t>(std::count_if(
            mask.begin(), mask.end(), [](std::uint8_t value) { return value != 0; }));
        if (targets == 0) return false;
        const double loss = morok::g2::tsukuyomi::evaluate_loss(model, dialogue, mask);
        if (!std::isfinite(loss)) return false;
        results.push_back({loss, targets});
        dialogue.clear();
        return true;
    };
    while (line_start < bytes.size()) {
        std::size_t line_end = line_start;
        while (line_end < bytes.size() && bytes[line_end] != '\n' && bytes[line_end] != '\r')
            ++line_end;
        std::size_t marker = line_start;
        while (marker < line_end && (bytes[marker] == ' ' || bytes[marker] == '\t')) ++marker;
        const bool starts_question = line_end - marker >= 2 &&
            bytes[marker] == 'U' && bytes[marker + 1] == ':';
        if (starts_question) {
            if (has_dialogue && !finish()) return {};
            has_dialogue = true;
        }
        std::size_t line_after = line_end;
        if (line_after < bytes.size() && bytes[line_after] == '\r') ++line_after;
        if (line_after < bytes.size() && bytes[line_after] == '\n') ++line_after;
        if (has_dialogue)
            dialogue.insert(dialogue.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(line_start),
                bytes.begin() + static_cast<std::ptrdiff_t>(line_after));
        line_start = line_after;
    }
    if (!has_dialogue || !finish()) return {};
    return results;
}

bool parse_size(const char* text, std::size_t& value) {
    const std::string input(text ? text : "");
    const auto result = std::from_chars(input.data(),
        input.data() + input.size(), value);
    return result.ec == std::errc{} && result.ptr == input.data() + input.size();
}
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--evaluate-each") {
        std::error_code error;
        const auto size = std::filesystem::file_size(argv[2], error);
        if (error || size < 2 || size > 512ULL * 1024ULL * 1024ULL) return 2;
        std::ifstream input(argv[2], std::ios::binary);
        if (!input) return 2;
        std::vector<std::uint8_t> bytes;
        bytes.reserve(static_cast<std::size_t>(size));
        char value = 0;
        while (input.get(value))
            bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
        if (bytes.size() != size) return 2;
        morok::g2::tsukuyomi::Model model;
        if (!morok::g2::tsukuyomi::load_checkpoint(model, argv[3])) return 1;
        const auto results = evaluate_each_dialogue(model, bytes);
        if (results.empty()) return 1;
        for (std::size_t i = 0; i < results.size(); ++i)
            std::cout << i + 1 << '\t' << results[i].loss << '\t'
                      << results[i].loss / std::log(2.0) << '\t'
                      << results[i].targets << '\n';
        return 0;
    }

    // Modo de avaliação: calcula entropia cruzada no holdout sem atualizar pesos.
    if (argc == 4 && std::string(argv[1]) == "--evaluate") {
        std::error_code eval_error;
        const auto eval_size = std::filesystem::file_size(argv[2], eval_error);
        constexpr std::uintmax_t kMaximumEvaluationBytes = 512ULL * 1024ULL * 1024ULL;
        if (eval_error || eval_size < 2 || eval_size > kMaximumEvaluationBytes) {
            std::cerr << "Corpus de avaliação inválido.\n";
            return 2;
        }
        std::ifstream eval_input(argv[2], std::ios::binary);
        if (!eval_input) {
            std::cerr << "Não foi possível abrir o corpus de avaliação.\n";
            return 2;
        }
        std::vector<std::uint8_t> eval_bytes;
        eval_bytes.reserve(static_cast<std::size_t>(eval_size));
        char eval_value = 0;
        while (eval_input.get(eval_value))
            eval_bytes.push_back(static_cast<std::uint8_t>(
                static_cast<unsigned char>(eval_value)));
        if (eval_bytes.size() != eval_size) {
            std::cerr << "Falha ao ler o corpus de avaliação integralmente.\n";
            return 2;
        }
        const auto eval_mask = make_dialogue_target_mask(eval_bytes);
        if (eval_mask.empty()) {
            std::cerr << "Corpus de avaliação não contém alvos IA: válidos.\n";
            return 2;
        }
        morok::g2::tsukuyomi::Model eval_model;
        if (!morok::g2::tsukuyomi::load_checkpoint(eval_model, argv[3])) {
            std::cerr << "Checkpoint ausente ou inválido para avaliação.\n";
            return 1;
        }
        const double loss = evaluate_dialogue_corpus_loss(eval_model, eval_bytes);
        if (!std::isfinite(loss)) {
            std::cerr << "Loss de validação não finito.\n";
            return 1;
        }
        std::cout << "validation_loss: " << loss << '\n';
        return 0;
    }
    if (argc < 3 || argc > 9) {
        std::cerr << "Uso: morok-g2-tsukuyomi-train <corpus.bin> <checkpoint> [epocas] [seed] [learning_rate] [--resume] [--lr-map arquivo]\nOu: --evaluate-each <corpus> <checkpoint>\n";
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
    config.weight_decay = 0.0; // Weight decay desativado no Tsukuyomi G2 0.1.
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
    std::filesystem::path learning_rate_map_path;
    bool learning_rate_seen = false;
    for (int index = 5; index < argc;) {
        const std::string option(argv[index]);
        if (option == "--resume") {
            if (resume) {
                std::cerr << "Opção --resume repetida.\n";
                return 2;
            }
            resume = true;
            ++index;
        } else if (option == "--lr-map") {
            if (!learning_rate_map_path.empty() || index + 1 >= argc) {
                std::cerr << "Uso inválido de --lr-map.\n";
                return 2;
            }
            learning_rate_map_path = argv[index + 1];
            index += 2;
        } else if (!learning_rate_seen) {
            char* end = nullptr;
            const double learning_rate = std::strtod(option.c_str(), &end);
            if (end != option.c_str() + option.size() ||
                !std::isfinite(learning_rate) || learning_rate < 1.0e-5 ||
                learning_rate > 1.0e-2) {
                std::cerr << "Learning rate inválido (1e-5 <= valor <= 1e-2).\n";
                return 2;
            }
            config.learning_rate = learning_rate;
            learning_rate_seen = true;
            ++index;
        } else {
            std::cerr << "Argumento desconhecido: " << option << '\n';
            return 2;
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
    std::vector<double> target_learning_rates;
    if (!learning_rate_map_path.empty()) {
        target_learning_rates.assign(bytes.size(), config.learning_rate);
        std::vector<std::uint8_t> assigned(bytes.size(), 0);
        std::ifstream rate_map(learning_rate_map_path);
        if (!rate_map) {
            std::cerr << "Não foi possível abrir o mapa de learning rate por questão.\n";
            return 2;
        }
        std::size_t begin = 0, end = 0;
        double rate = 0.0;
        std::size_t ranges = 0;
        while (rate_map >> begin >> end >> rate) {
            if (begin >= end || end > bytes.size() || !std::isfinite(rate) ||
                rate < 1.0e-5 || rate > 1.0e-2) {
                std::cerr << "Intervalo ou learning rate inválido no mapa por questão.\n";
                return 2;
            }
            for (std::size_t position = begin; position < end; ++position) {
                if (assigned[position]) {
                    std::cerr << "Intervalos sobrepostos no mapa de learning rate.\n";
                    return 2;
                }
                assigned[position] = 1;
                target_learning_rates[position] = rate;
            }
            ++ranges;
        }
        if (!rate_map.eof() || ranges == 0) {
            std::cerr << "Mapa de learning rate vazio ou malformado.\n";
            return 2;
        }
        std::cout << "Faixas de learning rate por questão: " << ranges << '\n';
    }
    const auto report = morok::g2::tsukuyomi::train(
        model, bytes, config, &optimizer_state, target_mask, target_learning_rates);
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
              << morok::g2::tsukuyomi::kBpttTruncation << ')' << '\n'
              << "Estado recorrente: " << morok::g2::tsukuyomi::kHiddenSize
              << " dimensões" << '\n'
              << "Learning rate: " << config.learning_rate << " ("
              << config.learning_rate * 100.0 << "%)" << '\n'
              << "Weight decay: desativado (0)" << '\n'
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
