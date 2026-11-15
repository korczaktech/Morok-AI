#include "morok/model/learn/g2_training.hpp"
#include "morok/model/parameters/g2_parameters.hpp"
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"uso: morok-train-g2 <corpus-dir> <checkpoint-prefix> [epochs] [seed] [--resume]\n";return 2;}
    try {
        morok::model::G2TrainingConfig c; c.base.corpus_directory=argv[1]; c.checkpoint_prefix=argv[2];
        bool epochs_seen=false,seed_seen=false;
        for(int i=3;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--resume") c.resume=true;
            else if(arg=="--patience") { if(i+1>=argc) throw std::invalid_argument("--patience requires a value"); c.base.patience=static_cast<std::size_t>(std::stoull(argv[++i])); if(c.base.patience==0||c.base.patience>100000) throw std::invalid_argument("--patience must be in [1, 100000]"); }
            else if(!epochs_seen) { c.base.max_epochs=static_cast<std::size_t>(std::stoull(arg)); epochs_seen=true; }
            else if(!seed_seen) { c.base.seed=std::stoull(arg); seed_seen=true; }
            else throw std::invalid_argument("argumento desconhecido: "+arg);
        }
        morok::model::G2Parameters p; morok::model::initialize_g2_parameters(p,c.base.seed);
        const auto r=morok::model::train_g2_corpus(c,p);
        std::cout<<"generation=G2\ntrainable_parameters="<<p.parameter_count()
                 <<"\nexpert_a_validation_bpb="<<r.expert_a.best_validation_bpb
                 <<"\nexpert_b_validation_bpb="<<r.expert_b.best_validation_bpb
                 <<"\nexpert_a_test_bpb="<<r.expert_a.test_bpb
                 <<"\nexpert_b_test_bpb="<<r.expert_b.test_bpb
                 <<"\nexpert_a_resumed="<<(r.expert_a.resumed?"true":"false")
                 <<"\nexpert_b_resumed="<<(r.expert_b.resumed?"true":"false")
                 <<"\nexpert_a_single_document_evaluation="<<(r.expert_a.single_document_evaluation?"true":"false")
                 <<"\nexpert_b_single_document_evaluation="<<(r.expert_b.single_document_evaluation?"true":"false")<<"\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"morok-train-g2: "<<e.what()<<'\n';return 1;}
}
