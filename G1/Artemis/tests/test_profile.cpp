#include "morok/g1/artemis/profile.hpp"
#include "morok/model/parameters/g1_parameters.hpp"
#include <cassert>
static_assert(morok::g1::artemis::kAgentCount > 2);
int main() {
    std::array<morok::model::G1Parameters, morok::g1::artemis::kAgentCount> parameters{};
    std::array<const morok::model::G1Parameters*, morok::g1::artemis::kAgentCount> agents{};
    for (std::size_t i = 0; i < agents.size(); ++i) {
        morok::model::initialize_g1_parameters(parameters[i], 17 + i);
        agents[i] = &parameters[i];
    }
    auto prompt = morok::common::Utf8Text::from_utf8("A");
    assert(prompt);
    morok::g1::artemis::Policy policy{};
    policy.max_new_bytes = 1;
    const auto reports = morok::g1::artemis::run_agents(agents, *prompt.value_if(), policy);
    for (const auto& report : reports) assert(report.generated_bytes <= 1);
}
