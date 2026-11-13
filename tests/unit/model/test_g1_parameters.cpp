#include "morok/model/parameters/g1_parameters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <std::size_t N>
bool all_finite(const std::array<float, N>& values) {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

void test_inventory_count_and_shapes() {
    using namespace morok::model;
    static_assert(G1Parameters::parameter_count() == 1000);
    static_assert(kG1FastParameterCount == 506);
    static_assert(kG1SlowParameterCount == 253);
    static_assert(kG1PredictionParameterCount == 212);
    static_assert(kG1SegmenterParameterCount == 29);

    G1Parameters parameters{};
    const auto flat = parameters.flatten();
    require(flat.size() == 1000, "flattened G1 inventory must contain 1000 values");
}

void test_deterministic_initialization_and_biases() {
    using namespace morok::model;
    G1Parameters first{};
    G1Parameters second{};
    initialize_g1_parameters(first, 42);
    initialize_g1_parameters(second, 42);
    require(first.flatten() == second.flatten(),
            "same seed must produce identical G1 parameters");

    require(std::all_of(first.b_a.begin(), first.b_a.end(),
                        [](float value) { return value == 0.0F; }),
            "fast gate bias must start at zero");
    require(std::all_of(first.b_c.begin(), first.b_c.end(),
                        [](float value) { return value == 0.0F; }),
            "candidate bias must start at zero");
    require(std::all_of(first.b_g.begin(), first.b_g.end(),
                        [](float value) { return value == 1.0F; }),
            "slow gate bias must start at one");
    require(std::all_of(first.c.begin(), first.c.end(),
                        [](float value) { return value == 0.0F; }),
            "prediction bias must start at zero");
    require(std::all_of(first.A.begin(), first.A.end(),
                        [](float value) { return value == 0.0F; }),
            "triangular prediction coefficients must start at zero");
    require(std::all_of(first.v.begin(), first.v.end(),
                        [](float value) { return value == 0.0F; }),
            "segmenter weights must start at zero");
    require(std::all_of(first.theta_len.begin(), first.theta_len.end(),
                        [](float value) { return value == 0.0F; }),
            "segmenter length biases must start at zero");
}

void test_all_parameters_are_finite_and_xavier_bounded() {
    using namespace morok::model;
    G1Parameters parameters{};
    initialize_g1_parameters(parameters, 123456);
    const auto flat = parameters.flatten();
    require(std::all_of(flat.begin(), flat.end(),
                        [](float value) { return std::isfinite(value); }),
            "all initialized parameters must be finite");

    const float xavier_limit = std::sqrt(6.0F / 22.0F);
    for (const float value : parameters.O) {
        require(std::abs(value) <= xavier_limit,
                "prediction matrix must obey Xavier uniform bounds");
    }
    const float square_limit = std::sqrt(6.0F / 22.0F);
    for (const float value : parameters.W_ax) {
        require(std::abs(value) <= square_limit,
                "input matrix must obey Xavier uniform bounds");
    }
}

void test_canonical_flatten_round_trip_and_rejection() {
    using namespace morok::model;
    G1Parameters original{};
    initialize_g1_parameters(original, 7);
    auto serialized = original.flatten();

    G1Parameters restored{};
    require(restored.assign(serialized), "valid 1000-value inventory must load");
    require(restored.flatten() == serialized,
            "canonical flatten/assign must preserve every parameter");

    const auto before = restored.flatten();
    require(!restored.assign(std::span<const float>(serialized.data(), serialized.size() - 1)),
            "wrong parameter count must be rejected");
    require(restored.flatten() == before,
            "failed assignment must not partially modify parameters");

    serialized[500] = std::numeric_limits<float>::quiet_NaN();
    require(!restored.assign(serialized), "NaN parameter must be rejected");
    require(restored.flatten() == before,
            "non-finite assignment must not partially modify parameters");
}

void test_canonical_order_boundaries() {
    using namespace morok::model;
    G1Parameters parameters{};
    parameters.W_ax[0] = 1.0F;
    parameters.W_ah[0] = 2.0F;
    parameters.W_cx[0] = 3.0F;
    parameters.W_ch[0] = 4.0F;
    parameters.b_a[0] = 5.0F;
    parameters.b_c[0] = 6.0F;
    parameters.W_gh[0] = 7.0F;
    parameters.W_gs[0] = 8.0F;
    parameters.b_g[0] = 9.0F;
    parameters.O[0] = 10.0F;
    parameters.c[0] = 11.0F;
    parameters.A[0] = 12.0F;
    parameters.v[0] = 13.0F;
    parameters.theta_len[0] = 14.0F;

    const auto flat = parameters.flatten();
    require(flat[0] == 1.0F, "W_ax must be first");
    require(flat[121] == 2.0F, "W_ah must follow W_ax");
    require(flat[242] == 3.0F, "W_cx must follow W_ah");
    require(flat[363] == 4.0F, "W_ch must follow W_cx");
    require(flat[484] == 5.0F, "b_a must follow fast matrices");
    require(flat[495] == 6.0F, "b_c must follow b_a");
    require(flat[506] == 7.0F, "W_gh must begin slow state");
    require(flat[627] == 8.0F, "W_gs must follow W_gh");
    require(flat[748] == 9.0F, "b_g must follow slow matrices");
    require(flat[759] == 10.0F, "O must follow slow state");
    require(flat[935] == 11.0F, "c must follow O");
    require(flat[943] == 12.0F, "A must follow c");
    require(flat[971] == 13.0F, "v must follow prediction head");
    require(flat[993] == 14.0F, "theta_len must follow v");
}

} // namespace

int main() {
    try {
        test_inventory_count_and_shapes();
        test_deterministic_initialization_and_biases();
        test_all_parameters_are_finite_and_xavier_bounded();
        test_canonical_flatten_round_trip_and_rejection();
        test_canonical_order_boundaries();
    } catch (const std::exception& error) {
        std::cerr << "G1 parameter test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
