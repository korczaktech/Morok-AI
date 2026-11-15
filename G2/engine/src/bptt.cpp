#include "morok/g2/engine/bptt.hpp"

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

constexpr std::size_t D = kG2ExpertInputDimension;
constexpr std::size_t B = kG2ExpertPredictionBits;
constexpr std::size_t C = kG2ExpertCombinedStateDimension;

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
constexpr std::size_t O_EOS_WEIGHTS = 971;
constexpr std::size_t O_EOS_BIASES = 993;

static_assert(O_EOS_WEIGHTS + kG2ExpertCombinedStateDimension == O_EOS_BIASES);
static_assert(O_EOS_BIASES + 7 == kG2ExpertParameterCount);

struct StepCache {
    std::array<double, D> input{};
    std::array<double, D> fast_previous{};
    std::array<double, D> slow_previous{};
    std::array<double, D> fast{};
    std::array<double, D> fast_unrounded{};
    std::array<double, D> slow{};
    std::array<double, D> fast_gate{};
    std::array<double, D> fast_candidate{};
    std::array<double, D> slow_gate{};
    std::array<double, B> base_logits{};
    std::uint8_t target{0};
    std::size_t context_length{0};
};

std::size_t eos_length_bucket(std::size_t length) noexcept {
    std::size_t bucket = 0;
    while (length > 1 && bucket < 6) {
        length >>= 1U;
        ++bucket;
    }
    return bucket;
}

void require_finite_parameters(const G2ExpertParameters& p) {
    const auto flat = p.flatten();
    for (const float value : flat) {
        if (!std::isfinite(value)) {
            throw std::domain_error("G2Expert parameters contain a non-finite value");
        }
    }
}

void require_finite_state(const G2ExpertState& state) {
    for (const float value : state.fast) {
        if (!std::isfinite(value)) {
            throw std::domain_error("BPTT initial fast state contains a non-finite value");
        }
    }
    for (const float value : state.slow) {
        if (!std::isfinite(value)) {
            throw std::domain_error("BPTT initial slow state contains a non-finite value");
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

StepCache forward_step(const G2ExpertParameters& p, const G2ExpertState& state,
                       std::uint8_t observed, std::uint8_t target, std::size_t context_length) {
    StepCache cache{};
    const auto encoded = encode_g2_expert_byte(observed);
    for (std::size_t i = 0; i < D; ++i) {
        cache.input[i] = encoded[i];
        cache.fast_previous[i] = state.fast[i];
        cache.slow_previous[i] = state.slow[i];
    }
    cache.target = target;
    cache.context_length = context_length;

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

    cache.fast_unrounded = cache.fast;

    for (std::size_t row = 0; row < D; ++row) {
        double zg = p.b_g[row];
        for (std::size_t col = 0; col < D; ++col) {
            const auto ix = row * D + col;
            zg += static_cast<double>(p.W_gh[ix]) * cache.fast_unrounded[col];
            zg += static_cast<double>(p.W_gs[ix]) * cache.slow_previous[col];
        }
        cache.slow_gate[row] = math::sigmoid(zg);
        cache.slow[row] = cache.slow_gate[row] * cache.slow_previous[row] +
            (1.0 - cache.slow_gate[row]) * cache.fast_unrounded[row];
    }

    // Match production storage: recurrent states are rounded to FP32 after both
    // transitions. The backward pass uses a straight-through derivative.
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

double step_loss_and_head_backward(const G2ExpertParameters& p, const StepCache& step,
                                   G2ExpertGradients& grad,
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
    const auto bucket = eos_length_bucket(step.context_length);
    double eos_logit = p.theta_len[bucket];
    for (std::size_t col = 0; col < C; ++col)
        eos_logit += static_cast<double>(p.v[col]) * combined[col];
    const double eos_probability = math::sigmoid(eos_logit);
    const double eos_dz = eos_probability; // A following byte means "continue".
    grad.values[O_EOS_BIASES + bucket] += eos_dz;
    for (std::size_t col = 0; col < C; ++col)
        grad.values[O_EOS_WEIGHTS + col] += eos_dz * combined[col];
    return loss;
}

void backward_recurrent_step(const G2ExpertParameters& p, const StepCache& step,
                             G2ExpertGradients& grad,
                             const std::array<double, D>& d_fast_from_head,
                             const std::array<double, D>& d_slow_from_head,
                             std::array<double, D>& d_fast_previous,
                             std::array<double, D>& d_slow_previous) {
    std::array<double, D> d_fast = d_fast_from_head;
    std::array<double, D> d_slow = d_slow_from_head;
    std::array<double, D> d_fast_gate_pre{};
    std::array<double, D> d_fast_candidate_pre{};
    std::array<double, D> d_slow_gate_pre{};

    for (std::size_t row = 0; row < D; ++row) {
        const double g = step.slow_gate[row];
        const double h = step.fast_unrounded[row];
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
            grad.values[O_WGH + ix] += dz * step.fast_unrounded[col];
            grad.values[O_WGS + ix] += dz * step.slow_previous[col];
            d_fast[col] += static_cast<double>(p.W_gh[ix]) * dz;
            d_slow_previous[col] += static_cast<double>(p.W_gs[ix]) * dz;
        }
    }

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

double G2ExpertGradients::global_norm() const noexcept {
    double norm = 0.0;
    for (const double value : values) norm = std::hypot(norm, value);
    return norm;
}

G2ExpertBpttResult compute_g2_expert_bptt_gradients(const G2ExpertParameters& parameters,
                                       std::span<const std::uint8_t> bytes,
                                       std::size_t truncation_length,
                                       const G2ExpertState& initial_state,
                                       std::size_t context_offset) {
    if (bytes.size() < 2) {
        throw std::invalid_argument("BPTT requires at least two bytes");
    }
    if (truncation_length == 0) {
        throw std::invalid_argument("BPTT truncation length must be positive");
    }
    if (truncation_length > kG2ExpertMaxBpttTruncationLength) {
        throw std::invalid_argument("BPTT truncation length exceeds the 128-target memory bound");
    }
    require_finite_parameters(parameters);
    require_finite_state(initial_state);

    G2ExpertBpttResult result{};
    result.target_byte_count = bytes.size() - 1;
    G2ExpertState state = initial_state;

    std::size_t first_target = 1;
    while (first_target < bytes.size()) {
        // Subtract before adding: this avoids size_t overflow for an extreme
        // truncation_length supplied by an API caller.
        const std::size_t remaining = bytes.size() - first_target;
        const std::size_t block_targets = std::min(truncation_length, remaining);
        const std::size_t end_target = first_target + block_targets;

        std::vector<StepCache> tape;
        tape.reserve(block_targets);

        for (std::size_t target_index = first_target;
             target_index < end_target; ++target_index) {
            if (context_offset > std::numeric_limits<std::size_t>::max() - target_index)
                throw std::overflow_error("BPTT context length overflow");
            auto step = forward_step(parameters, state, bytes[target_index - 1],
                                     bytes[target_index], context_offset + target_index);
            tape.push_back(step);
            for (std::size_t i = 0; i < D; ++i) {
                state.fast[i] = static_cast<float>(step.fast[i]);
                state.slow[i] = static_cast<float>(step.slow[i]);
            }
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
        first_target = end_target;
    }

    const double denominator = static_cast<double>(result.target_byte_count);
    result.mean_loss_nats /= denominator;
    for (double& value : result.gradients.values) value /= denominator;

    if (!std::isfinite(result.mean_loss_nats) ||
        !std::isfinite(result.gradients.global_norm())) {
        throw std::domain_error("BPTT produced a non-finite loss or gradient");
    }
    return result;
}

G2ExpertEosResult compute_g2_expert_eos_gradients(const G2ExpertParameters& parameters,
                                     const G2ExpertState& state,
                                     std::size_t context_length,
                                     bool should_stop) {
    require_finite_parameters(parameters);
    require_finite_state(state);
    constexpr std::size_t weight_offset = 971;
    constexpr std::size_t bias_offset = 993;
    std::array<double, C> combined{};
    for (std::size_t i = 0; i < D; ++i) {
        combined[i] = state.fast[i];
        combined[D + i] = state.slow[i];
    }
    const auto bucket = eos_length_bucket(context_length);
    double logit = parameters.theta_len[bucket];
    for (std::size_t i = 0; i < C; ++i)
        logit += static_cast<double>(parameters.v[i]) * combined[i];
    const double target = should_stop ? 1.0 : 0.0;
    const double probability = math::sigmoid(logit);
    const double dz = probability - target;
    G2ExpertEosResult result;
    result.loss_nats = math::binary_cross_entropy_with_logits(logit, target);
    result.gradients.values[bias_offset + bucket] = dz;
    for (std::size_t i = 0; i < C; ++i)
        result.gradients.values[weight_offset + i] = dz * combined[i];
    if (!std::isfinite(result.loss_nats) ||
        !std::isfinite(result.gradients.global_norm()))
        throw std::domain_error("EOS head produced a non-finite loss or gradient");
    return result;
}

} // namespace morok::model
