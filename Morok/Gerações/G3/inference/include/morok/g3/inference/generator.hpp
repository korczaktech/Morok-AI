#pragma once

#include "morok/common/result.hpp"
#include "morok/lang/byte_tokenizer.hpp"
#include "morok/model/model_backend.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace morok::g3::inference {

enum class EmptyResponsePolicy {
    allow,
    reject
};

struct GenerationConfig final {
    double temperature{0.7};
    std::size_t top_k{10}; // zero disables Top-K
    std::size_t max_input_bytes{4096};
    std::size_t max_output_bytes{512};
    std::chrono::milliseconds timeout{5000}; // zero disables timeout
    EmptyResponsePolicy empty_response{EmptyResponsePolicy::reject};
    std::size_t repetition_ngram_min{3};
    std::size_t repetition_ngram_max{6};
    bool reset_session_state{true};
    lang::TextCodecConfig text_codec{};
};

struct GenerationResult final {
    std::string text;
    std::vector<lang::TokenId> generated_tokens;
    std::size_t input_bytes{0};
    std::size_t output_bytes{0};
    bool stopped_on_eos{false};
    bool reached_output_limit{false};
    bool repetition_detected{false};
    bool replaced_invalid_utf8{false};
    std::chrono::nanoseconds elapsed{0};
};

[[nodiscard]] common::Result<GenerationResult> generate(
    const model::ModelBackend& backend,
    std::span<std::byte> session_storage,
    std::string_view input,
    const GenerationConfig& config,
    std::mt19937_64& random,
    const std::function<bool()>& cancellation_requested = {});

} // namespace morok::g3::inference
