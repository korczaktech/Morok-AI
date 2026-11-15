#include "morok/model/learn/g1_generation.hpp"
#include "morok/lang/rlah/segmenter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    if (!std::isfinite(config.temperature) || config.temperature < 0.0 ||
        !std::isfinite(config.repetition_penalty) || config.repetition_penalty < 0.0 ||
        !std::isfinite(config.eos_threshold) || config.eos_threshold <= 0.0 ||
        !std::isfinite(config.rlah_bias_strength) || config.rlah_bias_strength < 0.0 ||
        config.rlah_bias_strength > 2.0 ||
        config.eos_threshold >= 1.0 || config.min_new_bytes > config.max_new_bytes) {
        throw std::invalid_argument("configuração de amostragem inválida");
    }

    G1State recurrent{};
    for (const char raw_byte : prompt.string()) {
        const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(raw_byte));
        advance_g1_state(parameters, recurrent, byte);
    }

    std::string generated;
    generated.reserve(config.max_new_bytes);
    std::size_t evaluations = 0;
    Utf8State utf8{};
    std::mt19937_64 random(config.seed);
    morok::lang::rlah::Config rlah_config{};
    rlah_config.min_frequency = 2;
    rlah_config.max_candidates = 256;
    rlah_config.max_active = 64;
    rlah_config.max_dictionary = 64;
    rlah_config.create = 0.55F;
    rlah_config.keep = 0.45F;
    morok::lang::rlah::Segmenter rlah_segmenter(rlah_config);

    bool stopped_by_eos = false;
    for (std::size_t position = 0; position < config.max_new_bytes; ++position) {
        if (generated.size() >= config.min_new_bytes && utf8.remaining == 0) {
            std::size_t bucket = 0;
            std::size_t length = prompt.size_bytes() + generated.size();
            while (length > 1 && bucket < 6) { length >>= 1U; ++bucket; }
            double stop_logit = parameters.theta_len[bucket];
            for (std::size_t i = 0; i < kG1CombinedStateDimension; ++i) {
                const double activation = i < kG1FastStateDimension
                    ? recurrent.fast[i] : recurrent.slow[i - kG1FastStateDimension];
                stop_logit += static_cast<double>(parameters.v[i]) * activation;
            }
            const double stop_probability = stop_logit >= 0.0
                ? 1.0 / (1.0 + std::exp(-stop_logit))
                : std::exp(stop_logit) / (1.0 + std::exp(stop_logit));
            if (stop_probability >= config.eos_threshold) {
                stopped_by_eos = true;
                break;
            }
        }
        const std::size_t remaining_after = config.max_new_bytes - position - 1;
        struct Candidate {
            std::uint8_t byte;
            double loss;
            Utf8State utf8;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(256);

        std::array<double, 256> rlah_bias{};
        if (config.rlah_bias_strength > 0.0 && position % 8U == 0U) {
            std::vector<std::uint8_t> context;
            const auto prompt_bytes = prompt.string();
            constexpr std::size_t context_limit = 4096;
            const std::size_t prompt_begin = prompt_bytes.size() > context_limit
                ? prompt_bytes.size() - context_limit : 0U;
            context.reserve(std::min(context_limit, prompt_bytes.size() + generated.size()));
            for (std::size_t i = prompt_begin; i < prompt_bytes.size(); ++i)
                context.push_back(static_cast<std::uint8_t>(
                    static_cast<unsigned char>(prompt_bytes[i])));
            for (const unsigned char byte : generated) {
                context.push_back(byte);
                if (context.size() > context_limit) context.erase(context.begin());
            }
            if (context.size() >= 2U) {
                const auto segments = rlah_segmenter.evaluate(context);
                for (const auto& segment : segments) {
                    if (!segment.active || segment.bytes.size() < 2U ||
                        segment.bytes.size() > context.size() + 1U) continue;
                    const std::size_t prefix_size = segment.bytes.size() - 1U;
                    if (prefix_size == 0U || prefix_size > context.size()) continue;
                    if (std::equal(segment.bytes.begin(),
                                   segment.bytes.begin() + static_cast<std::ptrdiff_t>(prefix_size),
                                   context.end() - static_cast<std::ptrdiff_t>(prefix_size))) {
                        const auto next_byte = segment.bytes.back();
                        rlah_bias[next_byte] = std::max(
                            rlah_bias[next_byte],
                            static_cast<double>(std::clamp(segment.utility, 0.0F, 1.0F)));
                    }
                }
            }
        }

        for (unsigned int candidate = 0; candidate <= 255; ++candidate) {
            if (config.printable_only && utf8.remaining == 0 &&
                (candidate < 0x20U || candidate == 0x7FU))
                continue;
            Utf8State candidate_state = utf8;
            if (!advance_utf8(candidate_state, static_cast<std::uint8_t>(candidate)))
                continue;
            if (candidate_state.remaining > remaining_after)
                continue;

            const auto byte = static_cast<std::uint8_t>(candidate);
            const auto score = evaluate_g1_next_byte(parameters, recurrent, byte);
            ++evaluations;
            if (!std::isfinite(score.negative_log_likelihood_nats))
                throw std::domain_error("G1 produziu perda não finita durante geração");

            double adjusted_loss = score.negative_log_likelihood_nats;
            if (config.repetition_penalty > 0.0 && byte >= 0x20 && byte <= 0x7E) {
                const auto begin = generated.size() > config.repetition_window
                    ? generated.size() - config.repetition_window : 0U;
                for (std::size_t i = begin; i < generated.size(); ++i) {
                    if (static_cast<std::uint8_t>(static_cast<unsigned char>(generated[i])) == byte)
                        adjusted_loss += config.repetition_penalty;
                }
            }
            adjusted_loss -= config.rlah_bias_strength * rlah_bias[byte];
            candidates.push_back(Candidate{byte, adjusted_loss, candidate_state});
        }

        if (candidates.empty()) break;
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& left, const Candidate& right) {
                      if (left.loss != right.loss) return left.loss < right.loss;
                      return left.byte < right.byte;
                  });
        if (config.top_k > 0 && candidates.size() > config.top_k)
            candidates.resize(config.top_k);

        std::size_t selected = 0;
        if (config.temperature > 0.0 && candidates.size() > 1) {
            const double minimum_loss = candidates.front().loss;
            std::vector<double> weights;
            weights.reserve(candidates.size());
            for (const auto& candidate : candidates)
                weights.push_back(std::exp(-(candidate.loss - minimum_loss) / config.temperature));
            std::discrete_distribution<std::size_t> distribution(weights.begin(), weights.end());
            selected = distribution(random);
        }

        const auto byte = candidates[selected].byte;
        generated.push_back(static_cast<char>(byte));
        utf8 = candidates[selected].utf8;
        advance_g1_state(parameters, recurrent, byte);
    }

    auto valid = common::Utf8Text::from_utf8(generated);
    if (!valid) throw std::logic_error("decoder UTF-8 gerou uma sequência inválida");
    G1GenerationReport report{std::move(*valid.value_if()), evaluations, generated.size(),
                              generated.size() == config.max_new_bytes && !stopped_by_eos,
                              stopped_by_eos};
    return report;
}

} // namespace morok::model
