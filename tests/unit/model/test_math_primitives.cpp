#include "morok/model/math/initialization.hpp"
#include "morok/model/math/primitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

void require(bool condition, const char* message = "math primitive test assertion failed") {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(double left, double right, double tolerance = 1e-10) {
    return std::abs(left - right) <= tolerance;
}

void test_stable_activations_and_losses() {
    using namespace morok::math;
    require(close(sigmoid(0.0), 0.5));
    require(sigmoid(1000.0) > 0.999999);
    require(sigmoid(-1000.0) < 1e-12);
    require(close(tanh_activation(0.0), 0.0));
    require(close(softplus(0.0), std::log(2.0)));
    require(close(softplus(1000.0), 1000.0));
    require(softplus(-1000.0) >= 0.0);
    require(close(binary_cross_entropy_with_logits(0.0, 1.0), std::log(2.0)));
    require(binary_cross_entropy_with_logits(1000.0, 1.0) < 1e-12);
    require(binary_cross_entropy_with_logits(-1000.0, 1.0) > 999.0);

    bool rejected = false;
    try {
        static_cast<void>(binary_cross_entropy_with_logits(0.0, 2.0));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected);
}

void test_softmax_and_log_sum_exp() {
    using namespace morok::math;
    const std::array<double, 3> logits{1000.0, 1001.0, 999.0};
    const auto probabilities = softmax(logits);
    const double total = probabilities[0] + probabilities[1] + probabilities[2];
    require(close(total, 1.0));
    require(probabilities[1] > probabilities[0]);
    require(close(log_sum_exp(std::array<double, 2>{0.0, 0.0}), std::log(2.0)));

    const std::array<double, 3> equal{-1000.0, -1000.0, -1000.0};
    const auto uniform = softmax(equal);
    require(close(uniform[0], 1.0 / 3.0));
    require(close(uniform[1], 1.0 / 3.0));
    require(close(uniform[2], 1.0 / 3.0));

    bool rejected = false;
    const std::array<double, 2> invalid{0.0, std::numeric_limits<double>::infinity()};
    try {
        static_cast<void>(softmax(invalid));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected);
}

void test_matrix_vector_and_dimensions() {
    using namespace morok::math;
    Matrix matrix(2, 3);
    matrix(0, 0) = 1.0;
    matrix(0, 1) = 2.0;
    matrix(0, 2) = 3.0;
    matrix(1, 0) = -1.0;
    matrix(1, 1) = 0.0;
    matrix(1, 2) = 1.0;
    const std::array<double, 3> vector{2.0, 1.0, 0.0};
    const auto result = matvec(matrix, vector);
    require(close(result[0], 4.0));
    require(close(result[1], -2.0));

    bool rejected = false;
    const std::array<double, 2> wrong{1.0, 2.0};
    try {
        static_cast<void>(matvec(matrix, wrong));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected);
}

void test_gradient_clipping() {
    using namespace morok::math;
    std::array<double, 3> gradients{3.0, 4.0, 0.0};
    const double original_norm = clip_global_norm(gradients, 2.5);
    require(close(original_norm, 5.0));
    require(close(global_norm(gradients), 2.5));
    require(close(gradients[0], 1.5));
    require(close(gradients[1], 2.0));

    std::array<double, 2> small{0.3, 0.4};
    const auto before = small;
    static_cast<void>(clip_global_norm(small, 1.0));
    require(small == before);

    bool rejected = false;
    std::array<double, 1> invalid{std::numeric_limits<double>::quiet_NaN()};
    try {
        static_cast<void>(global_norm(invalid));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected);
}

void test_deterministic_initialization() {
    using namespace morok::math;
    Matrix first(11, 11);
    Matrix second(11, 11);
    initialize_xavier_uniform(first, 42);
    initialize_xavier_uniform(second, 42);
    require(std::equal(first.values().begin(), first.values().end(), second.values().begin()));

    Matrix rectangular_a(11, 11);
    Matrix rectangular_b(11, 11);
    initialize_orthogonal(rectangular_a, 123);
    initialize_orthogonal(rectangular_b, 123);
    require(std::equal(rectangular_a.values().begin(), rectangular_a.values().end(),
                      rectangular_b.values().begin()));

    for (std::size_t left = 0; left < rectangular_a.columns(); ++left) {
        for (std::size_t right = 0; right < rectangular_a.columns(); ++right) {
            double dot = 0.0;
            for (std::size_t row = 0; row < rectangular_a.rows(); ++row) {
                dot += rectangular_a(row, left) * rectangular_a(row, right);
            }
            require(close(dot, left == right ? 1.0 : 0.0, 1e-9));
        }
    }

    Matrix wide(3, 5);
    initialize_orthogonal(wide, 7);
    for (std::size_t left = 0; left < wide.rows(); ++left) {
        for (std::size_t right = 0; right < wide.rows(); ++right) {
            double dot = 0.0;
            for (std::size_t column = 0; column < wide.columns(); ++column) {
                dot += wide(left, column) * wide(right, column);
            }
            require(close(dot, left == right ? 1.0 : 0.0, 1e-9));
        }
    }
}

} // namespace

int main() {
    test_stable_activations_and_losses();
    test_softmax_and_log_sum_exp();
    test_matrix_vector_and_dimensions();
    test_gradient_clipping();
    test_deterministic_initialization();
    return 0;
}
