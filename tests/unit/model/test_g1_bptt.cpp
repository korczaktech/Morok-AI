#include "morok/model/learn/g1_bptt.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

double loss_only(const morok::model::G1Parameters& p,
                 const std::vector<std::uint8_t>& bytes,
                 std::size_t block) {
    return morok::model::compute_g1_bptt_gradients(p, bytes, bytes.size()).mean_loss_nats;
}

void finite_difference_check(const morok::model::G1Parameters& original,
                             const std::vector<std::uint8_t>& bytes,
                             const morok::model::G1Gradients& analytic,
                             std::size_t index) {
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
        (loss_only(p_plus, bytes, 2) - loss_only(p_minus, bytes, 2)) /
        (2.0 * static_cast<double>(epsilon));
    const double expected = analytic.values[index];
    const double tolerance = 2.5e-2 + 8.0e-2 * std::abs(numeric);
    require(std::abs(numeric - expected) <= tolerance,
            "BPTT gradient disagrees with central finite differences");
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
        require(full.gradients.global_norm() > 0.0, "BPTT gradient is unexpectedly zero");
        require(full.final_state.fast != G1State{}.fast,
                "BPTT final fast state was not advanced");
        for (std::size_t i = 971; i < kG1ParameterCount; ++i) {
            require(full.gradients.values[i] == 0.0,
                    "segmenter parameters must not receive B0 gradients");
        }

        const auto short_blocks = compute_g1_bptt_gradients(parameters, bytes, 2);
        require(std::abs(short_blocks.mean_loss_nats - full.mean_loss_nats) < 1e-10,
                "truncation must not change forward loss");
        require(short_blocks.gradients.global_norm() > 0.0,
                "truncated BPTT gradient is unexpectedly zero");
        require(short_blocks.final_state.fast == full.final_state.fast &&
                short_blocks.final_state.slow == full.final_state.slow,
                "carried state must match regardless of truncation length");

        // Selected central-difference checks cover every trainable tensor family.
        for (const std::size_t index : std::array<std::size_t, 12>{
                 0, 121, 242, 363, 484, 495, 506, 627, 748, 759, 935, 943}) {
            finite_difference_check(parameters, bytes, full.gradients, index);
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

        std::cout << "G1 BPTT tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
