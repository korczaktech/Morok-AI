#include "morok/g3/inference/generator.hpp"
#include "morok/g3/inference/model_artifact.hpp"
#include "morok/g3/neural/gru_backend.hpp"
#include "morok/g3/training/checkpoint.hpp"
#include "morok/g3/training/dataset.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace morok;

std::unique_ptr<model::ModelBackend> load_backend(const std::filesystem::path& path) {
    const auto artifact = g3::inference::read_model_artifact(path);
    if (!artifact) throw std::runtime_error("Modelo G3 inválido ou incompatível");
    if (artifact.value_if()->metadata.precision != g3::inference::PrecisionMode::fp32 ||
        artifact.value_if()->payload.size() != g3::neural::kTargetParameterCount * sizeof(float))
        throw std::runtime_error("Analisador exige artefato G3 FP32 de 100.000 parâmetros");

    auto backend = std::make_unique<g3::neural::G3Backend>();
    std::vector<float> weights;
    weights.reserve(g3::neural::kTargetParameterCount);
    const auto& payload = artifact.value_if()->payload;
    for (std::size_t offset = 0; offset < payload.size(); offset += 4U) {
        std::uint32_t bits = 0U;
        for (unsigned byte = 0; byte < 4U; ++byte)
            bits |= std::to_integer<std::uint32_t>(payload[offset + byte]) << (byte * 8U);
        const float value = std::bit_cast<float>(bits);
        if (!std::isfinite(value)) throw std::runtime_error("Pesos G3 contêm valor não finito");
        weights.push_back(value);
    }
    if (!g3::training::restore_g3_model_weights(weights, *backend))
        throw std::runtime_error("Pesos não correspondem à arquitetura G3 atual");
    return backend;
}

double lexical_similarity(const std::string& expected, const std::string& actual) {
    const std::size_t width = actual.size() + 1U;
    std::vector<std::size_t> previous(width), current(width);
    for (std::size_t j = 0; j < width; ++j) previous[j] = j;
    for (std::size_t i = 1; i <= expected.size(); ++i) {
        current[0] = i;
        for (std::size_t j = 1; j < width; ++j)
            current[j] = std::min({previous[j] + 1U, current[j - 1U] + 1U,
                previous[j - 1U] + (expected[i - 1U] == actual[j - 1U] ? 0U : 1U)});
        previous.swap(current);
    }
    const std::size_t denominator = std::max(expected.size(), actual.size());
    if (denominator == 0U) return 100.0;
    const double score = 1.0 - static_cast<double>(previous.back()) /
                                  static_cast<double>(denominator);
    return std::clamp(score, 0.0, 1.0) * 100.0;
}

std::string json_escape(const std::string& value) {
    std::string out;
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20U) out += ' ';
                else out += static_cast<char>(c);
        }
    }
    return out;
}

struct Attempt {
    std::string id;
    std::string question;
    std::string expected;
};

std::vector<Attempt> build_attempts(const g3::training::Dataset& dataset) {
    std::vector<Attempt> attempts;
    for (const auto& example : dataset.examples) {
        attempts.push_back({example.example_id, example.question, example.answer});
        for (std::size_t i = 0; i < example.question_variants.size(); ++i)
            attempts.push_back({example.example_id + "-v" + std::to_string(i + 1U),
                                example.question_variants[i], example.answer});
    }
    return attempts;
}
}

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::cerr << "Uso: morok-g3-analyze <dataset.jsonl> <modelo.morok> <relatorio.jsonl> [limiar-percentual]\n";
        return 2;
    }
    try {
        const double threshold = argc == 5 ? std::stod(argv[4]) : 80.0;
        if (!std::isfinite(threshold) || threshold < 0.0 || threshold > 100.0)
            throw std::invalid_argument("Limiar lexical deve estar entre 0 e 100");

        const auto dataset = g3::training::load_jsonl(argv[1]);
        const auto attempts = build_attempts(dataset);
        if (attempts.empty()) throw std::runtime_error("Dataset sem perguntas para analisar");

        auto backend = load_backend(argv[2]);
        const auto descriptor = backend->descriptor();
        std::vector<std::byte> state(descriptor.state_size_bytes);
        g3::inference::GenerationConfig config;
        config.temperature = 0.0;
        config.top_k = 1U;
        config.max_input_bytes = 4096U;
        config.max_output_bytes = 512U;
        config.empty_response = g3::inference::EmptyResponsePolicy::allow;
        std::mt19937_64 random(42U);

        const std::filesystem::path report_path(argv[3]);
        if (!report_path.parent_path().empty())
            std::filesystem::create_directories(report_path.parent_path());
        std::ofstream report(report_path, std::ios::trunc);
        if (!report) throw std::runtime_error("Não foi possível criar relatório lexical");

        std::size_t correct = 0U;
        for (std::size_t index = 0; index < attempts.size(); ++index) {
            const auto& attempt = attempts[index];
            const auto generated = g3::inference::generate(
                *backend, state, attempt.question, config, random);
            if (!generated)
                throw std::runtime_error("Falha de inferência na pergunta " +
                                         std::to_string(index + 1U) + ": " +
                                         generated.error_if()->message);

            const double lexical = lexical_similarity(attempt.expected,
                                                        generated.value_if()->text);
            const bool is_correct = lexical >= threshold;
            if (is_correct) ++correct;

            const char* color = is_correct ? "\033[32m" : "\033[31m";
            const char* status = is_correct ? "CORRETO" : "INCORRETO";
            std::cout << color << (is_correct ? "Sucesso" : "Falha")
                      << " | Pergunta " << std::setw(3) << std::setfill('0') << (index + 1U)
                      << "/" << attempts.size() << " | " << status
                      << " | Nivel lexical: " << std::fixed << std::setprecision(1)
                      << lexical << "%\033[0m\n";

            report << "{\"question_id\":\"" << json_escape(attempt.id)
                   << "\",\"question\":\"" << json_escape(attempt.question)
                   << "\",\"expected\":\"" << json_escape(attempt.expected)
                   << "\",\"response\":\"" << json_escape(generated.value_if()->text)
                   << "\",\"lexical_level\":" << std::setprecision(8) << lexical
                   << ",\"threshold\":" << threshold
                   << ",\"status\":\"" << status << "\"}\n";
            if (!report) throw std::runtime_error("Falha ao gravar relatório lexical");
        }

        std::cout << "Resumo | " << correct << "/" << attempts.size()
                  << " corretas | " << std::fixed << std::setprecision(1)
                  << (100.0 * static_cast<double>(correct) /
                      static_cast<double>(attempts.size()))
                  << "% | Limiar lexical: " << threshold << "%\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Falha no analisador de respostas G3: " << error.what() << '\n';
        return 1;
    }
}
