#include "morok/g2/artemis/profile.hpp"
#include <algorithm>
#include <stdexcept>
namespace morok::g2::artemis {
std::array<model::G2GenerationReport,kAgentCount> run_agents(const std::array<const model::G2Parameters*,kAgentCount>& agents,const common::Utf8Text& prompt,const Policy& policy) {
 std::array<model::G2GenerationReport,kAgentCount> reports{};
 for(std::size_t i=0;i<kAgentCount;++i){if(!agents[i])throw std::invalid_argument("Artemis requires three initialized agents");engine::G2ExpertGenerationConfig config{};config.max_new_bytes=std::clamp<std::size_t>(policy.max_new_bytes,1,256);config.temperature=0.0;config.top_k=16;config.seed=policy.seed+static_cast<std::uint64_t>(i);reports[i]=model::generate_g2(*agents[i],prompt,config);}
 return reports;
}
}
