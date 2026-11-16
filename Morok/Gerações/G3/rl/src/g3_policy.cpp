#include "morok/g3/rl/g3_policy.hpp"

#include "morok/common/error.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace morok::g3::rl {
namespace {
using morok::common::Error;
using morok::common::ErrorCode;
using morok::common::Status;
constexpr std::size_t E=neural::kEmbeddingDim, H1=neural::kLayer1HiddenDim,
                      H2=neural::kLayer2HiddenDim, V=neural::kVocabularySize;
constexpr double kBeta1=0.9, kBeta2=0.999, kEpsilon=1.0e-8;

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

G3PolicyAdapter::G3PolicyAdapter(neural::G3Backend& backend,std::string identity,
                                 double learning_rate,double weight_decay)
    :backend_(&backend),identity_(std::move(identity)),
     learning_rate_(learning_rate),weight_decay_(weight_decay) {
    if(identity_.empty() || !std::isfinite(learning_rate_) || learning_rate_<=0.0 ||
       !std::isfinite(weight_decay_) || weight_decay_<0.0 ||
       backend.parameter_count()!=neural::kTargetParameterCount)
        throw std::invalid_argument("Invalid G3 RL policy adapter configuration");
}
std::string_view G3PolicyAdapter::identity() const noexcept { return identity_; }

Status G3PolicyAdapter::next_token_probabilities(
    std::span<const TokenId> context,std::span<const TokenId> prefix,
    std::span<float> probabilities) const {
    if(probabilities.size()!=V) return fail("policy.shape","G3 policy output must have 261 entries");
    std::vector<std::byte> state(backend_->descriptor().state_size_bytes);
    if(!replay(*backend_,context,prefix,state))
        return fail("policy.replay","G3 policy could not replay prompt and sampled prefix");
    std::vector<float> logs(V);
    const auto status=backend_->next_token_log_probabilities(state,logs);
    if(!status) return status;
    for(std::size_t i=0;i<V;++i) probabilities[i]=std::exp(logs[i]);
    return Status::success();
}

Status G3PolicyAdapter::apply_batch_gradients(
    std::span<const SequenceGradient> gradients) {
    if(gradients.empty()) return fail("optimizer.empty","G3 RL optimizer received an empty batch");
    auto view=backend_->mutable_weights();
    std::array<std::span<float>,4> tensors{
        view.embedding,view.projection,view.projection_bias,view.output_bias};
    std::size_t total=0;
    for(auto t:tensors) total+=t.size();
    std::vector<double> accumulated(total,0.0);
    for(const auto& sequence:gradients) {
        if(sequence.context.empty() || sequence.response.empty() ||
           sequence.logit_gradients.size()!=sequence.response.size()*V)
            return fail("optimizer.gradient_shape","G3 REINFORCE sequence gradient has invalid dimensions");
        std::vector<std::byte> state(backend_->descriptor().state_size_bytes);
        if(!replay(*backend_,sequence.context,{},state))
            return fail("optimizer.context","G3 REINFORCE context replay failed");
        for(std::size_t step=0;step<sequence.response.size();++step) {
            float h[H1+H2]{};
            std::memcpy(h,state.data(),state.size());
            const float* h2=h+H1;
            std::vector<float> projected(E,0.0F);
            for(std::size_t row=0;row<E;++row) {
                double value=view.projection_bias[row];
                for(std::size_t col=0;col<H2;++col)
                    value+=view.projection[row*H2+col]*h2[col];
                projected[row]=static_cast<float>(value);
            }
            std::vector<double> dprojected(E,0.0);
            const std::size_t embedding_offset=0;
            const std::size_t projection_offset=view.embedding.size();
            const std::size_t projection_bias_offset=projection_offset+view.projection.size();
            const std::size_t output_bias_offset=projection_bias_offset+view.projection_bias.size();
            for(std::size_t token=0;token<V;++token) {
                const double dl=sequence.logit_gradients[step*V+token];
                accumulated[output_bias_offset+token]+=dl;
                for(std::size_t dim=0;dim<E;++dim) {
                    accumulated[embedding_offset+token*E+dim]+=dl*projected[dim];
                    dprojected[dim]+=dl*view.embedding[token*E+dim];
                }
            }
            for(std::size_t row=0;row<E;++row) {
                accumulated[projection_bias_offset+row]+=dprojected[row];
                for(std::size_t col=0;col<H2;++col)
                    accumulated[projection_offset+row*H2+col]+=dprojected[row]*h2[col];
            }
            const TokenId chosen=sequence.response[step];
            if(chosen!=morok::lang::kEosToken &&
               !backend_->consume_token(state,chosen))
                return fail("optimizer.replay_response","G3 REINFORCE response replay failed");
        }
    }
    double norm=0.0;
    for(double g:accumulated) norm=std::hypot(norm,g);
    if(!std::isfinite(norm)) return fail("optimizer.non_finite","G3 RL gradient norm is non-finite");
    const double clip=norm>1.0?1.0/norm:1.0;
    if(first_moment_.empty()) { first_moment_.assign(total,0.0F); second_moment_.assign(total,0.0F); }
    if(first_moment_.size()!=total || second_moment_.size()!=total)
        return fail("optimizer.state_shape","G3 RL optimizer state shape changed");
    ++optimizer_step_;
    const double c1=1.0-std::pow(kBeta1,static_cast<double>(optimizer_step_));
    const double c2=1.0-std::pow(kBeta2,static_cast<double>(optimizer_step_));
    std::size_t offset=0;
    for(std::size_t tensor_index=0;tensor_index<tensors.size();++tensor_index) {
        auto tensor=tensors[tensor_index];
        const bool decay=tensor_index<2U;
        for(std::size_t i=0;i<tensor.size();++i,++offset) {
            const double grad=accumulated[offset]*clip;
            first_moment_[offset]=static_cast<float>(kBeta1*first_moment_[offset]+(1.0-kBeta1)*grad);
            second_moment_[offset]=static_cast<float>(kBeta2*second_moment_[offset]+(1.0-kBeta2)*grad*grad);
            const double m=first_moment_[offset]/c1;
            const double v=second_moment_[offset]/c2;
            const double update=m/(std::sqrt(v)+kEpsilon)+(decay?weight_decay_*tensor[i]:0.0);
            tensor[i]=static_cast<float>(tensor[i]-learning_rate_*update);
            if(!std::isfinite(tensor[i])) return fail("optimizer.non_finite_weight","G3 RL update produced a non-finite weight");
        }
    }
    return Status::success();
}

} // namespace morok::g3::rl
