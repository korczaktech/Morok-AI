#include "morok/model/learn/g1_training.hpp"
#include "morok/model/parameters/g1_parameters.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
void req(bool x,const char* m){if(!x)throw std::runtime_error(m);}
void write(const std::filesystem::path& p,const std::string& s){std::ofstream f(p,std::ios::binary);f.write(s.data(),s.size());if(!f)throw std::runtime_error("test file write failed");}
void pipeline(){
 auto root=std::filesystem::temp_directory_path()/"morok-g1-trainer-test";std::filesystem::remove_all(root);std::filesystem::create_directories(root);
 try{
  for(int i=0;i<20;++i)write(root/("d"+std::to_string(i)+".txt"),"texto de teste "+std::to_string(i)+"\n");
  morok::model::G1TrainingConfig c;c.corpus_directory=root;c.max_epochs=2;c.patience=2;c.truncation_length=8;
  morok::model::G1Parameters p;morok::model::initialize_g1_parameters(p,42);auto r=morok::model::train_g1_corpus(c,p);
  req(r.discovered_documents==20,"document discovery");req(r.unique_documents==20,"deduplication");
  req(r.training_documents==18&&r.validation_documents==1&&r.test_documents==1,"document split");
  req(!r.corpus_hash.empty()&&!r.epochs.empty(),"report metadata");
  req(std::isfinite(r.best_validation_bpb)&&std::isfinite(r.test_bpb),"finite BPB");
  req(r.test_target_bytes>0&&r.epochs[0].optimizer_updates>0,"training/evaluation did not run");
 }catch(...){std::filesystem::remove_all(root);throw;}std::filesystem::remove_all(root);
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
int main(){try{pipeline();invalid_utf8();std::cout<<"G1 training tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
