#include "morok/common/utf8_text.hpp"
#include "morok/lang/rlah/segmenter.hpp"
#include "morok/model/g1_forward.hpp"
#include "morok/model/parameters/g1_parameters.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

int main() {
    using namespace morok;

    // Property 1: flatten/assign is a lossless canonical parameter mapping.
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        model::G1Parameters original{};
        model::initialize_g1_parameters(original, seed);
        const auto flattened = original.flatten();
        model::G1Parameters restored{};
        assert(restored.assign(flattened));
        assert(restored.flatten() == flattened);
    }

    // Property 2: all single-byte UTF-8 inputs have the expected validity.
    for (unsigned int value = 0; value <= 255; ++value) {
        const char byte = static_cast<char>(value);
        const auto parsed = common::Utf8Text::from_utf8(
            std::string_view(&byte, 1));
        assert(parsed.has_value() == (value <= 0x7f));
    }

    // Property 3: byte features are bounded and encode all eight bits MSB-first.
    for (unsigned int value = 0; value <= 255; ++value) {
        const auto features = model::encode_g1_byte(
            static_cast<std::uint8_t>(value));
        for (std::size_t bit = 0; bit < 8; ++bit) {
            const bool set = (value & (1U << (7U - bit))) != 0;
            assert(features[bit] == (set ? 1.0F : -1.0F));
        }
        for (const float feature : features) {
            assert(std::isfinite(feature));
            assert(feature >= -1.0F && feature <= 1.0F);
        }
    }

    // Property 4: recurrent updates and predictions remain finite for every byte.
    model::G1Parameters parameters{};
    model::initialize_g1_parameters(parameters, 123456);
    model::G1State state{};
    for (unsigned int value = 0; value <= 255; ++value) {
        model::advance_g1_state(parameters, state,
                                static_cast<std::uint8_t>(value));
        const auto prediction = model::predict_g1_next_byte(parameters, state);
        for (const auto probability : prediction.bit_probabilities) {
            assert(std::isfinite(probability));
            assert(probability >= 0.0 && probability <= 1.0);
        }
        const auto loss = model::evaluate_g1_next_byte(
            parameters, state, static_cast<std::uint8_t>(value));
        assert(std::isfinite(loss.negative_log_likelihood_nats));
        assert(loss.negative_log_likelihood_nats >= 0.0);
    }

    // Property 5: RLAH encode/decode is lossless over deterministic random data.
    std::mt19937_64 random(0x4d4f524f4bULL);
    for (std::size_t sample = 0; sample < 128; ++sample) {
        const std::size_t length = static_cast<std::size_t>(random() % 2048U);
        std::vector<std::uint8_t> input(length);
        for (auto& byte : input) byte = static_cast<std::uint8_t>(random());
        const auto encoded = lang::rlah::encode(input, {});
        std::vector<std::uint8_t> decoded;
        std::string error;
        assert(lang::rlah::decode(encoded.data, decoded, error));
        assert(decoded == input);
    }

    std::cout << "Morok property tests passed\n";
    return 0;
}
