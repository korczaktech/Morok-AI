#include "morok/model/learn/g1_bptt.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

double loss_only(const morok::model::G1Parameters& p,
                 const std::vector<std::uint8_t>& bytes,
                 std::size_t truncation,
                 const morok::model::G1State& initial_state = {}) {
    return morok::model::compute_g1_bptt_gradients(
        p, bytes, truncation, initial_state).mean_loss_nats;
}

void finite_difference_check(const morok::model::G1Parameters& original,
                             const std::vector<std::uint8_t>& bytes,
                             const morok::model::G1Gradients& analytic,
                             std::size_t index,
                             std::size_t truncation,
                             const morok::model::G1State& initial_state = {}) {
    auto plus = original.flatten();
    auto minus = original.flatten();
    constexpr float epsilon = 1.0e-2F;
    plus[index] += epsilon;
    minus[index] -= epsilon;
    morok::model::G1Parameters p_plus{};
    morok::model::G1Parameters p_minus{};
    require(p_plus.assign(plus), "plus parameter assignment failed");
    require(p_minus.assign(minus), "minus parameter assignment failed");
    const double numeric =
        (loss_only(p_plus, bytes, truncation, initial_state) -
         loss_only(p_minus, bytes, truncation, initial_state)) /
        (2.0 * static_cast<double>(epsilon));
    const double expected = analytic.values[index];
    const double tolerance = 2.5e-2 + 8.0e-2 * std::abs(numeric);
    require(std::abs(numeric - expected) <= tolerance,
            "BPTT gradient disagrees with central finite differences");
}

double reference_loss(const morok::model::G1Parameters& parameters,
                      const std::vector<std::uint8_t>& bytes,
                      const morok::model::G1State& initial_state) {
    using namespace morok::model;
    G1State state = initial_state;
    double total = 0.0;
    for (std::size_t target = 1; target < bytes.size(); ++target) {
        advance_g1_state(parameters, state, bytes[target - 1]);
        total += evaluate_g1_next_byte(parameters, state, bytes[target])
                     .negative_log_likelihood_nats;
    }
    return total / static_cast<double>(bytes.size() - 1);
}

bool all_finite(const morok::model::G1Gradients& gradients) {
    for (const double value : gradients.values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace morok::model;
    try {
        G1Parameters parameters{};
        initialize_g1_parameters(parameters, 0xC0FFEEULL);
        const std::vector<std::uint8_t> bytes{
            0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x4D, 0x6F, 0x72, 0x6F, 0x6B
        };

        const auto full = compute_g1_bptt_gradients(parameters, bytes, bytes.size());
        require(full.target_byte_count == bytes.size() - 1,
                "BPTT target count is incorrect");
        require(std::isfinite(full.mean_loss_nats), "BPTT loss is not finite");
        require(all_finite(full.gradients), "BPTT contains a non-finite gradient");
        require(full.gradients.global_norm() > 0.0,
                "BPTT gradient is unexpectedly zero");
        require(full.final_state.fast != G1State{}.fast,
                "BPTT final fast state was not advanced");
        for (std::size_t i = 971; i < kG1ParameterCount; ++i) {
            require(full.gradients.values[i] == 0.0,
                    "segmenter parameters must not receive B0 gradients");
        }

        require(std::abs(full.mean_loss_nats - reference_loss(parameters, bytes, {})) < 1e-11,
                "BPTT forward loss differs from the production forward/evaluation path");

        const auto short_blocks = compute_g1_bptt_gradients(parameters, bytes, 2);
        require(std::abs(short_blocks.mean_loss_nats - full.mean_loss_nats) < 1e-10,
                "truncation must not change forward loss");
        require(short_blocks.gradients.global_norm() > 0.0,
                "truncated BPTT gradient is unexpectedly zero");
        double truncation_difference = 0.0;
        for (std::size_t i = 0; i < kG1ParameterCount; ++i) {
            truncation_difference += std::abs(full.gradients.values[i] -
                                              short_blocks.gradients.values[i]);
        }
        require(truncation_difference > 1.0e-8,
                "truncated BPTT must cut gradient paths at block boundaries");
        require(short_blocks.final_state.fast == full.final_state.fast &&
                short_blocks.final_state.slow == full.final_state.slow,
                "carried state must match regardless of truncation length");

        const auto single_step_blocks = compute_g1_bptt_gradients(parameters, bytes, 1);
        require(single_step_blocks.final_state.fast == full.final_state.fast &&
                single_step_blocks.final_state.slow == full.final_state.slow,
                "one-target blocks must preserve the complete forward state");
        require(std::abs(single_step_blocks.mean_loss_nats - full.mean_loss_nats) < 1e-10,
                "one-target blocks must preserve the forward loss");

        const auto maximum_window = compute_g1_bptt_gradients(
            parameters, bytes, kG1MaxBpttTruncationLength);
        require(std::abs(maximum_window.mean_loss_nats - full.mean_loss_nats) < 1e-12,
                "maximum supported truncation length must behave as a full sequence");
        require(maximum_window.final_state.fast == full.final_state.fast &&
                maximum_window.final_state.slow == full.final_state.slow,
                "maximum supported truncation length changed the final state");

        bool rejected = false;
        try {
            (void)compute_g1_bptt_gradients(
                parameters, bytes, kG1MaxBpttTruncationLength + 1);
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "BPTT must enforce the 128-target memory bound");

        rejected = false;
        try {
            (void)compute_g1_bptt_gradients(
                parameters, bytes, std::numeric_limits<std::size_t>::max());
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "BPTT must reject extreme truncation lengths before allocation");

        for (const std::size_t index : std::array<std::size_t, 12>{
                 0, 121, 242, 363, 484, 495, 506, 627, 748, 759, 935, 943}) {
            finite_difference_check(parameters, bytes, full.gradients, index, bytes.size());
        }

        G1State nonzero_state{};
        for (std::size_t i = 0; i < kG1InputDimension; ++i) {
            nonzero_state.fast[i] = static_cast<float>(0.03 * static_cast<double>(i + 1));
            nonzero_state.slow[i] = static_cast<float>(-0.02 * static_cast<double>(i + 1));
        }
        const auto seeded = compute_g1_bptt_gradients(
            parameters, bytes, bytes.size(), nonzero_state);
        require(std::abs(seeded.mean_loss_nats -
                         reference_loss(parameters, bytes, nonzero_state)) < 1e-11,
                "non-zero initial state forward loss mismatch");
        for (const std::size_t index : std::array<std::size_t, 6>{
                 0, 363, 506, 627, 759, 943}) {
            finite_difference_check(parameters, bytes, seeded.gradients, index,
                                     bytes.size(), nonzero_state);
        }

        bool rejected = false;
        try {
            (void)compute_g1_bptt_gradients(parameters,
                std::span<const std::uint8_t>(bytes.data(), 1), 2);
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "BPTT must reject sequences shorter than two bytes");

        rejected = false;
        try { (void)compute_g1_bptt_gradients(parameters, bytes, 0); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "BPTT must reject zero truncation length");

        G1State invalid_state{};
        invalid_state.fast[3] = std::numeric_limits<float>::quiet_NaN();
        rejected = false;
        try {
            (void)compute_g1_bptt_gradients(parameters, bytes, 2, invalid_state);
        } catch (const std::domain_error&) { rejected = true; }
        require(rejected, "BPTT must reject a non-finite initial fast state");

        invalid_state = {};
        invalid_state.slow[5] = std::numeric_limits<float>::infinity();
        rejected = false;
        try {
            (void)compute_g1_bptt_gradients(parameters, bytes, 2, invalid_state);
        } catch (const std::domain_error&) { rejected = true; }
        require(rejected, "BPTT must reject a non-finite initial slow state");

        G1Parameters bad_parameters = parameters;
        bad_parameters.W_ax[0] = std::numeric_limits<float>::quiet_NaN();
        rejected = false;
        try {
            (void)compute_g1_bptt_gradients(bad_parameters, bytes, 2);
        } catch (const std::domain_error&) { rejected = true; }
        require(rejected, "BPTT must reject non-finite parameters");

        std::cout << "G1 BPTT tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
