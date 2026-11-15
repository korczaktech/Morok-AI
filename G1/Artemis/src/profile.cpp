#include "morok/g1/artemis/profile.hpp"
#include <algorithm>
#include <stdexcept>
namespace morok::g1::artemis {
std::array<model::G1GenerationReport, kAgentCount> run_agents(
    const std::array<const model::G1Parameters*, kAgentCount>& agents,
    const common::Utf8Text& prompt, const Policy& policy) {
    std::array<model::G1GenerationReport, kAgentCount> reports{};
    for (std::size_t i = 0; i < kAgentCount; ++i) {
        if (agents[i] == nullptr) throw std::invalid_argument("Artemis requires three initialized agents");
        model::G1GenerationConfig config{};
        config.max_new_bytes = std::clamp<std::size_t>(policy.max_new_bytes, 1, 256);
        config.temperature = 0.0;
        config.top_k = 16;
        config.seed = policy.seed + static_cast<std::uint64_t>(i);
        reports[i] = model::generate_g1(*agents[i], prompt, config);
    }
    return reports;
}
}
