#include "morok/model/learn/g1_generation.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace morok::model {
namespace {

struct Utf8State {
    unsigned remaining{0};
    std::uint32_t codepoint{0};
    std::uint32_t minimum{0};
    std::uint8_t next_min{0x80};
    std::uint8_t next_max{0xBF};
};

bool advance_utf8(Utf8State& state, std::uint8_t byte) noexcept {
    if (state.remaining == 0) {
        if (byte <= 0x7F) return true;
        if (byte >= 0xC2 && byte <= 0xDF) {
            state.remaining = 1; state.codepoint = byte & 0x1FU; state.minimum = 0x80;
            state.next_min = 0x80; state.next_max = 0xBF; return true;
        }
        if (byte >= 0xE0 && byte <= 0xEF) {
            state.remaining = 2; state.codepoint = byte & 0x0FU; state.minimum = 0x800;
            state.next_min = byte == 0xE0 ? 0xA0 : 0x80;
            state.next_max = byte == 0xED ? 0x9F : 0xBF; return true;
        }
        if (byte >= 0xF0 && byte <= 0xF4) {
            state.remaining = 3; state.codepoint = byte & 0x07U; state.minimum = 0x10000;
            state.next_min = byte == 0xF0 ? 0x90 : 0x80;
            state.next_max = byte == 0xF4 ? 0x8F : 0xBF; return true;
        }
        return false;
    }
    if (byte < state.next_min || byte > state.next_max) return false;
    state.codepoint = (state.codepoint << 6U) | (byte & 0x3FU);
    --state.remaining;
    state.next_min = 0x80;
    state.next_max = 0xBF;
    if (state.remaining == 0) {
        if (state.codepoint < state.minimum ||
            (state.codepoint >= 0xD800 && state.codepoint <= 0xDFFF) ||
            state.codepoint > 0x10FFFF) return false;
        state.codepoint = 0;
        state.minimum = 0;
    }
    return true;
}

} // namespace

G1GenerationReport generate_g1(const G1Parameters& parameters,
                               const common::Utf8Text& prompt,
                               const G1GenerationConfig& config) {
    if (config.max_new_bytes == 0 || config.max_new_bytes > 65536)
        throw std::invalid_argument("max_new_bytes precisa estar entre 1 e 65536");

    G1State recurrent{};
    for (const unsigned char byte : prompt.string())
        advance_g1_state(parameters, recurrent, static_cast<std::uint8_t>(byte));

    std::string generated;
    generated.reserve(config.max_new_bytes);
    std::size_t evaluations = 0;
    Utf8State utf8{};

    for (std::size_t position = 0; position < config.max_new_bytes; ++position) {
        const std::size_t remaining_after = config.max_new_bytes - position - 1;
        double best_loss = std::numeric_limits<double>::infinity();
        int best_byte = -1;
        Utf8State best_utf8{};

        for (unsigned int candidate = 0; candidate <= 255; ++candidate) {
            Utf8State candidate_state = utf8;
            if (!advance_utf8(candidate_state, static_cast<std::uint8_t>(candidate)))
                continue;
            if (candidate_state.remaining > remaining_after)
                continue;

            const auto score = evaluate_g1_next_byte(
                parameters, recurrent, static_cast<std::uint8_t>(candidate));
            ++evaluations;
            if (!std::isfinite(score.negative_log_likelihood_nats))
                throw std::domain_error("G1 produziu perda não finita durante geração");
            if (score.negative_log_likelihood_nats < best_loss) {
                best_loss = score.negative_log_likelihood_nats;
                best_byte = static_cast<int>(candidate);
                best_utf8 = candidate_state;
            }
        }

        if (best_byte < 0) break;
        const auto byte = static_cast<std::uint8_t>(best_byte);
        generated.push_back(static_cast<char>(byte));
        utf8 = best_utf8;
        advance_g1_state(parameters, recurrent, byte);
    }

    auto valid = common::Utf8Text::from_utf8(generated);
    if (!valid) throw std::logic_error("decoder UTF-8 gerou uma sequência inválida");
    G1GenerationReport report{std::move(*valid.value_if()), evaluations, generated.size(),
                              generated.size() == config.max_new_bytes};
    return report;
}

} // namespace morok::model
