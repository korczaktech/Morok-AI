#include "morok/model/parameters/g2_parameters.hpp"
namespace morok::model {
std::array<float, kG2ParameterCount> G2Parameters::flatten() const {
    std::array<float, kG2ParameterCount> result{};
    const auto a = expert_a.flatten();
    const auto b = expert_b.flatten();
    for (std::size_t i=0;i<a.size();++i) result[i]=a[i];
    for (std::size_t i=0;i<b.size();++i) result[a.size()+i]=b[i];
    return result;
}
bool G2Parameters::assign(std::span<const float> values) noexcept {
    if (values.size()!=kG2ParameterCount) return false;
    G2ExpertParameters a=expert_a, b=expert_b;
    if (!a.assign(values.first(G2ExpertParameters::parameter_count())) ||
        !b.assign(values.subspan(G2ExpertParameters::parameter_count()))) return false;
    expert_a=a; expert_b=b; return true;
}
void initialize_g2_parameters(G2Parameters& p, std::uint64_t seed) {
    initialize_g2_expert_parameters(p.expert_a, seed);
    initialize_g2_expert_parameters(p.expert_b, seed ^ 0x9E3779B97F4A7C15ULL);
}
}
