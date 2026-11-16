#include "morok/g3/inference/benchmark.hpp"
#include "morok/g3/inference/generator.hpp"
#include "morok/g3/inference/model_artifact.hpp"
#include "morok/g3/inference/quantization.hpp"

#include "morok/lang/byte_tokenizer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <random>
#include <span>
#include <thread>
#include <vector>

using namespace morok;
using namespace morok::g3::inference;

namespace {

class FixtureBackend final : public model::ModelBackend {
public:
    explicit FixtureBackend(bool immediate_eos = false,
                            std::chrono::milliseconds delay = std::chrono::milliseconds{0})
        : immediate_eos_(immediate_eos), delay_(delay) {}

    model::ModelDescriptor descriptor() const noexcept override {
        return {3U, kG3ArchitectureGruRmsNormTied, 261U, 100000U,
                sizeof(std::uint32_t), alignof(std::uint32_t), true};
    }

    common::Status reset_state(std::span<std::byte> state) const override {
        if (state.size() != sizeof(std::uint32_t)) {
            return common::Status::failure(common::Error::make(
                common::ErrorCode::invalid_argument, "bad fixture state"));
        }
        const std::uint32_t zero = 0;
        std::memcpy(state.data(), &zero, sizeof(zero));
        return common::Status::success();
    }

    common::Status consume_byte(
        std::span<std::byte> state, std::uint8_t) const override {
        std::uint32_t count = 0;
        std::memcpy(&count, state.data(), sizeof(count));
        ++count;
        std::memcpy(state.data(), &count, sizeof(count));
        return common::Status::success();
    }

    common::Status next_token_log_probabilities(
        std::span<const std::byte> state,
        std::span<float> output) const override {
        if (delay_.count() > 0) std::this_thread::sleep_for(delay_);
        if (state.size() != sizeof(std::uint32_t) ||
            output.size() != lang::kVocabularySize) {
            return common::Status::failure(common::Error::make(
                common::ErrorCode::invalid_argument, "bad fixture output"));
        }
        std::fill(output.begin(), output.end(),
                  -std::numeric_limits<float>::infinity());
        std::uint32_t count = 0;
        std::memcpy(&count, state.data(), sizeof(count));
        const auto token = immediate_eos_ || count > 1U
            ? lang::kEosToken : static_cast<lang::TokenId>('A');
        output[token] = 0.0F;
        return common::Status::success();
    }

private:
    bool immediate_eos_{false};
    std::chrono::milliseconds delay_{0};
};

ModelArtifact valid_fp32_artifact() {
    ModelArtifact artifact;
    artifact.metadata.state_size_bytes = 660U;
    artifact.metadata.parameter_count = 100000U;
    artifact.payload.resize(100000U * sizeof(float), std::byte{0});
    return artifact;
}

void test_quantization_round_trip_and_validation() {
    const std::array<float, 6> values{-2.0F, -1.0F, 0.0F, 1.0F, 2.0F, 0.5F};
    const auto quantized = quantize_per_row(values, 2U, 3U);
    assert(quantized.has_value());
    assert(quantized.value_if()->scales.size() == 2U);
    assert(quantized.value_if()->values.size() == values.size());
    const auto restored = dequantize_per_row(*quantized.value_if());
    assert(restored.has_value());
    for (std::size_t i = 0; i < values.size(); ++i) {
        assert(std::abs((*restored.value_if())[i] - values[i]) <=
               quantized.value_if()->scales[i / 3U] * 0.51F);
    }
    const std::array<float, 2> invalid{
        1.0F, std::numeric_limits<float>::quiet_NaN()};
    assert(!quantize_per_row(invalid, 1U, 2U).has_value());
    assert(!quantize_per_row(values, 3U, 3U).has_value());

    const std::array<float, 3> zeros{0.0F, 0.0F, 0.0F};
    const auto zero_tensor = quantize_per_row(zeros, 1U, 3U);
    assert(zero_tensor.has_value());
    assert(zero_tensor.value_if()->scales[0] == 1.0F);
}

void test_artifact_round_trip_and_corruption() {
    auto artifact = valid_fp32_artifact();
    const auto serialized = serialize_model_artifact(artifact);
    assert(serialized.has_value());
    assert(serialized.value_if()->size() ==
           kModelHeaderBytes + artifact.payload.size());
    const auto loaded = deserialize_model_artifact(*serialized.value_if());
    assert(loaded.has_value());
    assert(loaded.value_if()->metadata.parameter_count == 100000U);
    assert(loaded.value_if()->payload == artifact.payload);

    auto corrupted = *serialized.value_if();
    corrupted.back() ^= std::byte{0x01};
    assert(!deserialize_model_artifact(corrupted).has_value());

    auto invalid_metadata = artifact;
    invalid_metadata.metadata.architecture_id = 0x0401U;
    assert(!serialize_model_artifact(invalid_metadata).has_value());
    invalid_metadata = artifact;
    invalid_metadata.metadata.embedding_dim = 61U;
    assert(!serialize_model_artifact(invalid_metadata).has_value());
    invalid_metadata = artifact;
    invalid_metadata.metadata.hidden_dim_second = 69U;
    assert(!serialize_model_artifact(invalid_metadata).has_value());

    auto corrupted_header = *serialized.value_if();
    corrupted_header[20] ^= std::byte{0x01};
    assert(!deserialize_model_artifact(corrupted_header).has_value());

    std::array<std::byte, 12> truncated{};
    assert(!deserialize_model_artifact(truncated).has_value());
}

void test_atomic_artifact_file_round_trip() {
    const auto path = std::filesystem::temp_directory_path() /
        "morok_g3_inference_test.morok";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    const auto artifact = valid_fp32_artifact();
    assert(save_model_artifact_atomic(path, artifact));
    const auto loaded = read_model_artifact(path);
    assert(loaded.has_value());
    assert(loaded.value_if()->payload == artifact.payload);
    std::filesystem::remove(path, ignored);
}

void test_int8_artifact_layout() {
    ModelArtifact artifact;
    artifact.metadata.precision = PrecisionMode::int8_symmetric_per_row;
    artifact.metadata.state_size_bytes = 660U;
    artifact.metadata.parameter_count = 100000U;
    artifact.metadata.scale_count = 1U;
    artifact.payload.resize(100000U + sizeof(float), std::byte{0});
    const auto serialized = serialize_model_artifact(artifact);
    assert(serialized.has_value());
    const auto loaded = deserialize_model_artifact(*serialized.value_if());
    assert(loaded.has_value());
    assert(loaded.value_if()->metadata.precision ==
           PrecisionMode::int8_symmetric_per_row);
}

void test_generation_and_control_tokens() {
    FixtureBackend backend;
    alignas(8) std::array<std::byte, sizeof(std::uint32_t)> state{};
    GenerationConfig config;
    config.temperature = 1.0;
    config.top_k = 0U;
    config.max_output_bytes = 8U;
    config.text_codec.normalization = lang::UnicodeNormalization::none;
    std::mt19937_64 random(7U);
    const auto generated = generate(backend, state, "x", config, random);
    assert(generated.has_value());
    assert(generated.value_if()->text == "A");
    assert(generated.value_if()->stopped_on_eos);
    assert(generated.value_if()->output_bytes == 1U);

    FixtureBackend empty_backend(true);
    config.empty_response = EmptyResponsePolicy::allow;
    const auto empty = generate(empty_backend, state, "x", config, random);
    assert(empty.has_value());
    assert(empty.value_if()->text.empty());
    config.empty_response = EmptyResponsePolicy::reject;
    assert(!generate(empty_backend, state, "x", config, random).has_value());

    config.max_input_bytes = 1U;
    assert(!generate(backend, state, "long", config, random).has_value());
}

void test_cancellation() {
    FixtureBackend backend;
    alignas(8) std::array<std::byte, sizeof(std::uint32_t)> state{};
    GenerationConfig config;
    config.text_codec.normalization = lang::UnicodeNormalization::none;
    std::mt19937_64 random(9U);
    const auto cancelled = generate(
        backend, state, "x", config, random, [] { return true; });
    assert(!cancelled.has_value());
    assert(cancelled.error_if()->code == common::ErrorCode::cancelled);
}

void test_timeout() {
    FixtureBackend backend(false, std::chrono::milliseconds{3});
    alignas(8) std::array<std::byte, sizeof(std::uint32_t)> state{};
    GenerationConfig config;
    config.timeout = std::chrono::milliseconds{1};
    config.text_codec.normalization = lang::UnicodeNormalization::none;
    std::mt19937_64 random(10U);
    const auto result = generate(backend, state, "x", config, random);
    assert(!result.has_value());
    assert(result.error_if()->code == common::ErrorCode::tool_timeout);
}

void test_concurrent_session_isolation() {
    FixtureBackend backend;
    constexpr std::size_t workers = 8U;
    std::array<std::string, workers> outputs{};
    std::array<std::thread, workers> threads;
    std::array<std::atomic<bool>, workers> succeeded{};
    for (std::size_t index = 0; index < workers; ++index) {
        threads[index] = std::thread([&, index] {
            alignas(8) std::array<std::byte, sizeof(std::uint32_t)> state{};
            GenerationConfig config;
            config.text_codec.normalization = lang::UnicodeNormalization::none;
            std::mt19937_64 random(100U + index);
            const auto result = generate(backend, state, "x", config, random);
            if (result) {
                outputs[index] = result.value_if()->text;
                succeeded[index].store(true, std::memory_order_release);
            }
        });
    }
    for (auto& thread : threads) thread.join();
    for (std::size_t index = 0; index < workers; ++index) {
        assert(succeeded[index].load(std::memory_order_acquire));
        assert(outputs[index] == "A");
    }
}

void test_benchmark_harness() {
    FixtureBackend backend;
    alignas(8) std::array<std::byte, sizeof(std::uint32_t)> state{};
    GenerationConfig generation;
    generation.text_codec.normalization = lang::UnicodeNormalization::none;
    BenchmarkConfig benchmark;
    benchmark.warmup_runs = 1U;
    benchmark.measured_runs = 3U;
    std::mt19937_64 random(77U);
    const auto report = benchmark_inference(
        backend, state, "x", generation, benchmark, random);
    assert(report.has_value());
    assert(report.value_if()->measured_runs == 3U);
    assert(report.value_if()->state_bytes_per_session == sizeof(std::uint32_t));
    assert(report.value_if()->latency_samples_ns.size() == 3U);
    assert(report.value_if()->median_latency_ns > 0U);
    assert(report.value_if()->p95_latency_ns > 0U);
}

} // namespace

int main() {
    test_quantization_round_trip_and_validation();
    test_artifact_round_trip_and_corruption();
    test_atomic_artifact_file_round_trip();
    test_int8_artifact_layout();
    test_generation_and_control_tokens();
    test_cancellation();
    test_timeout();
    test_concurrent_session_isolation();
    test_benchmark_harness();
    return 0;
}
