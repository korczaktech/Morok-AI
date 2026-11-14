#include "morok/model/learn/g2_training.hpp"
#include "morok/model/learn/g2_checkpoint.hpp"
#include <stdexcept>
namespace morok::model {
G2TrainingReport train_g2_corpus(const G2TrainingConfig& config,G2Parameters& p) {
    if(config.checkpoint_prefix.empty()) throw std::invalid_argument("G2 checkpoint_prefix is required");
    auto a=config.base; a.seed=config.base.seed; a.checkpoint_path=config.checkpoint_prefix.string()+".expert-a.training";
    a.best_model_path=config.checkpoint_prefix.string()+".expert-a.inference";
    if(!config.base.resume_from_checkpoint.empty()) a.resume_from_checkpoint=a.checkpoint_path;
    if(!config.base.metrics_log_path.empty()) a.metrics_log_path=config.base.metrics_log_path.string()+".expert-a";
    auto b=config.base; b.seed=config.base.seed ^ 0x9E3779B97F4A7C15ULL;
    b.checkpoint_path=config.checkpoint_prefix.string()+".expert-b.training";
    b.best_model_path=config.checkpoint_prefix.string()+".expert-b.inference";
    if(!config.base.resume_from_checkpoint.empty()) b.resume_from_checkpoint=b.checkpoint_path;
    if(!config.base.metrics_log_path.empty()) b.metrics_log_path=config.base.metrics_log_path.string()+".expert-b";
    G2TrainingReport report;
    report.expert_a=train_g1_corpus(a,p.expert_a);
    report.expert_b=train_g1_corpus(b,p.expert_b);
    save_g2_checkpoint(config.checkpoint_prefix,p);
    return report;
}
}
