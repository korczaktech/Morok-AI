#include "morok/model/learn/g1_generation.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/parameters/g1_parameters.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void test_empty_prompt_and_bounded_valid_output() {
    using namespace morok;
    model::G1Parameters parameters{};
    auto empty_result = common::Utf8Text::from_utf8("");
    require(empty_result.has_value(), "empty prompt is valid UTF-8");
    common::Utf8Text prompt = std::move(*empty_result.value_if());
    model::G1GenerationConfig config;
    config.max_new_bytes = 32;
    config.eos_threshold = 0.99; // Verify the hard byte cap independently of learned EOS.
    const auto report = model::generate_g1(parameters, prompt, config);
    require(report.generated_bytes == 32, "generation must respect byte limit");
    require(report.continuation.size_bytes() == 32, "reported UTF-8 bytes must match");
    require(report.candidate_evaluations >= 32, "valid candidates must be scored");
    require(common::Utf8Text::from_utf8(report.continuation.string()).has_value(),
            "generated output must be strict UTF-8");
}

void test_prompt_is_context_and_generation_is_deterministic() {
    using namespace morok;
    model::G1Parameters parameters{};
    model::initialize_g1_parameters(parameters, 42);
    auto prompt_result = common::Utf8Text::from_utf8("Olá, Morok!");
    require(prompt_result.has_value(), "UTF-8 prompt must validate");
    auto prompt = std::move(*prompt_result.value_if());
    model::G1GenerationConfig config;
    config.max_new_bytes = 24;
    config.eos_threshold = 0.99;
    const auto first = model::generate_g1(parameters, prompt, config);
    const auto second = model::generate_g1(parameters, prompt, config);
    require(first.continuation.string() == second.continuation.string(),
            "greedy generation must be deterministic");
    require(first.generated_bytes <= 24, "generation cannot exceed its limit");
}

void test_multibyte_utf8_is_never_truncated() {
    using namespace morok;
    model::G1Parameters parameters{};
    auto prompt_result = common::Utf8Text::from_utf8("ação");
    require(prompt_result.has_value(), "multibyte UTF-8 prompt must validate");
    auto prompt = std::move(*prompt_result.value_if());
    model::G1GenerationConfig config;
    config.max_new_bytes = 1;
    config.eos_threshold = 0.99;
    const auto report = model::generate_g1(parameters, prompt, config);
    require(report.generated_bytes == 1, "one-byte generation must remain possible");
    require(common::Utf8Text::from_utf8(report.continuation.string()).has_value(),
            "one-byte generation must remain valid UTF-8");
}

void test_inference_checkpoint_round_trip() {
    using namespace morok;
    const auto path = std::filesystem::temp_directory_path() / "morok-g1-generation-inference.ckpt";
    model::G1Checkpoint saved;
    model::initialize_g1_parameters(saved.parameters, 777);
    saved.metadata = "kind=inference";
    model::save_g1_checkpoint(path, saved);
    const auto loaded = model::load_g1_checkpoint(path);
    require(!loaded.has_optimizer, "inference checkpoint must not require optimizer state");
    require(loaded.parameters.flatten() == saved.parameters.flatten(),
            "inference checkpoint must preserve all G1 parameters");
    auto prompt_result = common::Utf8Text::from_utf8("Morok");
    require(prompt_result.has_value(), "integration prompt must be valid");
    const auto generated = model::generate_g1(
        loaded.parameters, *prompt_result.value_if(), [] {
            model::G1GenerationConfig config;
            config.max_new_bytes = 12;
            config.eos_threshold = 0.99;
            return config;
        }());
    require(common::Utf8Text::from_utf8(generated.continuation.string()).has_value(),
            "checkpoint-backed generation must return valid UTF-8");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void test_sampling_is_seeded_and_top_k_one_is_greedy() {
    using namespace morok;
    model::G1Parameters parameters{};
    auto prompt_result = common::Utf8Text::from_utf8("Morok");
    require(prompt_result.has_value(), "sampling prompt must be valid UTF-8");
    const auto& prompt = *prompt_result.value_if();

    model::G1GenerationConfig sampled;
    sampled.max_new_bytes = 48;
    sampled.temperature = 0.8;
    sampled.seed = 1234;
    const auto first = model::generate_g1(parameters, prompt, sampled);
    const auto second = model::generate_g1(parameters, prompt, sampled);
    require(first.continuation.string() == second.continuation.string(),
            "sampling must be reproducible for the same seed");

    sampled.seed = 9876;
    const auto third = model::generate_g1(parameters, prompt, sampled);
    require(first.continuation.string() != third.continuation.string(),
            "different seeds should produce different samples for a uniform model");

    model::G1GenerationConfig greedy;
    greedy.max_new_bytes = 48;
    const auto baseline = model::generate_g1(parameters, prompt, greedy);
    sampled.seed = 1234;
    sampled.top_k = 1;
    const auto top_one = model::generate_g1(parameters, prompt, sampled);
    require(top_one.continuation.string() == baseline.continuation.string(),
            "top-k one must reduce sampling to greedy selection");
}

void test_learned_eos_stops_generation() {
    using namespace morok;
    model::G1Parameters parameters{};
    parameters.theta_len.fill(20.0F);
    auto prompt_result = common::Utf8Text::from_utf8("Pergunta: teste");
    require(prompt_result.has_value(), "EOS prompt must be valid UTF-8");
    model::G1GenerationConfig config;
    config.max_new_bytes = 32;
    config.min_new_bytes = 1;
    config.eos_threshold = 0.5;
    const auto report = model::generate_g1(parameters, *prompt_result.value_if(), config);
    require(report.stopped_by_eos, "high learned stop bias must terminate generation");
    require(!report.reached_byte_limit && report.generated_bytes == 1,
            "EOS termination must be distinct from reaching the hard byte cap");
}

void test_invalid_limits_rejected() {
    using namespace morok;
    model::G1Parameters parameters{};
    auto prompt_result = common::Utf8Text::from_utf8("x");
    require(prompt_result.has_value(), "ASCII prompt must validate");
    auto prompt = std::move(*prompt_result.value_if());
    bool rejected = false;
    try {
        static_cast<void>(model::generate_g1(parameters, prompt, {0}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "zero byte limit must be rejected");
}
} // namespace

int main() {
    try {
        test_empty_prompt_and_bounded_valid_output();
        test_prompt_is_context_and_generation_is_deterministic();
        test_multibyte_utf8_is_never_truncated();
        test_inference_checkpoint_round_trip();
        test_sampling_is_seeded_and_top_k_one_is_greedy();
        test_learned_eos_stops_generation();
        test_invalid_limits_rejected();
    } catch (const std::exception& error) {
        std::cerr << "G1 generation test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
