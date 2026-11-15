#include "morok/common/utf8_text.hpp"
#include "morok/model/learn/g1_generation.hpp"
#include "morok/model/learn/g1_bptt.hpp"
#include "morok/model/learn/optimizer.hpp"
#include "morok/model/parameters/g1_parameters.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr std::size_t kMaxInputBytes = 64U * 1024U;
constexpr std::size_t kTrainingEpochs = 50;
constexpr std::size_t kGenerationBytes = 160;
constexpr std::size_t kBpttWindow = 64;

std::uint64_t input_seed(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::vector<std::uint8_t> read_input(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("nao foi possivel abrir entrada.txt");
    const std::vector<char> raw(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>{});
    if (file.bad()) throw std::runtime_error("erro ao ler entrada.txt");
    std::vector<std::uint8_t> bytes;
    bytes.reserve(raw.size());
    for (const unsigned char byte : raw) {
        bytes.push_back(static_cast<std::uint8_t>(byte));
    }
    if (bytes.size() < 2) throw std::runtime_error("entrada.txt precisa conter pelo menos 2 bytes");
    if (bytes.size() > kMaxInputBytes) throw std::runtime_error("entrada.txt excede 64 KiB");
    return bytes;
}

std::vector<std::uint8_t> generate(const morok::model::G1Parameters& parameters,
                                   const std::vector<std::uint8_t>& prompt_bytes) {
    const std::string prompt_text(
        reinterpret_cast<const char*>(prompt_bytes.data()), prompt_bytes.size());
    auto prompt = morok::common::Utf8Text::from_utf8(prompt_text);
    if (!prompt) throw std::runtime_error("entrada.txt não contém UTF-8 válido");

    morok::model::G1GenerationConfig config;
    config.max_new_bytes = kGenerationBytes;
    config.temperature = 0.8;
    config.top_k = 0;
    config.seed = input_seed(prompt_bytes);
    config.repetition_penalty = 0.0;
    config.repetition_window = 48;
    config.printable_only = true;
    const auto report = morok::model::generate_g1(parameters, *prompt.value_if(), config);
    const auto& text = report.continuation.string();
    std::vector<std::uint8_t> output;
    output.reserve(text.size());
    for (const unsigned char byte : text)
        output.push_back(static_cast<std::uint8_t>(byte));
    return output;
}
} // namespace

int main() {
    try {
        const auto input = read_input("entrada.txt");
        morok::model::G1Parameters parameters;
        morok::model::initialize_g1_parameters(parameters, input_seed(input));

        auto optimizer_config = morok::model::G1OptimizerConfig::adamw();
        auto optimizer_state = morok::model::make_g1_optimizer_state(
            morok::model::G1OptimizerKind::AdamW);

        for (std::size_t epoch = 0; epoch < kTrainingEpochs; ++epoch) {
            const auto gradients = morok::model::compute_g1_bptt_gradients(
                parameters, input, kBpttWindow);
            (void)morok::model::apply_g1_optimizer_step(
                parameters, gradients.gradients, optimizer_config, optimizer_state);
        }

        const auto continuation = generate(parameters, input);
        std::ofstream output_file("saida.txt", std::ios::binary | std::ios::trunc);
        if (!output_file) throw std::runtime_error("nao foi possivel criar saida.txt");
        output_file.write(reinterpret_cast<const char*>(continuation.data()),
                          static_cast<std::streamsize>(continuation.size()));
        output_file.put('\n');
        output_file.close();
        if (!output_file) throw std::runtime_error("erro ao gravar saida.txt");

        std::cout << "Entrada processada: " << input.size() << " bytes\n"
                  << "Epocas de treino: " << kTrainingEpochs << "\n"
                  << "Continuacao gravada em saida.txt: " << continuation.size()
                  << " bytes (ASCII imprimivel)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok_complete: " << error.what() << '\n';
        return 1;
    }
}
