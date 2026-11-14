#include "morok/model/learn/g1_bptt.hpp"
#include "morok/model/learn/g2_checkpoint.hpp"
#include "morok/model/learn/optimizer.hpp"
#include "morok/model/parameters/g2_parameters.hpp"
#include <filesystem>
#include <stdexcept>
#include <vector>
int main(){
 using namespace morok::model;
 const auto dir=std::filesystem::temp_directory_path()/"morok-g2-reinforcement-test";
 try{
  std::filesystem::remove_all(dir); std::filesystem::create_directories(dir);
  G2Parameters p; initialize_g2_parameters(p,123); const auto before=p.flatten();
  const std::vector<std::uint8_t> sequence{'Q','A','B','C'};
  auto config=G1OptimizerConfig::sgd_momentum(); config.learning_rate=0.0001; config.momentum=0.0; config.weight_decay=0.0; config.max_gradient_norm=0.25;
  auto optimizer_a=make_g1_optimizer_state(G1OptimizerKind::SGDMomentum);
  auto optimizer_b=make_g1_optimizer_state(G1OptimizerKind::SGDMomentum);
  auto gradient_a=compute_g1_bptt_gradients(p.expert_a,sequence,64);
  auto gradient_b=compute_g1_bptt_gradients(p.expert_b,sequence,64);
  for(double& value:gradient_a.gradients.values)value*=1.0;
  for(double& value:gradient_b.gradients.values)value*=1.0;
  const auto step_a=apply_g1_optimizer_step(p.expert_a,gradient_a.gradients,config,optimizer_a);
  const auto step_b=apply_g1_optimizer_step(p.expert_b,gradient_b.gradients,config,optimizer_b);
  if(step_a.step!=1||step_b.step!=1)throw std::runtime_error("both G2 experts must receive an optimizer update");
  const auto after=p.flatten(); if(after==before)throw std::runtime_error("G2 reinforcement did not update parameters");
  bool changed_a=false,changed_b=false;
  for(std::size_t i=0;i<kG1ParameterCount;++i)changed_a=changed_a||(after[i]!=before[i]);
  for(std::size_t i=kG1ParameterCount;i<kG2ParameterCount;++i)changed_b=changed_b||(after[i]!=before[i]);
  if(!changed_a||!changed_b)throw std::runtime_error("reinforcement must update both experts");
  const auto prefix=dir/"checkpoint"; save_g2_checkpoint(prefix,p); auto q=load_g2_checkpoint(prefix);
  if(q.flatten()!=after)throw std::runtime_error("G2 reinforcement checkpoint roundtrip failed");
  if(G2Parameters::parameter_count()!=2000)throw std::runtime_error("G2 reinforcement must preserve 2000 trainable parameters");
  std::filesystem::remove_all(dir); return 0;
 }catch(...){std::filesystem::remove_all(dir);throw;}
}
