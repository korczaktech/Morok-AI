#include "morok/g3/rl/g3_policy.hpp"
#include "morok/common/error.hpp"
#include "morok/g3/training/trainer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace morok::g3::rl {
namespace {
using morok::common::Error;
using morok::common::ErrorCode;
using morok::common::Status;
constexpr std::size_t V=neural::kVocabularySize;
Status fail(const char* stage,const char* message) {
    return Status::failure(Error::make(ErrorCode::invalid_argument,message,
                                       "g3.rl.g3_policy",stage));
}
bool replay(const neural::G3Backend& backend,std::span<const TokenId> context,
            std::span<const TokenId> prefix,std::vector<std::byte>& state) {
    if(!backend.reset_state(state)) return false;
    for(TokenId t:context) if(!backend.consume_token(state,t)) return false;
    for(TokenId t:prefix) {
        if(t==morok::lang::kEosToken) break;
        if(!morok::lang::ByteTokenizer::is_byte_token(t) ||
           !backend.consume_token(state,t)) return false;
    }
    return true;
}
}

struct G3PolicyAdapter::OptimizerState final {
    training::ReinforceOptimizerState state;
    training::TrainingConfig config;
};

G3PolicyAdapter::G3PolicyAdapter(neural::G3Backend& backend,std::string identity,
                                 double learning_rate,double weight_decay)
    :backend_(&backend),identity_(std::move(identity)),
     learning_rate_(learning_rate),weight_decay_(weight_decay),
     optimizer_state_(std::make_unique<OptimizerState>()) {
    if(identity_.empty() || !std::isfinite(learning_rate_) || learning_rate_<=0.0 ||
       !std::isfinite(weight_decay_) || weight_decay_<0.0 ||
       backend.parameter_count()!=neural::kTargetParameterCount)
        throw std::invalid_argument("Invalid G3 RL policy adapter configuration");
    optimizer_state_->config.bptt_steps=128U;
    optimizer_state_->config.learning_rate=learning_rate_;
    optimizer_state_->config.minimum_learning_rate=learning_rate_;
    optimizer_state_->config.weight_decay=weight_decay_;
    optimizer_state_->config.maximum_gradient_norm=1.0;
}
G3PolicyAdapter::~G3PolicyAdapter() = default;
std::string_view G3PolicyAdapter::identity() const noexcept { return identity_; }
Status G3PolicyAdapter::next_token_probabilities(
    std::span<const TokenId> context,std::span<const TokenId> prefix,
    std::span<float> probabilities) const {
    if(probabilities.size()!=V) return fail("policy.shape","G3 policy output must have 261 entries");
    std::vector<std::byte> state(backend_->descriptor().state_size_bytes);
    bool reused=false;
    if(cache_valid_ && cached_context_.size()==context.size() &&
       std::equal(cached_context_.begin(),cached_context_.end(),context.begin())) {
        if(prefix.size()==cached_prefix_.size() &&
           std::equal(cached_prefix_.begin(),cached_prefix_.end(),prefix.begin())) {
            state=cached_state_;
            reused=true;
        } else if(prefix.size()==cached_prefix_.size()+1U &&
                  std::equal(cached_prefix_.begin(),cached_prefix_.end(),prefix.begin())) {
            state=cached_state_;
            const TokenId appended=prefix.back();
            if(appended==morok::lang::kEosToken ||
               !morok::lang::ByteTokenizer::is_byte_token(appended) ||
               !backend_->consume_token(state,appended))
                return fail("policy.cache","G3 policy cache received an invalid appended token");
            reused=true;
        }
    }
    if(!reused && !replay(*backend_,context,prefix,state))
        return fail("policy.replay","G3 policy could not replay prompt and sampled prefix");
    cached_context_.assign(context.begin(),context.end());
    cached_prefix_.assign(prefix.begin(),prefix.end());
    cached_state_=state;
    cache_valid_=true;
    std::vector<float> logs(V);
    const auto status=backend_->next_token_log_probabilities(state,logs);
    if(!status) return status;
    for(std::size_t i=0;i<V;++i) probabilities[i]=std::exp(logs[i]);
    return Status::success();
}
Status G3PolicyAdapter::apply_batch_gradients(
    std::span<const SequenceGradient> gradients) {
    try {
        cache_valid_=false;
        (void)training::apply_reinforce_gradients(
            *backend_,gradients,optimizer_state_->state,optimizer_state_->config);
    } catch(const std::exception& error) {
        return fail("optimizer.update",error.what());
    }
    return Status::success();
}
} // namespace morok::g3::rl
