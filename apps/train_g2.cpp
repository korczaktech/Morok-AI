#include "morok/model/learn/g2_training.hpp"
#include "morok/model/learn/g2_checkpoint.hpp"
#include "morok/model/parameters/g2_parameters.hpp"
#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"uso: morok-train-g2 <corpus-dir> <checkpoint-prefix> [epochs] [seed]\n";return 2;}
    try {
        morok::model::G2TrainingConfig c; c.base.corpus_directory=argv[1]; c.checkpoint_prefix=argv[2];
        if(argc>3)c.base.max_epochs=static_cast<std::size_t>(std::stoull(argv[3]));
        if(argc>4)c.base.seed=std::stoull(argv[4]);
        morok::model::G2Parameters p; morok::model::initialize_g2_parameters(p,c.base.seed);
        const auto r=morok::model::train_g2_corpus(c,p);
        std::cout<<"generation=G2\ntrainable_parameters="<<p.parameter_count()
                 <<"\nexpert_a_validation_bpb="<<r.expert_a.best_validation_bpb
                 <<"\nexpert_b_validation_bpb="<<r.expert_b.best_validation_bpb
                 <<"\nexpert_a_test_bpb="<<r.expert_a.test_bpb
                 <<"\nexpert_b_test_bpb="<<r.expert_b.test_bpb<<"\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"morok-train-g2: "<<e.what()<<'\n';return 1;}
}
