#include "morok/g3/rl/reference_evaluator.hpp"
#include "morok/common/error.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace morok::g3::rl {
namespace {
std::string key(std::span<const TokenId> tokens) {
    std::string result;
    result.reserve(tokens.size()*2U);
    for(TokenId token:tokens) {
        result.push_back(static_cast<char>(token&0xffU));
        result.push_back(static_cast<char>((token>>8U)&0xffU));
    }
    return result;
}
std::size_t edit_distance(std::span<const TokenId> a,std::span<const TokenId> b) {
    std::vector<std::size_t> prev(b.size()+1U),cur(b.size()+1U);
    for(std::size_t j=0;j<=b.size();++j) prev[j]=j;
    for(std::size_t i=1;i<=a.size();++i) {
        cur[0]=i;
        for(std::size_t j=1;j<=b.size();++j)
            cur[j]=std::min({prev[j]+1U,cur[j-1U]+1U,
                             prev[j-1U]+(a[i-1U]==b[j-1U]?0U:1U)});
        prev.swap(cur);
    }
    return prev[b.size()];
}
double bigram_f1(std::span<const TokenId> a,std::span<const TokenId> b) {
    if(a.size()<2U || b.size()<2U) return a.size()==b.size() && a.size()==1U && a[0]==b[0]?1.0:0.0;
    std::size_t overlap=0;
    std::vector<bool> used(b.size()-1U,false);
    for(std::size_t i=0;i+1U<a.size();++i)
        for(std::size_t j=0;j+1U<b.size();++j)
            if(!used[j] && a[i]==b[j] && a[i+1U]==b[j+1U]) {used[j]=true;++overlap;break;}
    const double p=static_cast<double>(overlap)/static_cast<double>(a.size()-1U);
    const double r=static_cast<double>(overlap)/static_cast<double>(b.size()-1U);
    return p+r==0.0?0.0:2.0*p*r/(p+r);
}
}
ReferenceRewardEvaluator::ReferenceRewardEvaluator(
    std::vector<ReferenceAnswer> refs,std::string version)
    :rubric_version_(std::move(version)) {
    if(refs.empty() || rubric_version_.empty()) throw std::invalid_argument("Reference evaluator needs examples and a rubric version");
    for(auto& ref:refs) {
        if(ref.prompt.empty() || ref.answer.empty() ||
           !references_.emplace(key(ref.prompt),std::move(ref.answer)).second)
            throw std::invalid_argument("Reference evaluator contains an empty or duplicate prompt");
    }
}
EvaluatorMetadata ReferenceRewardEvaluator::metadata() const {
    return {"g3-independent-reference-rubric",rubric_version_,"dataset-reference-answer-v1"};
}
std::size_t ReferenceRewardEvaluator::expected_response_tokens(
    std::span<const TokenId> prompt) const {
    const auto found=references_.find(key(prompt));
    return found==references_.end()?0U:found->second.size();
}
morok::common::Result<RewardComponents> ReferenceRewardEvaluator::evaluate(
    std::span<const TokenId> prompt,std::span<const TokenId> response) const {
    const auto found=references_.find(key(prompt));
    if(found==references_.end())
        return morok::common::Result<RewardComponents>::failure(
            morok::common::Error::make(morok::common::ErrorCode::invalid_argument,
                "No reference answer exists for this prompt","g3.rl.evaluator","lookup"));
    const auto& target=found->second;
    const std::size_t denominator=std::max(target.size(),response.size());
    const double similarity=denominator==0U?0.0:
        1.0-static_cast<double>(edit_distance(target,response))/static_cast<double>(denominator);
    const double intent=bigram_f1(target,response);
    const double length=target.empty()?0.0:
        std::max(0.0,1.0-std::abs(static_cast<double>(response.size())-
                                 static_cast<double>(target.size()))/
                            static_cast<double>(target.size()));
    const double format=response.empty()?0.0:1.0;
    return morok::common::Result<RewardComponents>::success(
        RewardComponents{std::clamp(similarity,0.0,1.0),
                         std::clamp(intent,0.0,1.0),
                         std::clamp(length,0.0,1.0),format});
}
} // namespace morok::g3::rl
