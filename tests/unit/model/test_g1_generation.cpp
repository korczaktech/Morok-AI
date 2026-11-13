#include "morok/model/learn/g1_generation.hpp"
#include "morok/model/parameters/g1_parameters.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void test_empty_prompt_and_bounded_valid_output() {
    using namespace morok;
    model::G1Parameters parameters{};
    common::Utf8Text prompt = std::move(common::Utf8Text::from_utf8("").value());
    const auto report = model::generate_g1(parameters, prompt, {32});
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
    auto prompt = std::move(common::Utf8Text::from_utf8("Olá, Morok!").value());
    const auto first = model::generate_g1(parameters, prompt, {24});
    const auto second = model::generate_g1(parameters, prompt, {24});
    require(first.continuation.string() == second.continuation.string(),
            "greedy generation must be deterministic");
    require(first.generated_bytes <= 24, "generation cannot exceed its limit");
}
void test_multibyte_utf8_is_never_truncated() {
    using namespace morok;
    model::G1Parameters parameters{};
    auto prompt = std::move(common::Utf8Text::from_utf8("ação").value());
    const auto report = model::generate_g1(parameters, prompt, {1});
    require(report.generated_bytes == 1, "one-byte generation must remain possible");
    require(common::Utf8Text::from_utf8(report.continuation.string()).has_value(),
            "one-byte generation must remain valid UTF-8");
}
void test_invalid_limits_rejected() {
    using namespace morok;
    model::G1Parameters parameters{};
    auto prompt = std::move(common::Utf8Text::from_utf8("x").value());
    bool rejected = false;
    try { static_cast<void>(model::generate_g1(parameters, prompt, {0})); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "zero byte limit must be rejected");
}
}
int main() {
    try {
        test_empty_prompt_and_bounded_valid_output();
        test_prompt_is_context_and_generation_is_deterministic();
        test_multibyte_utf8_is_never_truncated();
        test_invalid_limits_rejected();
    } catch (const std::exception& error) {
        std::cerr << "G1 generation test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
