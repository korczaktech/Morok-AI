#include "morok/g1/apollo/profile.hpp"
#include "morok/model/parameters/g1_parameters.hpp"
#include <cassert>
int main() {
    morok::model::G1Parameters parameters{};
    morok::model::initialize_g1_parameters(parameters, 23);
    auto prompt = morok::common::Utf8Text::from_utf8("A");
    assert(prompt);
    morok::g1::apollo::Policy policy{};
    policy.max_new_bytes = 1;
    const auto report = morok::g1::apollo::generate(parameters, *prompt.value_if(), policy);
    assert(report.generated_bytes <= 1);
}
