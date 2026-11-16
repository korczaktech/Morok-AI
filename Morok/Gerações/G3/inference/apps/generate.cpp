#include "morok/g3/inference/benchmark.hpp"
#include "morok/g3/inference/int8_backend.hpp"
#include "morok/g3/inference/model_artifact.hpp"
#include "morok/g3/inference/session_manager.hpp"
#include "morok/g3/neural/gru_backend.hpp"
#include "morok/g3/training/checkpoint.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0U;
    for (unsigned i = 0; i < 4U; ++i)
        value |= static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(bytes[offset + i])) << (i * 8U);
    return value;
}

std::unique_ptr<morok::model::ModelBackend> load_backend(
    const morok::g3::inference::ModelArtifact& artifact) {
    using namespace morok::g3;
    if (artifact.metadata.precision ==
        inference::PrecisionMode::int8_symmetric_per_row) {
        auto loaded = inference::load_g3_int8_backend(artifact);
        if (!loaded) throw std::runtime_error("Invalid or incompatible G3 INT8 model");
        return std::move(*loaded.value_if());
    }
    if (artifact.metadata.precision != inference::PrecisionMode::fp32 ||
        artifact.payload.size() != neural::kTargetParameterCount * sizeof(float))
        throw std::runtime_error("Unsupported G3 model precision or payload");

    auto backend = std::make_unique<neural::G3Backend>();
    std::vector<float> weights;
    weights.reserve(neural::kTargetParameterCount);
    for (std::size_t offset = 0; offset < artifact.payload.size(); offset += 4U) {
        const float value = std::bit_cast<float>(read_u32(artifact.payload, offset));
        if (!std::isfinite(value)) throw std::runtime_error("Model contains non-finite FP32 weights");
        weights.push_back(value);
    }
    const auto restored = training::restore_g3_model_weights(weights, *backend);
    if (!restored) throw std::runtime_error("Model weights do not match G3 architecture");
    return backend;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Uso: morok-g3-generate <modelo.morok> [--benchmark] [prompt]\n";
        return 2;
    }
    try {
        const auto artifact = morok::g3::inference::read_model_artifact(argv[1]);
        if (!artifact) throw std::runtime_error("Unable to load or validate .morok model");
        auto backend = load_backend(*artifact.value_if());

        morok::g3::inference::GenerationConfig config;
        morok::g3::inference::SessionManager sessions;
        bool benchmark_mode = false;
        std::string prompt;
        for (int i = 2; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--benchmark") {
                benchmark_mode = true;
            } else if (argument.starts_with("--temperature=")) {
                config.temperature = std::stod(std::string(argument.substr(14U)));
            } else if (argument.starts_with("--top-k=")) {
                config.top_k = static_cast<std::size_t>(
                    std::stoull(std::string(argument.substr(8U))));
            } else if (argument.starts_with("--max-output=")) {
                config.max_output_bytes = static_cast<std::size_t>(
                    std::stoull(std::string(argument.substr(13U))));
            } else {
                if (!prompt.empty()) prompt.push_back(' ');
                prompt.append(argument);
            }
        }

        if (benchmark_mode) {
            if (prompt.empty()) throw std::invalid_argument("--benchmark requires a prompt");
            config.empty_response =
                morok::g3::inference::EmptyResponsePolicy::allow;
            std::vector<std::byte> state(backend->descriptor().state_size_bytes);
            std::mt19937_64 random(42U);
            morok::g3::inference::BenchmarkConfig benchmark;
            benchmark.warmup_runs = 5U;
            benchmark.measured_runs = 30U;
            const auto report = morok::g3::inference::benchmark_inference(
                *backend, state, prompt, config, benchmark, random);
            if (!report) throw std::runtime_error("G3 inference benchmark failed");
            const auto& r = *report.value_if();
            std::cout << "{\"precision\":\""
                      << (artifact.value_if()->metadata.precision ==
                          morok::g3::inference::PrecisionMode::fp32 ? "FP32" : "INT8")
                      << "\",\"runs\":" << r.measured_runs
                      << ",\"session_state_bytes\":" << r.state_bytes_per_session
                      << ",\"rss_before_bytes\":" << r.resident_bytes_before
                      << ",\"rss_after_bytes\":" << r.resident_bytes_after
                      << ",\"peak_rss_bytes\":" << r.peak_resident_bytes
                      << ",\"median_latency_ns\":" << r.median_latency_ns
                      << ",\"p95_latency_ns\":" << r.p95_latency_ns
                      << ",\"output_bytes_per_second\":" << r.output_bytes_per_second
                      << "}\n";
            return 0;
        }

        if (!prompt.empty()) {
            const auto result = sessions.generate(*backend, "cli", prompt, config);
            if (!result) throw std::runtime_error("G3 generation failed");
            std::cout << result.value_if()->text << '\n';
            if (result.value_if()->repetition_detected)
                std::cerr << "[G3] generation stopped: repeated n-gram detected\n";
            return 0;
        }

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "/sair") break;
            const auto result = sessions.generate(*backend, "interactive", line, config);
            if (!result) {
                std::cerr << "[G3] generation failed\n";
                continue;
            }
            std::cout << result.value_if()->text << "\n\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Falha na inferência G3: " << error.what() << '\n';
        return 1;
    }
}
