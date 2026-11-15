#include "morok/g2/artemis/profile.hpp"
#include "morok/g2/model/parameters/g2_parameters.hpp"
#include <cassert>
static_assert(morok::g2::artemis::kAgentCount>2);
int main(){std::array<morok::g2::model::G2Parameters,morok::g2::artemis::kAgentCount> ps{};std::array<const morok::g2::model::G2Parameters*,morok::g2::artemis::kAgentCount> agents{};for(std::size_t i=0;i<agents.size();++i){morok::g2::model::initialize_g2_parameters(ps[i],41+i);agents[i]=&ps[i];}auto prompt=morok::common::Utf8Text::from_utf8("A");assert(prompt);morok::g2::artemis::Policy policy{};policy.max_new_bytes=1;auto reports=morok::g2::artemis::run_agents(agents,*prompt.value_if(),policy);for(const auto& r:reports)assert(r.generated_bytes<=1);}
