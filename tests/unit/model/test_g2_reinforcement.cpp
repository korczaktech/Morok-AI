#include "morok/model/learn/g2_checkpoint.hpp"
#include "morok/model/parameters/g2_parameters.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
int main(){
 using namespace morok::model;
 const auto dir=std::filesystem::temp_directory_path()/"morok-g2-reinforcement-test";
 try{
  std::filesystem::remove_all(dir); std::filesystem::create_directories(dir);
  G2Parameters p; initialize_g2_parameters(p,123); const auto prefix=dir/"checkpoint";
  save_g2_checkpoint(prefix,p); auto q=load_g2_checkpoint(prefix);
  if(q.flatten()!=p.flatten())throw std::runtime_error("G2 reinforcement checkpoint roundtrip failed");
  if(q.parameter_count()!=2000)throw std::runtime_error("G2 reinforcement must preserve 2000 trainable parameters");
  std::filesystem::remove_all(dir); return 0;
 }catch(...){std::filesystem::remove_all(dir);throw;}
}
