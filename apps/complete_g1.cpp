#include "morok/model/g1_forward.hpp"
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
    std::vector<std::uint8_t> bytes(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (file.bad()) throw std::runtime_error("erro ao ler entrada.txt");
    if (bytes.size() < 2) throw std::runtime_error("entrada.txt precisa conter pelo menos 2 bytes");
    if (bytes.size() > kMaxInputBytes) throw std::runtime_error("entrada.txt excede 64 KiB");
    return bytes;
}

std::vector<std::uint8_t> generate(const morok::model::G1Parameters& parameters,
                                   const std::vector<std::uint8_t>& prompt) {
    morok::model::G1State state{};
    for (const auto byte : prompt) {
        morok::model::advance_g1_state(parameters, state, byte);
    }

    std::vector<std::uint8_t> output;
    output.reserve(kGenerationBytes);
    for (std::size_t position = 0; position < kGenerationBytes; ++position) {
        double best_loss = std::numeric_limits<double>::infinity();
        std::uint8_t best_byte = static_cast<std::uint8_t>(' ');
        // Constrain generation to printable ASCII so saida.txt is always valid,
        // readable UTF-8. This first prototype does not generate accented bytes.
        for (unsigned int candidate = 32; candidate <= 126; ++candidate) {
            const auto evaluation = morok::model::evaluate_g1_next_byte(
                parameters, state, static_cast<std::uint8_t>(candidate));
            if (evaluation.negative_log_likelihood_nats < best_loss) {
                best_loss = evaluation.negative_log_likelihood_nats;
                best_byte = static_cast<std::uint8_t>(candidate);
            }
        }
        output.push_back(best_byte);
        morok::model::advance_g1_state(parameters, state, best_byte);
    }
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
