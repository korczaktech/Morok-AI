#include "morok/g2/engine/bptt.hpp"
#include "morok/model/learn/g2_checkpoint.hpp"
#include "morok/g2/engine/optimizer.hpp"
#include "morok/model/parameters/g2_parameters.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
int main(){
 using namespace morok::model;
 const auto dir=std::filesystem::temp_directory_path()/"morok-g2-reinforcement-test";
 try{
  std::filesystem::remove_all(dir); std::filesystem::create_directories(dir);
  G2Parameters p; initialize_g2_parameters(p,123); const auto before=p.flatten();
  const std::string prompt="Usuario: O ceu e azul?\nIA: ";
  const std::string answer="Sim, o ceu e azul";
  G2ExpertState context_a{},context_b{};
  for(std::size_t i=0;i+1<prompt.size();++i){
   const auto byte=static_cast<std::uint8_t>(static_cast<unsigned char>(prompt[i]));
   advance_g2_expert_state(p.expert_a,context_a,byte); advance_g2_expert_state(p.expert_b,context_b,byte);
  }
  std::vector<std::uint8_t> sequence{static_cast<std::uint8_t>(static_cast<unsigned char>(prompt.back()))};
  for(unsigned char byte:answer)sequence.push_back(byte);
  auto config=G2ExpertOptimizerConfig::adamw(); config.learning_rate=0.001; config.weight_decay=0.0; config.max_gradient_norm=5.0;
  auto positive=p;
  auto positive_a=compute_g2_expert_bptt_gradients(positive.expert_a,sequence,64,context_a,prompt.size()-1);
  auto positive_b=compute_g2_expert_bptt_gradients(positive.expert_b,sequence,64,context_b,prompt.size()-1);
  const double before_a=positive_a.mean_loss_nats, before_b=positive_b.mean_loss_nats;
  auto optimizer_a=make_g2_expert_optimizer_state(G2ExpertOptimizerKind::AdamW);
  auto optimizer_b=make_g2_expert_optimizer_state(G2ExpertOptimizerKind::AdamW);
  const auto step_a=apply_g2_expert_optimizer_step(positive.expert_a,positive_a.gradients,config,optimizer_a);
  const auto step_b=apply_g2_expert_optimizer_step(positive.expert_b,positive_b.gradients,config,optimizer_b);
  if(step_a.step!=1||step_b.step!=1)throw std::runtime_error("both G2 experts must receive an optimizer update");
  const auto after_positive_a=compute_g2_expert_bptt_gradients(positive.expert_a,sequence,64,context_a,prompt.size()-1).mean_loss_nats;
  const auto after_positive_b=compute_g2_expert_bptt_gradients(positive.expert_b,sequence,64,context_b,prompt.size()-1).mean_loss_nats;
  if(!(after_positive_a<before_a)||!(after_positive_b<before_b))
   throw std::runtime_error("positive reward must reduce target sequence loss for both experts");

  auto negative=p;
  auto negative_a=compute_g2_expert_bptt_gradients(negative.expert_a,sequence,64,context_a,prompt.size()-1);
  auto negative_b=compute_g2_expert_bptt_gradients(negative.expert_b,sequence,64,context_b,prompt.size()-1);
  const double negative_before_a=negative_a.mean_loss_nats, negative_before_b=negative_b.mean_loss_nats;
  for(double& value:negative_a.gradients.values)value=-value;
  for(double& value:negative_b.gradients.values)value=-value;
  auto negative_optimizer_a=make_g2_expert_optimizer_state(G2ExpertOptimizerKind::AdamW);
  auto negative_optimizer_b=make_g2_expert_optimizer_state(G2ExpertOptimizerKind::AdamW);
  apply_g2_expert_optimizer_step(negative.expert_a,negative_a.gradients,config,negative_optimizer_a);
  apply_g2_expert_optimizer_step(negative.expert_b,negative_b.gradients,config,negative_optimizer_b);
  if(!(compute_g2_expert_bptt_gradients(negative.expert_a,sequence,64,context_a,prompt.size()-1).mean_loss_nats>negative_before_a)||
     !(compute_g2_expert_bptt_gradients(negative.expert_b,sequence,64,context_b,prompt.size()-1).mean_loss_nats>negative_before_b))
   throw std::runtime_error("negative reward must increase target sequence loss for both experts");

  const auto after=positive.flatten(); if(after==before)throw std::runtime_error("G2 reinforcement did not update parameters");
  bool changed_a=false,changed_b=false;
  for(std::size_t i=0;i<kG2ExpertParameterCount;++i)changed_a=changed_a||(after[i]!=before[i]);
  for(std::size_t i=kG2ExpertParameterCount;i<kG2ParameterCount;++i)changed_b=changed_b||(after[i]!=before[i]);
  if(!changed_a||!changed_b)throw std::runtime_error("reinforcement must update both experts");
  const auto prefix=dir/"checkpoint"; save_g2_checkpoint(prefix,positive); auto q=load_g2_checkpoint(prefix);
  if(q.flatten()!=after)throw std::runtime_error("G2 reinforcement checkpoint roundtrip failed");
  if(G2Parameters::parameter_count()!=2000)throw std::runtime_error("G2 reinforcement must preserve 2000 trainable parameters");
  std::filesystem::remove_all(dir); return 0;
 }catch(...){std::filesystem::remove_all(dir);throw;}
}
