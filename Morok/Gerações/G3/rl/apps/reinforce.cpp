#include "morok/g3/rl/g3_policy.hpp"
#include "morok/g3/rl/reference_evaluator.hpp"
#include "morok/g3/training/dataset.hpp"
#include "morok/g3/training/evaluation.hpp"
#include "morok/g3/inference/model_artifact.hpp"
#include "morok/lang/byte_tokenizer.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using morok::lang::TokenId;
std::vector<TokenId> encode_prompt(const std::string& question) {
    const auto encoded=morok::lang::ByteTokenizer::encode_text(question);
    if(!encoded) throw std::runtime_error("Could not encode RL question");
    std::vector<TokenId> tokens{morok::lang::kBosToken,morok::lang::kUsrToken};
    tokens.insert(tokens.end(),encoded.value_if()->byte_tokens.begin(),
                  encoded.value_if()->byte_tokens.end());
    tokens.push_back(morok::lang::kBotToken);
    return tokens;
}
std::vector<TokenId> encode_answer(const std::string& answer) {
    const auto encoded=morok::lang::ByteTokenizer::encode_text(answer);
    if(!encoded) throw std::runtime_error("Could not encode reference answer");
    return encoded.value_if()->byte_tokens;
}
morok::g3::rl::Prompt make_prompt(const morok::g3::training::DatasetExample& e) {
    return {e.example_id,e.category,encode_prompt(e.question)};
}
void save_fp32(const std::filesystem::path& path,
               const morok::g3::neural::G3Backend& backend) {
    using namespace morok::g3;
    inference::ModelArtifact artifact;
    artifact.metadata.state_size_bytes=backend.descriptor().state_size_bytes;
    artifact.metadata.parameter_count=backend.parameter_count();
    const auto& w=backend.weights();
    const std::vector<const std::vector<float>*> tensors{
        &w.embedding,&w.layer1_input,&w.layer1_recurrent,&w.layer1_bias,
        &w.layer2_input,&w.layer2_recurrent,&w.layer2_bias,&w.rms_scale,
        &w.projection,&w.projection_bias,&w.output_bias};
    artifact.payload.reserve(static_cast<std::size_t>(backend.parameter_count())*sizeof(float));
    for(const auto* tensor:tensors) for(float value:*tensor) {
        const std::uint32_t bits=std::bit_cast<std::uint32_t>(value);
        for(unsigned shift=0;shift<32;shift+=8)
            artifact.payload.push_back(static_cast<std::byte>((bits>>shift)&0xffU));
    }
    const auto saved=inference::save_model_artifact_atomic(path,artifact);
    if(!saved) throw std::runtime_error("Could not save RL model artifact");
}
void write_report(const std::filesystem::path& path,
                  const morok::g3::rl::ComparisonReport& comparison,
                  const morok::g3::rl::TrainingReport& training,
                  std::size_t epochs) {
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::trunc);
    if(!out) throw std::runtime_error("Could not create G3 RL report");
    out<<std::setprecision(12)
       <<"{\n  \"schema\": \"morok-g3-rl-report-v1\",\n"
       <<"  \"epochs\": "<<epochs<<",\n"
       <<"  \"candidates\": "<<training.candidates<<",\n"
       <<"  \"optimizer_updates\": "<<training.optimizer_updates<<",\n"
       <<"  \"mean_training_reward\": "<<training.mean_reward<<",\n"
       <<"  \"mean_kl\": "<<training.mean_kl<<",\n"
       <<"  \"reward_hacking_suspicions\": "<<training.reward_hacking_suspicions<<",\n"
       <<"  \"reward_hacking_suspicion_rate\": "<<training.reward_hacking_suspicion_rate<<",\n"
       <<"  \"sft\": {\"mean_reward\": "<<comparison.reference_sft.mean_reward
       <<", \"mean_response_tokens\": "<<comparison.reference_sft.mean_response_tokens
       <<", \"empty_rate\": "<<comparison.reference_sft.empty_response_rate
       <<", \"repeated_rate\": "<<comparison.reference_sft.repeated_response_rate<<"},\n"
       <<"  \"rl\": {\"mean_reward\": "<<comparison.reinforcement_policy.mean_reward
       <<", \"mean_response_tokens\": "<<comparison.reinforcement_policy.mean_response_tokens
       <<", \"empty_rate\": "<<comparison.reinforcement_policy.empty_response_rate
       <<", \"repeated_rate\": "<<comparison.reinforcement_policy.repeated_response_rate<<"},\n"
       <<"  \"reward_delta\": "<<comparison.reward_delta<<"\n}\n";
    if(!out) throw std::runtime_error("Could not finish G3 RL report");
}
}
int main(int argc,char** argv) {
    if(argc<5 || argc>6) {
        std::cerr<<"Uso: morok-g3-reinforce <dataset.jsonl> <sft.morok> "
                    "<rl-output.morok> <report.json> [epochs]\n";
        return 2;
    }
    try {
        const std::size_t epochs=argc==6?static_cast<std::size_t>(std::stoull(argv[5])):1U;
        if(epochs==0U || epochs>100U) throw std::invalid_argument("epochs must be in [1,100]");
        const auto dataset=morok::g3::training::load_jsonl(argv[1]);
        const auto splits=morok::g3::training::split_by_group_id(dataset);
        morok::g3::neural::G3Backend sft_backend;
        (void)morok::g3::training::load_fp32_model_weights(argv[2],sft_backend);
        morok::g3::neural::G3Backend rl_backend=sft_backend;
        morok::g3::rl::G3PolicyAdapter sft_policy(sft_backend,"g3-sft-reference");
        morok::g3::rl::G3PolicyAdapter rl_policy(rl_backend,"g3-reinforcement-policy");
        std::vector<morok::g3::rl::ReferenceAnswer> references;
        references.reserve(dataset.examples.size());
        for(const auto& e:dataset.examples)
            references.push_back({encode_prompt(e.question),encode_answer(e.answer)});
        morok::g3::rl::ReferenceRewardEvaluator evaluator(std::move(references));
        std::vector<morok::g3::rl::Prompt> train_prompts,test_prompts;
        for(auto i:splits.train) if(!dataset.examples[i].sensitive)
            train_prompts.push_back(make_prompt(dataset.examples[i]));
        for(auto i:splits.test) if(!dataset.examples[i].sensitive)
            test_prompts.push_back(make_prompt(dataset.examples[i]));
        if(train_prompts.empty() || test_prompts.empty())
            throw std::runtime_error("RL needs non-sensitive train and test examples");
        const auto& first=dataset.examples[splits.train.front()];
        const auto first_prompt=encode_prompt(first.question);
        const auto first_answer=encode_answer(first.answer);
        const std::vector<morok::g3::rl::RewardAuditCase> audit{
            {"reference-exact",first_prompt,first_answer,0.90,1.0},
            {"empty-answer",first_prompt,{},-1.0,-0.50}};
        morok::g3::rl::Config config;
        config.candidates_per_prompt=4U;
        config.max_response_tokens=128U;
        config.top_k=10U;
        config.temperature=0.7;
        config.gamma=0.95;
        config.kl_beta=0.02;
        config.seed=42U;
        morok::g3::rl::ReinforceTrainer trainer(config);
        morok::g3::rl::TrainingReport latest;
        for(std::size_t epoch=0;epoch<epochs;++epoch) {
            const auto result=trainer.train_batch(train_prompts,rl_policy,sft_policy,evaluator,audit);
            if(!result) throw std::runtime_error("G3 REINFORCE training batch failed");
            latest=*result.value_if();
        }
        const auto comparison=morok::g3::rl::compare_sft_and_rl(
            sft_policy,rl_policy,evaluator,test_prompts,128U);
        if(!comparison) throw std::runtime_error("G3 SFT/RL comparison failed");
        save_fp32(argv[3],rl_backend);
        write_report(argv[4],*comparison.value_if(),latest,epochs);
        std::cout<<"G3 RL complete. Parameters: "<<rl_backend.parameter_count()
                 <<". SFT reward: "<<comparison.value_if()->reference_sft.mean_reward
                 <<". RL reward: "<<comparison.value_if()->reinforcement_policy.mean_reward
                 <<". Delta: "<<comparison.value_if()->reward_delta
                 <<". Report: "<<argv[4]<<"\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"Falha no RL G3: "<<e.what()<<"\n";
        return 1;
    }
}
