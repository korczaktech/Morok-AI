#include "morok/model/learn/g1_bptt.hpp"

#include "morok/model/math/primitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace morok::model {
namespace {

constexpr std::size_t D = kG1InputDimension;
constexpr std::size_t B = kG1PredictionBits;
constexpr std::size_t C = kG1CombinedStateDimension;

constexpr std::size_t O_WAX = 0;
constexpr std::size_t O_WAH = 121;
constexpr std::size_t O_WCX = 242;
constexpr std::size_t O_WCH = 363;
constexpr std::size_t O_BA = 484;
constexpr std::size_t O_BC = 495;
constexpr std::size_t O_WGH = 506;
constexpr std::size_t O_WGS = 627;
constexpr std::size_t O_BG = 748;
constexpr std::size_t O_HEAD = 759;
constexpr std::size_t O_HEAD_BIAS = 935;
constexpr std::size_t O_TRIANGULAR = 943;
constexpr std::size_t O_SEGMENTER = 971;

struct StepCache {
    std::array<double, D> input{};
    std::array<double, D> fast_previous{};
    std::array<double, D> slow_previous{};
    std::array<double, D> fast{};
    std::array<double, D> slow{};
    std::array<double, D> fast_gate{};
    std::array<double, D> fast_candidate{};
    std::array<double, D> slow_gate{};
    std::array<double, B> base_logits{};
    std::uint8_t target{0};
};

void require_finite_parameters(const G1Parameters& p) {
    const auto flat = p.flatten();
    for (const float value : flat) {
        if (!std::isfinite(value)) {
            throw std::domain_error("G1 parameters contain a non-finite value");
        }
    }
}

std::array<double, C> join(const std::array<double, D>& fast,
                           const std::array<double, D>& slow) {
    std::array<double, C> result{};
    for (std::size_t i = 0; i < D; ++i) {
        result[i] = fast[i];
        result[D + i] = slow[i];
    }
    return result;
}

StepCache forward_step(const G1Parameters& p, const G1State& state,
                       std::uint8_t observed, std::uint8_t target) {
    StepCache cache{};
    const auto encoded = encode_g1_byte(observed);
    for (std::size_t i = 0; i < D; ++i) {
        cache.input[i] = encoded[i];
        cache.fast_previous[i] = state.fast[i];
        cache.slow_previous[i] = state.slow[i];
    }
    cache.target = target;

    for (std::size_t row = 0; row < D; ++row) {
        double za = p.b_a[row];
        double zc = p.b_c[row];
        for (std::size_t col = 0; col < D; ++col) {
            const auto ix = row * D + col;
            za += static_cast<double>(p.W_ax[ix]) * cache.input[col];
            za += static_cast<double>(p.W_ah[ix]) * cache.fast_previous[col];
            zc += static_cast<double>(p.W_cx[ix]) * cache.input[col];
            zc += static_cast<double>(p.W_ch[ix]) * cache.fast_previous[col];
        }
        cache.fast_gate[row] = math::sigmoid(za);
        cache.fast_candidate[row] = math::tanh_activation(zc);
        cache.fast[row] = cache.fast_gate[row] * cache.fast_previous[row] +
            (1.0 - cache.fast_gate[row]) * cache.fast_candidate[row];
    }

    for (std::size_t row = 0; row < D; ++row) {
        double zg = p.b_g[row];
        for (std::size_t col = 0; col < D; ++col) {
            const auto ix = row * D + col;
            zg += static_cast<double>(p.W_gh[ix]) * cache.fast[col];
            zg += static_cast<double>(p.W_gs[ix]) * cache.slow_previous[col];
        }
        cache.slow_gate[row] = math::sigmoid(zg);
        cache.slow[row] = cache.slow_gate[row] * cache.slow_previous[row] +
            (1.0 - cache.slow_gate[row]) * cache.fast[row];
    }

    // Match the production forward pass: recurrent states are stored in FP32.
    for (std::size_t i = 0; i < D; ++i) {
        cache.fast[i] = static_cast<float>(cache.fast[i]);
        cache.slow[i] = static_cast<float>(cache.slow[i]);
    }

    const auto combined = join(cache.fast, cache.slow);
    for (std::size_t bit = 0; bit < B; ++bit) {
        double z = p.c[bit];
        for (std::size_t col = 0; col < C; ++col) {
            z += static_cast<double>(p.O[bit * C + col]) * combined[col];
        }
        cache.base_logits[bit] = z;
    }
    return cache;
}

double step_loss_and_head_backward(const G1Parameters& p, const StepCache& step,
                                   G1Gradients& grad,
                                   std::array<double, D>& d_fast,
                                   std::array<double, D>& d_slow) {
    const auto combined = join(step.fast, step.slow);
    double loss = 0.0;

    for (std::size_t bit = 0; bit < B; ++bit) {
        double logit = step.base_logits[bit];
        for (std::size_t previous = 0; previous < bit; ++previous) {
            const auto a_ix = bit * (bit - 1U) / 2U + previous;
            const bool one = (step.target & static_cast<std::uint8_t>(1U << (7U - previous))) != 0;
            const double signed_bit = one ? 1.0 : -1.0;
            logit += static_cast<double>(p.A[a_ix]) * signed_bit;
        }
        const bool target_one =
            (step.target & static_cast<std::uint8_t>(1U << (7U - bit))) != 0;
        const double y = target_one ? 1.0 : 0.0;
        const double probability = math::sigmoid(logit);
        const double dz = probability - y;
        loss += math::binary_cross_entropy_with_logits(logit, y);

        grad.values[O_HEAD_BIAS + bit] += dz;
        for (std::size_t col = 0; col < C; ++col) {
            const auto ix = bit * C + col;
            grad.values[O_HEAD + ix] += dz * combined[col];
            const double contribution = static_cast<double>(p.O[ix]) * dz;
            if (col < D) d_fast[col] += contribution;
            else d_slow[col - D] += contribution;
        }
        for (std::size_t previous = 0; previous < bit; ++previous) {
            const auto a_ix = bit * (bit - 1U) / 2U + previous;
            const bool one = (step.target & static_cast<std::uint8_t>(1U << (7U - previous))) != 0;
            grad.values[O_TRIANGULAR + a_ix] += dz * (one ? 1.0 : -1.0);
        }
    }
    return loss;
}

void backward_recurrent_step(const G1Parameters& p, const StepCache& step,
                             G1Gradients& grad,
                             const std::array<double, D>& d_fast_from_head,
                             const std::array<double, D>& d_slow_from_head,
                             std::array<double, D>& d_fast_previous,
                             std::array<double, D>& d_slow_previous) {
    std::array<double, D> d_fast = d_fast_from_head;
    std::array<double, D> d_slow = d_slow_from_head;
    std::array<double, D> d_fast_gate_pre{};
    std::array<double, D> d_fast_candidate_pre{};
    std::array<double, D> d_slow_gate_pre{};

    // Reverse the slow-state interpolation first.
    for (std::size_t row = 0; row < D; ++row) {
        const double g = step.slow_gate[row];
        const double h = step.fast[row];
        const double s_prev = step.slow_previous[row];
        const double dg = d_slow[row] * (s_prev - h);
        const double dz = dg * g * (1.0 - g);
        d_slow_previous[row] += d_slow[row] * g;
        d_fast[row] += d_slow[row] * (1.0 - g);
        d_slow_gate_pre[row] = dz;
        grad.values[O_BG + row] += dz;
    }
    for (std::size_t row = 0; row < D; ++row) {
        const double dz = d_slow_gate_pre[row];
        for (std::size_t col = 0; col < D; ++col) {
            const auto ix = row * D + col;
            grad.values[O_WGH + ix] += dz * step.fast[col];
            grad.values[O_WGS + ix] += dz * step.slow_previous[col];
            d_fast[col] += static_cast<double>(p.W_gh[ix]) * dz;
            d_slow_previous[col] += static_cast<double>(p.W_gs[ix]) * dz;
        }
    }

    // Reverse the fast-state interpolation and its two affine transforms.
    for (std::size_t row = 0; row < D; ++row) {
        const double a = step.fast_gate[row];
        const double candidate = step.fast_candidate[row];
        const double previous = step.fast_previous[row];
        const double da = d_fast[row] * (previous - candidate);
        const double dc = d_fast[row] * (1.0 - a);
        d_fast_previous[row] += d_fast[row] * a;
        d_fast_gate_pre[row] = da * a * (1.0 - a);
        d_fast_candidate_pre[row] = dc * (1.0 - candidate * candidate);
        grad.values[O_BA + row] += d_fast_gate_pre[row];
        grad.values[O_BC + row] += d_fast_candidate_pre[row];
    }

    for (std::size_t row = 0; row < D; ++row) {
        const double da = d_fast_gate_pre[row];
        const double dc = d_fast_candidate_pre[row];
        for (std::size_t col = 0; col < D; ++col) {
            const auto ix = row * D + col;
            grad.values[O_WAX + ix] += da * step.input[col];
            grad.values[O_WAH + ix] += da * step.fast_previous[col];
            grad.values[O_WCX + ix] += dc * step.input[col];
            grad.values[O_WCH + ix] += dc * step.fast_previous[col];
            d_fast_previous[col] += static_cast<double>(p.W_ah[ix]) * da;
            d_fast_previous[col] += static_cast<double>(p.W_ch[ix]) * dc;
        }
    }
}

} // namespace

double G1Gradients::global_norm() const noexcept {
    double norm = 0.0;
    for (const double value : values) norm = std::hypot(norm, value);
    return norm;
}

G1BpttResult compute_g1_bptt_gradients(const G1Parameters& parameters,
                                       std::span<const std::uint8_t> bytes,
                                       std::size_t truncation_length) {
    if (bytes.size() < 2) {
        throw std::invalid_argument("BPTT requires at least two bytes");
    }
    if (truncation_length == 0) {
        throw std::invalid_argument("BPTT truncation length must be positive");
    }
    require_finite_parameters(parameters);

    G1BpttResult result{};
    result.target_byte_count = bytes.size() - 1;
    G1State state{};

    // A block contains at most truncation_length target bytes. Its incoming
    // numeric state is carried forward, but no adjoint crosses its left edge.
    std::size_t first_target = 1;
    while (first_target < bytes.size()) {
        const std::size_t end_target =
            std::min(bytes.size(), first_target + truncation_length);
        std::vector<StepCache> tape;
        tape.reserve(end_target - first_target);

        for (std::size_t target_index = first_target;
             target_index < end_target; ++target_index) {
            auto step = forward_step(parameters, state, bytes[target_index - 1],
                                     bytes[target_index]);
            tape.push_back(step);
            for (std::size_t i = 0; i < D; ++i) {
                state.fast[i] = static_cast<float>(step.fast[i]);
                state.slow[i] = static_cast<float>(step.slow[i]);
            }
            // The state is now the state after consuming bytes[target_index-1].
            // The next step consumes the next observed byte.
        }

        result.final_state = state;
        std::array<double, D> d_fast_future{};
        std::array<double, D> d_slow_future{};
        for (std::size_t reverse = tape.size(); reverse-- > 0;) {
            const auto& step = tape[reverse];
            std::array<double, D> d_fast_head{};
            std::array<double, D> d_slow_head{};
            result.mean_loss_nats +=
                step_loss_and_head_backward(parameters, step, result.gradients,
                                            d_fast_head, d_slow_head);
            // Add the adjoint arriving from the next time step before reversing
            // this step's recurrent transition.
            for (std::size_t i = 0; i < D; ++i) {
                d_fast_head[i] += d_fast_future[i];
                d_slow_head[i] += d_slow_future[i];
            }
            std::array<double, D> d_fast_previous{};
            std::array<double, D> d_slow_previous{};
            backward_recurrent_step(parameters, step, result.gradients,
                                    d_fast_head, d_slow_head,
                                    d_fast_previous, d_slow_previous);
            d_fast_future = d_fast_previous;
            d_slow_future = d_slow_previous;
        }
        // The future adjoints are intentionally discarded at the block edge:
        // the numeric state is carried, but the previous block's graph is detached.
        first_target = end_target;
    }

    const double denominator = static_cast<double>(result.target_byte_count);
    result.mean_loss_nats /= denominator;
    for (double& value : result.gradients.values) value /= denominator;

    if (!std::isfinite(result.mean_loss_nats) ||
        !std::isfinite(result.gradients.global_norm())) {
        throw std::domain_error("BPTT produced a non-finite loss or gradient");
    }
    for (std::size_t i = O_SEGMENTER; i < kG1ParameterCount; ++i) {
        result.gradients.values[i] = 0.0;
    }
    return result;
}

} // namespace morok::model
