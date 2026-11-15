#include "morok/g1/tsukuyomi/profile.hpp"
#include "morok/model/parameters/g1_parameters.hpp"
#include <cassert>
static_assert(morok::g1::tsukuyomi::kAgentCount == 1);
int main() {
    morok::model::G1Parameters parameters{};
    morok::model::initialize_g1_parameters(parameters, 11);
    auto prompt = morok::common::Utf8Text::from_utf8("A");
    assert(prompt);
    morok::g1::tsukuyomi::Policy policy{};
    policy.max_new_bytes = 1;
    const auto report = morok::g1::tsukuyomi::generate(parameters, *prompt.value_if(), policy);
    assert(report.generated_bytes <= 1);
}
