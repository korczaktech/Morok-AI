#include "morok/common/utf8_text.hpp"
#include "morok/model/g1_forward.hpp"
#include "morok/model/learn/g1_bptt.hpp"
#include "morok/model/learn/g2_checkpoint.hpp"
#include "morok/model/learn/optimizer.hpp"
#include "morok/model/parameters/g2_parameters.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
double target_loss(const morok::model::G1Parameters& parameters,
                  const std::string& prompt, const std::string& response) {
 morok::model::G1State state{};
 for(std::size_t i=0;i+1<prompt.size();++i)
  morok::model::advance_g1_state(parameters,state,static_cast<std::uint8_t>(static_cast<unsigned char>(prompt[i])));
 std::vector<std::uint8_t> sequence;
 sequence.reserve(response.size()+1);
 sequence.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(prompt.back())));
 for(unsigned char byte:response) sequence.push_back(byte);
 return morok::model::compute_g1_bptt_gradients(parameters,sequence,64,state,prompt.size()-1).mean_loss_nats;
}
std::string read_file(const char* path) {
 std::ifstream f(path,std::ios::binary); if(!f) throw std::runtime_error(std::string("cannot open ")+path);
 std::string s((std::istreambuf_iterator<char>(f)),{}); if(f.bad()) throw std::runtime_error(std::string("cannot read ")+path); return s;
}
}
int main(int argc,char** argv) {
 if(argc<5||argc>7){std::cerr<<"usage: morok-reinforce-g2 <checkpoint-prefix> <prompt.txt> <response.txt> <reward:-1|1> [learning-rate] [steps]\n";return 2;}
 try {
  const auto prompt_text=read_file(argv[2]), response_text=read_file(argv[3]);
  if(prompt_text.empty()||response_text.size()<2) throw std::invalid_argument("prompt must be non-empty and response must contain at least two bytes");
  const std::string reward_text=argv[4]; if(reward_text!="1"&&reward_text!="-1") throw std::invalid_argument("reward must be exactly 1 or -1");
  const double reward=reward_text=="1"?1.0:-1.0;
  const double lr=argc>=6?std::stod(argv[5]):0.001;
  const auto steps=argc>=7?static_cast<std::size_t>(std::stoull(argv[6])):1U;
  if(!std::isfinite(lr)||lr<=0.0||lr>0.01||steps<1||steps>1000) throw std::invalid_argument("learning-rate or steps out of range");
  auto prompt=morok::common::Utf8Text::from_utf8(prompt_text), response=morok::common::Utf8Text::from_utf8(response_text);
  if(!prompt||!response) throw std::invalid_argument("prompt and response must be valid UTF-8");
  auto parameters=morok::model::load_g2_checkpoint(argv[1]);
  std::vector<std::uint8_t> sequence; sequence.reserve(response_text.size()+1);
  sequence.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(prompt_text.back())));
  for(unsigned char b:response_text) sequence.push_back(b);
  auto config=morok::model::G1OptimizerConfig::adamw(); config.learning_rate=lr; config.weight_decay=0.0; config.max_gradient_norm=5.0;
  auto optimizer_a=morok::model::make_g1_optimizer_state(morok::model::G1OptimizerKind::AdamW);
  auto optimizer_b=morok::model::make_g1_optimizer_state(morok::model::G1OptimizerKind::AdamW);
  const auto before=parameters.flatten(); double first_norm=0,last_norm=0;
  const double loss_before=(target_loss(parameters.expert_a,prompt_text,response_text)+target_loss(parameters.expert_b,prompt_text,response_text))*0.5;
  morok::model::G1OptimizerStepResult last_a{},last_b{};
  std::size_t applied_steps=0;
  double current_loss=loss_before;
  for(std::size_t n=0;n<steps;++n) {
   morok::model::G1State state_a{},state_b{};
   for(std::size_t i=0;i+1<prompt_text.size();++i) {
    const auto byte=static_cast<std::uint8_t>(static_cast<unsigned char>(prompt_text[i]));
    morok::model::advance_g1_state(parameters.expert_a,state_a,byte);
    morok::model::advance_g1_state(parameters.expert_b,state_b,byte);
   }
   auto ga=morok::model::compute_g1_bptt_gradients(parameters.expert_a,sequence,64,state_a,prompt_text.size()-1);
   auto gb=morok::model::compute_g1_bptt_gradients(parameters.expert_b,sequence,64,state_b,prompt_text.size()-1);
   last_norm=std::max(ga.gradients.global_norm(),gb.gradients.global_norm()); if(n==0) first_norm=last_norm;
   // Positive reward minimizes the demonstrated sequence loss; negative reward maximizes it.
   for(double& v:ga.gradients.values)v*=reward;
   for(double& v:gb.gradients.values)v*=reward;
   const auto saved_a=parameters.expert_a, saved_b=parameters.expert_b;
   const auto saved_optimizer_a=optimizer_a, saved_optimizer_b=optimizer_b;
   bool accepted=false;
   for(unsigned int retry=0;retry<10;++retry) {
    config.learning_rate=lr*std::pow(0.5,static_cast<double>(retry));
    auto candidate_a=saved_a, candidate_b=saved_b;
    auto candidate_optimizer_a=saved_optimizer_a, candidate_optimizer_b=saved_optimizer_b;
    const auto step_a=morok::model::apply_g1_optimizer_step(candidate_a,ga.gradients,config,candidate_optimizer_a);
    const auto step_b=morok::model::apply_g1_optimizer_step(candidate_b,gb.gradients,config,candidate_optimizer_b);
    const double candidate_loss=(target_loss(candidate_a,prompt_text,response_text)+target_loss(candidate_b,prompt_text,response_text))*0.5;
    const bool improved=reward>0.0?candidate_loss<current_loss:candidate_loss>current_loss;
    if(improved) {
     parameters.expert_a=std::move(candidate_a); parameters.expert_b=std::move(candidate_b);
     optimizer_a=std::move(candidate_optimizer_a); optimizer_b=std::move(candidate_optimizer_b);
     last_a=step_a; last_b=step_b; current_loss=candidate_loss; accepted=true; ++applied_steps; break;
    }
   }
   config.learning_rate=lr;
   if(!accepted) break;
  }
  const double loss_after=(target_loss(parameters.expert_a,prompt_text,response_text)+target_loss(parameters.expert_b,prompt_text,response_text))*0.5;
  const auto after=parameters.flatten(); double delta2=0;
  for(std::size_t i=0;i<before.size();++i){const double d=static_cast<double>(after[i])-before[i];delta2+=d*d;}
  morok::model::save_g2_checkpoint(argv[1],parameters);
  std::cout<<"{\"generation\":\"G2\",\"reward\":"<<(reward>0?1:-1)<<",\"target_bytes\":"<<response_text.size()
   <<",\"gradient_norm_first\":"<<first_norm<<",\"gradient_norm_last\":"<<last_norm
   <<",\"target_loss_before\":"<<loss_before<<",\"target_loss_after\":"<<loss_after
   <<",\"target_loss_delta\":"<<(loss_after-loss_before)<<",\"parameter_delta_l2\":"<<std::sqrt(delta2)<<",\"optimizer\":\"AdamW\",\"optimizer_steps_per_expert\":"<<applied_steps
   <<",\"expert_a_optimizer_step\":"<<last_a.step<<",\"expert_b_optimizer_step\":"<<last_b.step
   <<",\"trainable_parameters\":"<<morok::model::kG2ParameterCount<<"}\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"morok-reinforce-g2: "<<e.what()<<'\n';return 1;}
}
