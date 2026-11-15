#include "morok/model/g2_forward.hpp"
#include "morok/model/learn/g2_checkpoint.hpp"
#include "morok/model/learn/g2_generation.hpp"
#include "morok/model/learn/g2_training.hpp"
#include "morok/model/parameters/g2_parameters.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
int main() {
    const auto dir=std::filesystem::temp_directory_path()/"morok-g2-test";
    try {
        using namespace morok::model;
        std::filesystem::remove_all(dir);
        static_assert(G2Parameters::parameter_count()==2000);
        G2Parameters p; initialize_g2_parameters(p,42);
        const auto flat=p.flatten();
        if(flat.size()!=2000)throw std::runtime_error("G2 inventory must contain exactly 2000 values");
        G2Parameters q; if(!q.assign(flat)||q.flatten()!=flat)throw std::runtime_error("G2 flatten roundtrip failed");
        G2State state; advance_g2_state(p,state,static_cast<std::uint8_t>('A'));
        const auto prediction=predict_g2_next_byte(p,state);
        for(double probability:prediction.bit_probabilities)
            if(!std::isfinite(probability)||probability<0.0||probability>1.0)
                throw std::runtime_error("invalid G2 prediction probability");
        const auto evaluation=evaluate_g2_next_byte(p,state,static_cast<std::uint8_t>('B'));
        if(!(evaluation.negative_log_likelihood_nats>=0.0))throw std::runtime_error("invalid G2 loss");
        std::filesystem::create_directories(dir);
        const auto prefix=dir/"model";
        save_g2_checkpoint(prefix,p); auto loaded=load_g2_checkpoint(prefix);
        if(loaded.flatten()!=p.flatten())throw std::runtime_error("G2 checkpoint roundtrip failed");
        auto prompt=morok::common::Utf8Text::from_utf8("Q");
        if(!prompt)throw std::runtime_error("test prompt is not valid UTF-8");
        G1GenerationConfig generation; generation.max_new_bytes=2; generation.min_new_bytes=2;
        const auto generated=generate_g2(p,*prompt.value_if(),generation);
        if(generated.generated_bytes>2)throw std::runtime_error("G2 generation exceeded its byte bound");
        const auto corpus=dir/"corpus"; std::filesystem::create_directories(corpus);
        {std::ofstream f(corpus/"doc-01.txt");f<<"Usuario: O ceu e azul?\nIA: Sim, o ceu e azul";}
        {std::ofstream f(corpus/"doc-02.txt");f<<"Usuario: As montanhas sao altas ou baixas?\nIA: As montanhas sao altas.";}
        {std::ofstream f(corpus/"doc-03.txt");f<<"Usuario: Que cor e a grama?\nIA: A grama e verde.";}
        G2TrainingConfig training; training.base.corpus_directory=corpus;
        training.base.max_epochs=1; training.base.patience=5; training.base.truncation_length=128;
        training.base.train_all_documents=true;
        training.checkpoint_prefix=dir/"trained";
        G2Parameters trained; initialize_g2_parameters(trained,7);
        const auto report=train_g2_corpus(training,trained);
        if(report.expert_a.best_epoch!=1||report.expert_b.best_epoch!=1)
            throw std::runtime_error("both G2 experts must complete one training epoch");
        if(report.expert_a.training_documents!=3||report.expert_b.training_documents!=3)
            throw std::runtime_error("both G2 experts must train on all three independent QA documents");
        if(!report.expert_a.training_data_evaluation||!report.expert_b.training_data_evaluation)
            throw std::runtime_error("G2 smoke training must report that evaluation reuses training documents");
        if(report.expert_a.single_document_evaluation||report.expert_b.single_document_evaluation)
            throw std::runtime_error("three QA documents must not be mislabeled as a single-document corpus");
        if(load_g2_checkpoint(training.checkpoint_prefix).flatten()!=trained.flatten())
            throw std::runtime_error("trained G2 checkpoint does not match trained parameters");
        training.base.max_epochs=2; training.resume=true;
        const auto resumed=train_g2_corpus(training,trained);
        if(!resumed.expert_a.resumed||!resumed.expert_b.resumed)
            throw std::runtime_error("both G2 experts must resume their own training checkpoints");
        std::filesystem::remove_all(dir);
    } catch(const std::exception& e){std::filesystem::remove_all(dir);std::cerr<<"G2 test failed: "<<e.what()<<'\n';return 1;}
    return 0;
}
