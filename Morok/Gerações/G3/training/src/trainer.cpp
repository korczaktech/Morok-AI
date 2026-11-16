#include "morok/g3/training/trainer.hpp"

#include <unicode/unistr.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <numbers>
#include <sstream>
#include <random>
#include <stdexcept>
#include <string_view>

namespace morok::g3::training {
namespace {

using neural::G3Weights;
using neural::G3MutableWeightsView;
constexpr std::size_t V = neural::kVocabularySize;
constexpr std::size_t E = neural::kEmbeddingDim;
constexpr std::size_t H1 = neural::kLayer1HiddenDim;
constexpr std::size_t H2 = neural::kLayer2HiddenDim;
constexpr std::size_t G = neural::kGateCount;
constexpr std::uint16_t PAD = 256U, BOS = 257U, EOS = 258U, USR = 259U, BOT = 260U;

struct Gradients {
    G3Weights w;
    explicit Gradients(const G3Weights& source) {
        auto zero = [](std::vector<float>& out, const std::vector<float>& in) {
            out.assign(in.size(), 0.0F);
        };
        zero(w.embedding, source.embedding);
        zero(w.layer1_input, source.layer1_input);
        zero(w.layer1_recurrent, source.layer1_recurrent);
        zero(w.layer1_bias, source.layer1_bias);
        zero(w.layer2_input, source.layer2_input);
        zero(w.layer2_recurrent, source.layer2_recurrent);
        zero(w.layer2_bias, source.layer2_bias);
        zero(w.rms_scale, source.rms_scale);
        zero(w.projection, source.projection);
        zero(w.projection_bias, source.projection_bias);
        zero(w.output_bias, source.output_bias);
    }
    double norm() const {
        double result = 0.0;
        auto add = [&result](const std::vector<float>& values) {
            for (float value : values) result = std::hypot(result, value);
        };
        add(w.embedding); add(w.layer1_input); add(w.layer1_recurrent);
        add(w.layer1_bias); add(w.layer2_input); add(w.layer2_recurrent);
        add(w.layer2_bias); add(w.rms_scale); add(w.projection);
        add(w.projection_bias); add(w.output_bias);
        return result;
    }
};

struct GruTape {
    std::vector<float> input, previous, z, r, candidate, output;
};

struct StepTape {
    std::uint16_t input_token{0}, target{0};
    bool supervised{false};
    std::vector<float> input_mask, second_mask;
    GruTape first, second;
    std::vector<float> first_raw, normalized, logits;
    float inverse_rms{1.0F};
};

struct AdamState {
    std::vector<float> first, second;
    std::uint64_t step{0};
};

std::array<std::span<float>, 11> mutable_spans(G3MutableWeightsView v) {
    return {v.embedding, v.layer1_input, v.layer1_recurrent, v.layer1_bias,
            v.layer2_input, v.layer2_recurrent, v.layer2_bias, v.rms_scale,
            v.projection, v.projection_bias, v.output_bias};
}
std::array<const std::vector<float>*, 11> const_tensors(const G3Weights& w) {
    return {&w.embedding, &w.layer1_input, &w.layer1_recurrent, &w.layer1_bias,
            &w.layer2_input, &w.layer2_recurrent, &w.layer2_bias, &w.rms_scale,
            &w.projection, &w.projection_bias, &w.output_bias};
}

std::size_t total_parameters(const G3Weights& w) {
    return static_cast<std::size_t>(neural::count_parameters(w));
}

void flatten_gradients(const G3Weights& w, std::vector<float>& out) {
    out.clear(); out.reserve(total_parameters(w));
    for (const auto* tensor : const_tensors(w))
        out.insert(out.end(), tensor->begin(), tensor->end());
}

std::vector<bool> decay_mask(const G3Weights& w) {
    std::vector<bool> mask;
    mask.reserve(total_parameters(w));
    auto add = [&mask](const std::vector<float>& tensor, bool decay) {
        mask.insert(mask.end(), tensor.size(), decay);
    };
    add(w.embedding, true);
    add(w.layer1_input, true); add(w.layer1_recurrent, true);
    add(w.layer1_bias, false);
    add(w.layer2_input, true); add(w.layer2_recurrent, true);
    add(w.layer2_bias, false);
    add(w.rms_scale, false);
    add(w.projection, true);
    add(w.projection_bias, false);
    add(w.output_bias, false);
    return mask;
}

std::vector<float> dropout_mask(std::size_t size, double probability,
                               std::mt19937_64& rng) {
    std::vector<float> mask(size, 1.0F);
    if (probability <= 0.0) return mask;
    std::bernoulli_distribution keep(1.0 - probability);
    const float scale = static_cast<float>(1.0 / (1.0 - probability));
    for (float& value : mask) value = keep(rng) ? scale : 0.0F;
    return mask;
}

float sigmoid(float x) {
    if (x >= 0.0F) { const float e = std::exp(-x); return 1.0F / (1.0F + e); }
    const float e = std::exp(x); return e / (1.0F + e);
}

GruTape gru_forward(std::span<const float> input, std::span<const float> previous,
                    const std::vector<float>& wx, const std::vector<float>& wh,
                    const std::vector<float>& bias, std::size_t hidden) {
    const std::size_t in = input.size();
    GruTape tape;
    tape.input.assign(input.begin(), input.end());
    tape.previous.assign(previous.begin(), previous.end());
    tape.z.resize(hidden); tape.r.resize(hidden);
    tape.candidate.resize(hidden); tape.output.resize(hidden);
    for (std::size_t gate = 0; gate < 2U; ++gate) {
        auto& target = gate == 0U ? tape.z : tape.r;
        for (std::size_t row = 0; row < hidden; ++row) {
            double sum = bias[gate * hidden + row];
            const auto xo = (gate * hidden + row) * in;
            const auto ho = (gate * hidden + row) * hidden;
            for (std::size_t col = 0; col < in; ++col) sum += wx[xo + col] * input[col];
            for (std::size_t col = 0; col < hidden; ++col) sum += wh[ho + col] * previous[col];
            target[row] = sigmoid(static_cast<float>(sum));
        }
    }
    for (std::size_t row = 0; row < hidden; ++row) {
        double sum = bias[2U * hidden + row];
        const auto xo = (2U * hidden + row) * in;
        const auto ho = (2U * hidden + row) * hidden;
        for (std::size_t col = 0; col < in; ++col) sum += wx[xo + col] * input[col];
        for (std::size_t col = 0; col < hidden; ++col)
            sum += wh[ho + col] * tape.r[col] * previous[col];
        tape.candidate[row] = std::tanh(static_cast<float>(sum));
        tape.output[row] = (1.0F - tape.z[row]) * tape.candidate[row] +
                           tape.z[row] * previous[row];
    }
    return tape;
}

void gru_backward(const GruTape& t, std::size_t hidden,
                  const std::vector<float>& wx, const std::vector<float>& wh,
                  std::vector<float>& gwx, std::vector<float>& gwh,
                  std::vector<float>& gb, std::span<const float> dh,
                  std::vector<float>& dinput, std::vector<float>& dprevious) {
    const std::size_t in = t.input.size();
    dinput.assign(in, 0.0F);
    dprevious.assign(hidden, 0.0F);
    std::vector<float> dz(hidden), dr(hidden), dn(hidden), dq(hidden, 0.0F);
    for (std::size_t row = 0; row < hidden; ++row) {
        const float z = t.z[row], n = t.candidate[row];
        const float hp = t.previous[row];
        const float dn_pre = dh[row] * (1.0F - z) * (1.0F - n * n);
        const float dz_pre = dh[row] * (hp - n) * z * (1.0F - z);
        dprevious[row] += dh[row] * z;
        dz[row] = dz_pre;
        dn[row] = dn_pre;
        gb[2U * hidden + row] += dn_pre;
        gb[row] += dz_pre;
        const std::size_t xoff = (2U * hidden + row) * in;
        const std::size_t hoff = (2U * hidden + row) * hidden;
        for (std::size_t col = 0; col < in; ++col) {
            gwx[xoff + col] += dn_pre * t.input[col];
            dinput[col] += wx[xoff + col] * dn_pre;
        }
        for (std::size_t col = 0; col < hidden; ++col) {
            const float gated_previous = t.r[col] * t.previous[col];
            gwh[hoff + col] += dn_pre * gated_previous;
            dq[col] += wh[hoff + col] * dn_pre;
        }
    }
    for (std::size_t col = 0; col < hidden; ++col) {
        const float dr_pre = dq[col] * t.previous[col] *
                             t.r[col] * (1.0F - t.r[col]);
        dprevious[col] += dq[col] * t.r[col];
        dr[col] = dr_pre;
        gb[hidden + col] += dr_pre;
    }
    for (std::size_t row = 0; row < hidden; ++row) {
        const std::size_t xz = row * in, hz = row * hidden;
        const std::size_t xr = (hidden + row) * in;
        const std::size_t hr = (hidden + row) * hidden;
        for (std::size_t col = 0; col < in; ++col) {
            gwx[xz + col] += dz[row] * t.input[col];
            gwx[xr + col] += dr[row] * t.input[col];
            dinput[col] += wx[xz + col] * dz[row] + wx[xr + col] * dr[row];
        }
        for (std::size_t col = 0; col < hidden; ++col) {
            gwh[hz + col] += dz[row] * t.previous[col];
            gwh[hr + col] += dr[row] * t.previous[col];
            dprevious[col] += wh[hz + col] * dz[row] + wh[hr + col] * dr[row];
        }
    }
}

void normalize_forward(std::span<const float> input, const std::vector<float>& scale,
                       float epsilon, std::vector<float>& output, float& inverse_rms) {
    double sum = 0.0;
    for (float x : input) sum += static_cast<double>(x) * x;
    inverse_rms = static_cast<float>(1.0 / std::sqrt(
        sum / static_cast<double>(input.size()) + epsilon));
    output.resize(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = input[i] * inverse_rms * scale[i];
}

void normalize_backward(std::span<const float> input,
                        const std::vector<float>& scale, float inverse_rms,
                        std::span<const float> dy, std::vector<float>& dinput,
                        std::vector<float>& dscale) {
    double projection = 0.0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        projection += static_cast<double>(dy[i]) * scale[i] * input[i];
        dscale[i] += dy[i] * input[i] * inverse_rms;
    }
    const double correction = projection * inverse_rms * inverse_rms *
                              inverse_rms / static_cast<double>(input.size());
    dinput.resize(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        dinput[i] = dy[i] * scale[i] * inverse_rms -
                    static_cast<float>(correction * input[i]);
}

std::vector<float> output_logits(const G3Weights& w, std::span<const float> h2,
                                 std::vector<float>* projected_out = nullptr) {
    std::vector<float> projected(E);
    for (std::size_t row = 0; row < E; ++row) {
        double value = w.projection_bias[row];
        for (std::size_t col = 0; col < H2; ++col)
            value += w.projection[row * H2 + col] * h2[col];
        projected[row] = static_cast<float>(value);
    }
    std::vector<float> logits(V);
    for (std::size_t token = 0; token < V; ++token) {
        double value = w.output_bias[token];
        for (std::size_t dim = 0; dim < E; ++dim)
            value += w.embedding[token * E + dim] * projected[dim];
        logits[token] = static_cast<float>(value);
    }
    if (projected_out) *projected_out = std::move(projected);
    return logits;
}

std::vector<std::uint16_t> sequence_for(const DatasetExample& example,
                                        std::size_t& answer_start,
                                        std::size_t& eos_index) {
    std::vector<std::uint16_t> sequence;
    sequence.reserve(example.question.size() + example.answer.size() + 5U);
    sequence.push_back(BOS); sequence.push_back(USR);
    for (unsigned char byte : example.question) sequence.push_back(byte);
    sequence.push_back(BOT);
    answer_start = sequence.size();
    for (unsigned char byte : example.answer) sequence.push_back(byte);
    sequence.push_back(EOS);
    eos_index = sequence.size() - 1U;
    return sequence;
}

double example_forward_backward(neural::G3Backend& backend,
                                const DatasetExample& example,
                                const TrainingConfig& config,
                                std::mt19937_64& rng, Gradients* gradients,
                                double& gradient_norm, bool training) {
    const auto& w = backend.weights();
    std::size_t answer_start = 0, eos_index = 0;
    const auto sequence = sequence_for(example, answer_start, eos_index);
    std::vector<float> h1(H1, 0.0F), h2(H2, 0.0F);
    double total_loss = 0.0;
    std::size_t supervised_targets = 0U;
    const std::size_t steps = sequence.size() - 1U;
    for (std::size_t begin = 0; begin < steps; begin += config.bptt_steps) {
        const std::size_t end = std::min(steps, begin + config.bptt_steps);
        const auto mask1 = dropout_mask(E, training ? config.variational_dropout : 0.0, rng);
        const auto mask2 = dropout_mask(H1, training ? config.variational_dropout : 0.0, rng);
        std::vector<StepTape> tape;
        tape.reserve(end - begin);
        for (std::size_t t = begin; t < end; ++t) {
            StepTape step;
            step.input_token = sequence[t];
            step.target = sequence[t + 1U];
            step.supervised = t + 1U >= answer_start && t + 1U <= eos_index &&
                              step.target != PAD;
            step.input_mask = mask1;
            step.second_mask = mask2;
            std::vector<float> embedded(E);
            const std::size_t embedding_offset = static_cast<std::size_t>(step.input_token) * E;
            for (std::size_t i = 0; i < E; ++i)
                embedded[i] = w.embedding[embedding_offset + i] * mask1[i];
            step.first = gru_forward(embedded, h1, w.layer1_input,
                                     w.layer1_recurrent, w.layer1_bias, H1);
            step.first_raw = step.first.output;
            normalize_forward(step.first_raw, w.rms_scale, backend.rms_epsilon(),
                              step.normalized, step.inverse_rms);
            for (std::size_t i = 0; i < H1; ++i) step.normalized[i] *= mask2[i];
            step.second = gru_forward(step.normalized, h2, w.layer2_input,
                                      w.layer2_recurrent, w.layer2_bias, H2);
            step.logits = output_logits(w, step.second.output);
            h1 = step.first.output;
            h2 = step.second.output;
            if (step.supervised) {
                total_loss += causal_cross_entropy(step.logits, step.target,
                                                    config.label_smoothing);
                ++supervised_targets;
            }
            tape.push_back(std::move(step));
        }
        if (gradients == nullptr) continue;
        std::vector<float> dh1_future(H1, 0.0F), dh2_future(H2, 0.0F);
        for (std::size_t reverse = tape.size(); reverse-- > 0U;) {
            const auto& step = tape[reverse];
            std::vector<float> dh2(H2, 0.0F);
            if (step.supervised) {
                std::vector<float> dlogits;
                (void)causal_cross_entropy(step.logits, step.target,
                                           config.label_smoothing, &dlogits);
                std::vector<float> projected;
                (void)output_logits(w, step.second.output, &projected);
                std::vector<float> dprojected(E, 0.0F);
                for (std::size_t token = 0; token < V; ++token) {
                    const float dl = dlogits[token];
                    gradients->w.output_bias[token] += dl;
                    for (std::size_t dim = 0; dim < E; ++dim) {
                        gradients->w.embedding[token * E + dim] += dl * projected[dim];
                        dprojected[dim] += dl * w.embedding[token * E + dim];
                    }
                }
                for (std::size_t row = 0; row < E; ++row) {
                    gradients->w.projection_bias[row] += dprojected[row];
                    for (std::size_t col = 0; col < H2; ++col) {
                        gradients->w.projection[row * H2 + col] +=
                            dprojected[row] * step.second.output[col];
                        dh2[col] += w.projection[row * H2 + col] * dprojected[row];
                    }
                }
            }
            for (std::size_t i = 0; i < H2; ++i) dh2[i] += dh2_future[i];
            std::vector<float> dnorm, dprev2;
            gru_backward(step.second, H2, w.layer2_input, w.layer2_recurrent,
                         gradients->w.layer2_input, gradients->w.layer2_recurrent,
                         gradients->w.layer2_bias, dh2, dnorm, dprev2);
            for (std::size_t i = 0; i < H1; ++i) dnorm[i] *= step.second_mask[i];
            std::vector<float> dfirst, dscale(H1, 0.0F);
            normalize_backward(step.first_raw, w.rms_scale, step.inverse_rms,
                               dnorm, dfirst, dscale);
            for (std::size_t i = 0; i < H1; ++i) {
                dfirst[i] += dh1_future[i];
                gradients->w.rms_scale[i] += dscale[i];
            }
            std::vector<float> dembedded, dprev1;
            gru_backward(step.first, H1, w.layer1_input, w.layer1_recurrent,
                         gradients->w.layer1_input, gradients->w.layer1_recurrent,
                         gradients->w.layer1_bias, dfirst, dembedded, dprev1);
            const std::size_t emb_offset = static_cast<std::size_t>(step.input_token) * E;
            for (std::size_t i = 0; i < E; ++i)
                gradients->w.embedding[emb_offset + i] += dembedded[i] * step.input_mask[i];
            dh1_future = std::move(dprev1);
            dh2_future = std::move(dprev2);
        }
    }
    if (supervised_targets == 0U)
        throw std::runtime_error("example has no supervised response targets");
    if (gradients != nullptr) {
        const float inverse = 1.0F / static_cast<float>(supervised_targets);
        for (auto* tensor : {&gradients->w.embedding, &gradients->w.layer1_input,
             &gradients->w.layer1_recurrent, &gradients->w.layer1_bias,
             &gradients->w.layer2_input, &gradients->w.layer2_recurrent,
             &gradients->w.layer2_bias, &gradients->w.rms_scale,
             &gradients->w.projection, &gradients->w.projection_bias,
             &gradients->w.output_bias})
            for (float& value : *tensor) value *= inverse;
        gradient_norm = gradients->norm();
    }
    return total_loss / static_cast<double>(supervised_targets);
}

double adamw_update(neural::G3Backend& backend, const Gradients& gradients,
                  AdamState& state, const TrainingConfig& config,
                  std::uint64_t total_steps, std::size_t step_index,
                  double& clipped_norm) {
    std::vector<float> grad;
    flatten_gradients(gradients.w, grad);
    const double norm = gradients.norm();
    const double scale = norm > config.maximum_gradient_norm
        ? config.maximum_gradient_norm / norm : 1.0;
    clipped_norm = norm * scale;
    const std::size_t count = grad.size();
    if (state.first.empty()) { state.first.assign(count, 0.0F); state.second.assign(count, 0.0F); }
    if (state.first.size() != count || state.second.size() != count)
        throw std::logic_error("AdamW state shape does not match G3 parameter count");
    ++state.step;
    double lr = config.learning_rate;
    const std::size_t warmup = static_cast<std::size_t>(
        std::ceil(config.warmup_fraction * static_cast<double>(total_steps)));
    if (warmup > 0U && step_index <= warmup) {
        lr *= static_cast<double>(step_index) / static_cast<double>(warmup);
    } else {
        const auto decay_steps = std::max<std::size_t>(1U, total_steps > warmup ?
                                                       total_steps - warmup : 1U);
        const double progress = std::clamp(
            static_cast<double>(step_index > warmup ? step_index - warmup : 0U) /
            static_cast<double>(decay_steps), 0.0, 1.0);
        lr = config.minimum_learning_rate + 0.5 *
            (config.learning_rate - config.minimum_learning_rate) *
            (1.0 + std::cos(std::numbers::pi * progress));
    }
    const double correction1 = 1.0 - std::pow(config.beta1, static_cast<double>(state.step));
    const double correction2 = 1.0 - std::pow(config.beta2, static_cast<double>(state.step));
    const auto mask = decay_mask(backend.weights());
    auto spans = mutable_spans(backend.mutable_weights());
    std::size_t offset = 0U;
    for (auto span : spans) {
        for (float& parameter : span) {
            const double g = static_cast<double>(grad[offset]) * scale;
            const double m = config.beta1 * state.first[offset] + (1.0 - config.beta1) * g;
            const double v = config.beta2 * state.second[offset] + (1.0 - config.beta2) * g * g;
            double updated = parameter;
            if (mask[offset]) updated *= (1.0 - lr * config.weight_decay);
            updated -= lr * (m / correction1) /
                       (std::sqrt(v / correction2) + config.adam_epsilon);
            if (!std::isfinite(updated) || !std::isfinite(m) || !std::isfinite(v))
                throw std::runtime_error("AdamW generated a non-finite parameter or state");
            parameter = static_cast<float>(updated);
            state.first[offset] = static_cast<float>(m);
            state.second[offset] = static_cast<float>(v);
            ++offset;
        }
    }
    return lr;
}

std::string json_escape(std::string_view input) {
    std::string out;
    for (unsigned char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20U) out += '?';
                else out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

void append_jsonl(const std::filesystem::path& path, const std::string& line) {
    if (path.empty()) return;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::app);
    if (!out) throw std::runtime_error("cannot open G3 metrics JSONL: " + path.string());
    out << line << '\n';
    if (!out) throw std::runtime_error("failed writing G3 metrics JSONL");
}

std::string to_utf8(const icu::UnicodeString& value) {
    std::string out; value.toUTF8String(out); return out;
}

std::vector<UChar32> codepoints(const std::string& text) {
    const auto u = icu::UnicodeString::fromUTF8(text);
    std::vector<UChar32> result;
    for (int32_t i = 0; i < u.length();) {
        const UChar32 cp = u.char32At(i);
        result.push_back(cp); i += U16_LENGTH(cp);
    }
    return result;
}

std::string from_codepoints(const std::vector<UChar32>& cps) {
    icu::UnicodeString value;
    for (auto cp : cps) value.append(cp);
    return to_utf8(value);
}

UChar32 strip_accent(UChar32 cp) {
    switch (cp) {
        case 0x00E1: case 0x00E0: case 0x00E2: case 0x00E3: case 0x00E4: return 'a';
        case 0x00C1: case 0x00C0: case 0x00C2: case 0x00C3: case 0x00C4: return 'A';
        case 0x00E9: case 0x00EA: case 0x00E8: case 0x00EB: return 'e';
        case 0x00C9: case 0x00CA: case 0x00C8: case 0x00CB: return 'E';
        case 0x00ED: case 0x00EC: case 0x00EE: case 0x00EF: return 'i';
        case 0x00CD: case 0x00CC: case 0x00CE: case 0x00CF: return 'I';
        case 0x00F3: case 0x00F2: case 0x00F4: case 0x00F5: case 0x00F6: return 'o';
        case 0x00D3: case 0x00D2: case 0x00D4: case 0x00D5: case 0x00D6: return 'O';
        case 0x00FA: case 0x00F9: case 0x00FB: case 0x00FC: return 'u';
        case 0x00DA: case 0x00D9: case 0x00DB: case 0x00DC: return 'U';
        case 0x00E7: return 'c'; case 0x00C7: return 'C';
        default: return cp;
    }
}

std::vector<UChar32> keyboard_substitute(std::vector<UChar32> cps,
                                         std::mt19937_64& rng) {
    static const std::array<std::pair<char, char>, 22> neighbors{{
        {'a','s'},{'s','d'},{'d','f'},{'f','g'},{'g','h'},{'h','j'},
        {'j','k'},{'k','l'},{'q','w'},{'w','e'},{'e','r'},{'r','t'},
        {'t','y'},{'y','u'},{'u','i'},{'i','o'},{'o','p'},{'z','x'},
        {'x','c'},{'c','v'},{'v','b'},{'b','n'}}};
    std::vector<std::size_t> eligible;
    for (std::size_t i = 0; i < cps.size(); ++i)
        if (cps[i] < 128 && std::isalpha(static_cast<unsigned char>(cps[i])))
            eligible.push_back(i);
    if (eligible.empty()) return cps;
    std::uniform_int_distribution<std::size_t> pick(0U, eligible.size() - 1U);
    const auto index = eligible[pick(rng)];
    const char current = static_cast<char>(std::tolower(static_cast<unsigned char>(cps[index])));
    for (const auto& pair : neighbors) {
        if (pair.first == current) { cps[index] = pair.second; return cps; }
        if (pair.second == current) { cps[index] = pair.first; return cps; }
    }
    cps[index] = current == 'm' ? 'n' : 'm';
    return cps;
}

} // namespace

double causal_cross_entropy(const std::vector<float>& logits, std::uint16_t target,
                            double smoothing, std::vector<float>* gradient) {
    if (logits.size() != V || target >= V ||
        !std::isfinite(smoothing) || smoothing < 0.0 || smoothing >= 1.0)
        throw std::invalid_argument("invalid causal Cross-Entropy arguments");
    if (target == PAD) {
        if (gradient) gradient->assign(V, 0.0F);
        return 0.0;
    }
    float maximum = -std::numeric_limits<float>::infinity();
    for (float x : logits) {
        if (!std::isfinite(x)) throw std::domain_error("non-finite logit");
        maximum = std::max(maximum, x);
    }
    double denominator = 0.0;
    for (float x : logits) denominator += std::exp(static_cast<double>(x - maximum));
    const double logz = static_cast<double>(maximum) + std::log(denominator);
    std::vector<float> probabilities(V);
    for (std::size_t i = 0; i < V; ++i)
        probabilities[i] = static_cast<float>(std::exp(static_cast<double>(logits[i]) - logz));
    double loss = 0.0;
    if (target < 256U && smoothing > 0.0) {
        const double smooth_mass = smoothing / 256.0;
        for (std::size_t i = 0; i < 256U; ++i) {
            const double q = smooth_mass + (i == target ? 1.0 - smoothing : 0.0);
            if (q > 0.0) loss -= q * std::log(std::max<double>(probabilities[i], 1.0e-30));
            probabilities[i] -= static_cast<float>(q);
        }
    } else {
        loss = -std::log(std::max<double>(probabilities[target], 1.0e-30));
        probabilities[target] -= 1.0F;
    }
    if (gradient) *gradient = std::move(probabilities);
    return loss;
}

std::string perturb_question(const std::string& question,
                             const PerturbationConfig& config,
                             std::mt19937_64& random) {
    const double weights_sum = config.swap_adjacent_weight + config.omission_weight +
        config.nearby_key_weight + config.accent_case_punctuation_weight +
        config.repetition_spacing_weight;
    if (!std::isfinite(config.probability) || config.probability < 0.0 ||
        config.probability > 1.0 || std::abs(weights_sum - 1.0) > 1.0e-9)
        throw std::invalid_argument("perturbation probabilities must be valid and weights sum to one");
    std::bernoulli_distribution apply(config.probability);
    if (!apply(random)) return question;
    auto cps = codepoints(question);
    if (cps.size() < 2U) return question;
    std::discrete_distribution<int> operation{
        config.swap_adjacent_weight, config.omission_weight,
        config.nearby_key_weight, config.accent_case_punctuation_weight,
        config.repetition_spacing_weight};
    switch (operation(random)) {
        case 0: {
            std::uniform_int_distribution<std::size_t> pick(0U, cps.size() - 2U);
            const auto i = pick(random); std::swap(cps[i], cps[i + 1U]); break;
        }
        case 1: {
            std::uniform_int_distribution<std::size_t> pick(0U, cps.size() - 1U);
            cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(pick(random)));
            break;
        }
        case 2: cps = keyboard_substitute(std::move(cps), random); break;
        case 3: {
            std::uniform_int_distribution<int> kind(0, 2);
            if (kind(random) == 0) {
                std::uniform_int_distribution<std::size_t> pick(0U, cps.size() - 1U);
                auto& cp = cps[pick(random)];
                const auto plain = strip_accent(cp);
                cp = plain == cp ? (cp >= 'a' && cp <= 'z' ? cp - 32 :
                                    (cp >= 'A' && cp <= 'Z' ? cp + 32 : plain)) : plain;
            } else if (kind(random) == 1) {
                std::uniform_int_distribution<std::size_t> pick(0U, cps.size() - 1U);
                auto& cp = cps[pick(random)];
                cp = cp >= 'a' && cp <= 'z' ? cp - 32 :
                     (cp >= 'A' && cp <= 'Z' ? cp + 32 : strip_accent(cp));
            } else {
                const auto punctuation = std::find_if(cps.begin(), cps.end(),
                    [](UChar32 cp) { return cp == '?' || cp == '!' || cp == '.' || cp == ','; });
                if (punctuation != cps.end()) cps.erase(punctuation);
                else cps.push_back('?');
            }
            break;
        }
        default: {
            std::uniform_int_distribution<std::size_t> pick(0U, cps.size() - 1U);
            const auto i = pick(random);
            if (cps[i] == ' ') cps.insert(cps.begin() + static_cast<std::ptrdiff_t>(i), ' ');
            else cps.insert(cps.begin() + static_cast<std::ptrdiff_t>(i), cps[i]);
            break;
        }
    }
    if (cps.empty()) return question;
    return from_codepoints(cps);
}

TrainingReport train(neural::G3Backend& backend, const Dataset& dataset,
                     const TrainingConfig& config) {
    if (backend.parameter_count() != neural::kTargetParameterCount)
        throw std::invalid_argument("G3 trainer requires the exact 100000-parameter backend");
    if (config.epochs == 0U || config.bptt_steps == 0U || config.bptt_steps > 512U ||
        config.maximum_variants_per_example == 0U ||
        config.learning_rate <= 0.0 || config.minimum_learning_rate <= 0.0 ||
        config.minimum_learning_rate > config.learning_rate ||
        config.weight_decay < 0.0 || config.beta1 <= 0.0 || config.beta1 >= 1.0 ||
        config.beta2 <= 0.0 || config.beta2 >= 1.0 ||
        config.adam_epsilon <= 0.0 || config.label_smoothing < 0.0 ||
        config.label_smoothing >= 1.0 || config.variational_dropout < 0.0 ||
        config.variational_dropout >= 1.0 || config.maximum_gradient_norm <= 0.0 ||
        config.warmup_fraction < 0.0 || config.warmup_fraction >= 1.0)
        throw std::invalid_argument("invalid G3 training configuration");
    if (dataset.examples.empty()) throw std::invalid_argument("G3 dataset is empty");

    TrainingReport report;
    report.run_id = "g3-sft-" + std::to_string(config.seed) + "-" +
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    report.dataset_fingerprint = dataset.fingerprint;
    report.splits = split_by_group_id(dataset, SplitConfig{0.85, 0.10, 0.05, config.seed});
    std::mt19937_64 rng(config.seed);
    std::vector<DatasetExample> train_examples;
    for (const auto index : report.splits.train) {
        const auto expanded = expand_controlled(dataset.examples[index],
            config.maximum_variants_per_example, rng());
        for (auto item : expanded) {
            item.question = perturb_question(item.question, config.perturbation, rng);
            train_examples.push_back(std::move(item));
        }
    }
    report.augmented_examples = train_examples.size();
    if (train_examples.empty())
        throw std::runtime_error("group split produced an empty training set");
    if (train_examples.size() > std::numeric_limits<std::size_t>::max() / config.epochs)
        throw std::overflow_error("planned G3 optimizer step count overflow");
    const std::size_t planned_steps = config.epochs * train_examples.size();
    AdamState optimizer;
    std::size_t global_step = 0U;

    for (std::size_t epoch = 1U; epoch <= config.epochs; ++epoch) {
        std::shuffle(train_examples.begin(), train_examples.end(), rng);
        double train_loss = 0.0, last_norm = 0.0, max_norm = 0.0, last_lr = config.learning_rate;
        std::size_t seen = 0U;
        for (const auto& example : train_examples) {
            Gradients gradients(backend.weights());
            double raw_norm = 0.0;
            const double loss = example_forward_backward(
                backend, example, config, rng, &gradients, raw_norm, true);
            ++global_step;
            double clipped_norm = 0.0;
            last_lr = adamw_update(backend, gradients, optimizer, config,
                                         planned_steps, global_step, clipped_norm);
            train_loss += loss;
            ++seen; ++report.examples_seen; ++report.optimizer_steps;
            last_norm = clipped_norm;
            max_norm = std::max(max_norm, raw_norm);
            std::ostringstream event;
            event << std::setprecision(12)
                  << "{\"event\":\"example_loss\",\"run_id\":\"" << report.run_id
                  << "\",\"code_version\":\"g3-sft-1\",\"config_version\":\"g3-sft-config-1\""
                  << ",\"dataset_version\":\"" << json_escape(example.dataset_version)
                  << "\",\"seed\":" << config.seed
                  << ",\"epoch\":" << epoch
                  << ",\"example_id\":\"" << json_escape(example.example_id)
                  << "\",\"group_id\":\"" << json_escape(example.group_id)
                  << "\",\"question_loss\":" << loss
                  << ",\"example_loss\":" << loss
                  << ",\"gradient_norm\":" << raw_norm
                  << ",\"clipped_gradient_norm\":" << clipped_norm
                  << ",\"learning_rate\":" << last_lr << "}";
            append_jsonl(config.metrics_jsonl, event.str());
        }
        train_loss /= static_cast<double>(std::max<std::size_t>(1U, seen));
        double validation_loss = 0.0;
        std::size_t validation_seen = 0U;
        for (const auto index : report.splits.validation) {
            double ignored_norm = 0.0;
            validation_loss += example_forward_backward(
                backend, dataset.examples[index], config, rng, nullptr,
                ignored_norm, false);
            ++validation_seen;
        }
        if (validation_seen == 0U)
            throw std::runtime_error("group split produced an empty validation set");
        validation_loss /= static_cast<double>(validation_seen);
        EpochMetrics metrics{epoch, train_loss, validation_loss, seen,
                             validation_seen, seen, last_norm, max_norm};
        report.epochs.push_back(metrics);
        std::ostringstream event;
        event << std::setprecision(12)
              << "{\"event\":\"epoch\",\"run_id\":\"" << report.run_id
              << "\",\"dataset_fingerprint\":\"" << report.dataset_fingerprint
              << "\",\"epoch\":" << epoch
              << ",\"training_loss\":" << train_loss
              << ",\"validation_loss\":" << validation_loss
              << ",\"training_examples\":" << seen
              << ",\"validation_examples\":" << validation_seen
              << ",\"optimizer_steps\":" << seen
              << ",\"learning_rate_last\":" << last_lr
              << ",\"code_version\":\"g3-sft-1\",\"config_version\":\"g3-sft-config-1\""
              << ",\"gradient_norm_last\":" << last_norm
              << ",\"gradient_norm_max\":" << max_norm
              << ",\"seed\":" << config.seed << "}";
        append_jsonl(config.metrics_jsonl, event.str());
    }
    return report;
}

} // namespace morok::g3::training
