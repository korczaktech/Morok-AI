#include "morok/model/learn/g1_training.hpp"
#include "morok/model/parameters/g1_parameters.hpp"
#include <exception>
#include <iomanip>
#include <iostream>
int main(int argc,char** argv){
 if(argc<2||argc>4){std::cerr<<"usage: morok_train <corpus-directory> [epochs] [seed]\n";return 2;}
 try{
  morok::model::G1TrainingConfig c;c.corpus_directory=argv[1];
  if(argc>2)c.max_epochs=std::stoull(argv[2]);if(argc>3)c.seed=std::stoull(argv[3]);
  morok::model::G1Parameters p;morok::model::initialize_g1_parameters(p,c.seed);
  auto r=morok::model::train_g1_corpus(c,p);std::cout<<std::setprecision(10);
  std::cout<<"{\"type\":\"summary\",\"documents\":"<<r.unique_documents<<",\"train\":"<<r.training_documents<<",\"validation\":"<<r.validation_documents<<",\"test\":"<<r.test_documents<<",\"corpus_bytes\":"<<r.corpus_bytes<<",\"corpus_hash\":\""<<r.corpus_hash<<"\",\"best_epoch\":"<<r.best_epoch<<",\"validation_bpb\":"<<r.best_validation_bpb<<",\"test_bpb\":"<<r.test_bpb<<"}\n";
  for(const auto& e:r.epochs)std::cout<<"{\"type\":\"epoch\",\"epoch\":"<<e.epoch<<",\"train_bpb\":"<<e.training_bpb<<",\"validation_bpb\":"<<e.validation_bpb<<",\"updates\":"<<e.optimizer_updates<<"}\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"morok_train: "<<e.what()<<'\n';return 1;}
}
