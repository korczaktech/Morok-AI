#include "morok/model/learn/g2_checkpoint.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include <stdexcept>
namespace morok::model {
void save_g2_checkpoint(const std::filesystem::path& path,const G2Parameters& p) {
    if(path.empty()) throw std::invalid_argument("G2 checkpoint path is empty");
    G1Checkpoint a; a.parameters=p.expert_a; a.metadata="kind=inference\ngeneration=G2\nexpert=A\ntrainable_parameters=2000";
    G1Checkpoint b; b.parameters=p.expert_b; b.metadata="kind=inference\ngeneration=G2\nexpert=B\ntrainable_parameters=2000";
    save_g1_checkpoint(path.string()+".expert-a",a);
    save_g1_checkpoint(path.string()+".expert-b",b);
}
G2Parameters load_g2_checkpoint(const std::filesystem::path& path) {
    auto a=load_g1_checkpoint(path.string()+".expert-a");
    auto b=load_g1_checkpoint(path.string()+".expert-b");
    if(a.has_optimizer||b.has_optimizer||a.metadata.find("generation=G2")==std::string::npos||
       b.metadata.find("generation=G2")==std::string::npos||
       a.metadata.find("expert=A")==std::string::npos||b.metadata.find("expert=B")==std::string::npos)
        throw std::runtime_error("checkpoint pair is not a compatible G2 inference model");
    G2Parameters p; p.expert_a=a.parameters; p.expert_b=b.parameters; return p;
}
}
