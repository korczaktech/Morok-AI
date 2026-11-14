#include "morok/model/learn/g1_training.hpp"
#include "morok/model/learn/g1_bptt.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/parameters/g1_parameters.hpp"
#include "morok/model/parameters/g1_parameters.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
namespace {
void req(bool x,const char* m){if(!x)throw std::runtime_error(m);}
void write(const std::filesystem::path& p,const std::string& s){std::ofstream f(p,std::ios::binary);f.write(s.data(),s.size());if(!f)throw std::runtime_error("test file write failed");}
void pipeline(){
 auto root=std::filesystem::temp_directory_path()/"morok-g1-trainer-test";std::filesystem::remove_all(root);std::filesystem::create_directories(root);
 try{
  for(int i=0;i<20;++i)write(root/("d"+std::to_string(i)+".txt"),"texto de teste "+std::to_string(i)+"\n");
  write(root/"duplicate.txt","texto de teste 0\n");
  morok::model::G1TrainingConfig c;c.corpus_directory=root;c.max_epochs=2;c.patience=2;c.truncation_length=8;
  morok::model::G1Parameters p;morok::model::initialize_g1_parameters(p,42);auto r=morok::model::train_g1_corpus(c,p);
  req(r.discovered_documents==21,"document discovery");req(r.unique_documents==20,"exact duplicate removal");
  req(r.training_documents==18&&r.validation_documents==1&&r.test_documents==1,"document split");
  req(!r.corpus_hash.empty()&&!r.epochs.empty(),"report metadata");
  req(std::isfinite(r.best_validation_bpb)&&std::isfinite(r.test_bpb),"finite BPB");
  req(r.test_target_bytes>0&&r.epochs[0].optimizer_updates>0,"training/evaluation did not run");
  req(std::isfinite(r.epochs[0].mean_gradient_norm)&&std::isfinite(r.epochs[0].max_gradient_norm),"gradient norm diagnostics must be finite");
  req(r.epochs[0].max_gradient_norm>=r.epochs[0].mean_gradient_norm,"maximum gradient norm must bound the mean");
 }catch(...){std::filesystem::remove_all(root);throw;}std::filesystem::remove_all(root);
}

void incremental_state_continuity(){
 morok::model::G1Parameters p;morok::model::initialize_g1_parameters(p,7);
 std::array<std::uint8_t,17> bytes{};for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(i*13U);
 morok::model::G1State zero{};
 auto first=morok::model::compute_g1_bptt_gradients(p,std::span<const std::uint8_t>(bytes.data(),9),8,zero);
 morok::model::G1State expected{};
 for(std::size_t i=0;i<8;++i)morok::model::advance_g1_state(p,expected,bytes[i]);
 for(std::size_t i=0;i<11;++i){
  req(std::abs(first.final_state.fast[i]-expected.fast[i])<1e-6F,"fast state mismatch at BPTT boundary");
  req(std::abs(first.final_state.slow[i]-expected.slow[i])<1e-6F,"slow state mismatch at BPTT boundary");
 }
 auto second=morok::model::compute_g1_bptt_gradients(p,std::span<const std::uint8_t>(bytes.data()+8,9),8,first.final_state);
 for(std::size_t i=8;i<16;++i)morok::model::advance_g1_state(p,expected,bytes[i]);
 for(std::size_t i=0;i<11;++i){
  req(std::abs(second.final_state.fast[i]-expected.fast[i])<1e-6F,"overlapped fast state mismatch");
  req(std::abs(second.final_state.slow[i]-expected.slow[i])<1e-6F,"overlapped slow state mismatch");
 }
}

void deterministic_resume(){
 auto root=std::filesystem::temp_directory_path()/"morok-g1-resume-test";
 auto ck=root/"resume.ckpt";auto log=root/"training.jsonl";std::filesystem::remove_all(root);std::filesystem::create_directories(root);
 try{
  for(int i=0;i<20;++i)write(root/("d"+std::to_string(i)+".txt"),"documento de treino "+std::to_string(i)+" com contexto repetido\n");
  morok::model::G1TrainingConfig full;full.corpus_directory=root;full.max_epochs=4;full.patience=10;full.truncation_length=8;full.best_model_path=root/"best-full.ckpt";
  morok::model::G1Parameters uninterrupted;morok::model::initialize_g1_parameters(uninterrupted,77);
  auto full_report=morok::model::train_g1_corpus(full,uninterrupted);
  auto first=full;first.max_epochs=2;first.checkpoint_path=ck;first.metrics_log_path=log;first.best_model_path.clear();
  morok::model::G1Parameters interrupted;morok::model::initialize_g1_parameters(interrupted,77);
  auto first_report=morok::model::train_g1_corpus(first,interrupted);
  req(first_report.epochs.size()==2&&std::filesystem::exists(ck),"checkpoint not saved at epoch boundary");
  auto resumed=full;resumed.resume_from_checkpoint=ck;resumed.checkpoint_path=ck;resumed.metrics_log_path=log;resumed.best_model_path=root/"best-resumed.ckpt";
  morok::model::G1Parameters resumed_parameters;
  auto resumed_report=morok::model::train_g1_corpus(resumed,resumed_parameters);
  req(resumed_report.resumed,"resume flag not set");
  req(resumed_report.epochs.size()==2&&resumed_report.epochs.front().epoch==3,"resume did not continue at next epoch");
  req(uninterrupted.flatten()==resumed_parameters.flatten(),"resumed training diverged from uninterrupted training");
  auto best_full=morok::model::load_g1_checkpoint(full.best_model_path);auto best_resumed=morok::model::load_g1_checkpoint(resumed.best_model_path);req(best_full.parameters.flatten()==best_resumed.parameters.flatten()&&!best_full.has_optimizer,"best inference checkpoint export mismatch");
  req(full_report.best_epoch==resumed_report.best_epoch,"best epoch differs after resume");
  std::ifstream lf(log);std::string logs((std::istreambuf_iterator<char>(lf)),std::istreambuf_iterator<char>());req(logs.find("\"event\":\"epoch\"")!=std::string::npos&&logs.find("\"event\":\"finished\"")!=std::string::npos,"persistent JSONL log events missing");
 }catch(...){std::filesystem::remove_all(root);throw;}
 std::filesystem::remove_all(root);
}

void invalid_utf8(){
 auto root=std::filesystem::temp_directory_path()/"morok-g1-invalid-test";std::filesystem::remove_all(root);std::filesystem::create_directories(root);
 try{
  write(root/"a.txt","valid");write(root/"b.txt","valid2");write(root/"c.txt","valid3");write(root/"bad.txt",std::string("\xC0\xAF",2));
  morok::model::G1TrainingConfig c;c.corpus_directory=root;morok::model::G1Parameters p;morok::model::initialize_g1_parameters(p,42);
  bool failed=false;try{(void)morok::model::train_g1_corpus(c,p);}catch(const std::runtime_error&){failed=true;}
  req(failed,"invalid UTF-8 accepted");
 }catch(...){std::filesystem::remove_all(root);throw;}std::filesystem::remove_all(root);
}
}
int main(){try{pipeline();incremental_state_continuity();deterministic_resume();invalid_utf8();std::cout<<"G1 training tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
