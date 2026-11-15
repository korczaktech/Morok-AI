#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/g1/tsukuyomi/profile.hpp"
#include "morok/g1/mokosh/profile.hpp"
#include "morok/g1/apollo/profile.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
struct Question { const char* id; const char* prompt; const char* expected; };
const std::vector<Question> questions{
 {"sky_is_blue","O céu é azul?\nResposta: ","azul"},
 {"grass_color","Que cor é a grama?\nResposta: ","verde"},
 {"mountains_high_or_low","As montanhas são baixas ou altas?\nResposta: ","altas"}
};
std::string clean(std::string s) { for(char& c:s) if(c=='\t'||c=='\n'||c=='\r') c=' '; return s; }
template<class Generate>
void run_profile(const char* name,const morok::model::G1Parameters& p,std::ofstream& out,Generate generate) {
 for(const auto& q:questions) {
  auto prompt=morok::common::Utf8Text::from_utf8(q.prompt);
  if(!prompt) throw std::runtime_error("invalid UTF-8 question");
  const auto start=std::chrono::steady_clock::now();
  const auto result=generate(p,*prompt.value_if());
  const auto stop=std::chrono::steady_clock::now();
  const double ms=std::chrono::duration<double,std::milli>(stop-start).count();
  const auto answer=result.continuation.string();
  const bool correct=answer.find(q.expected)!=std::string::npos;
  out<<name<<'\t'<<q.id<<'\t'<<(correct?"correct":"incorrect")<<'\t'<<ms<<'\t'<<clean(answer)<<'\n';
  std::cout<<name<<" | "<<q.id<<" | "<<(correct?"correct":"incorrect")<<" | "<<ms<<" ms | "<<clean(answer)<<'\n';
 }
}
}
int main(int argc,char** argv) {
 if(argc!=3){std::cerr<<"usage: morok-g1-profile-evaluate <checkpoint> <results.tsv>\n";return 2;}
 try {
  const auto checkpoint=morok::model::load_g1_checkpoint(argv[1]);
  std::ofstream out(argv[2],std::ios::trunc);
  if(!out) throw std::runtime_error("cannot open results file");
  out<<"profile\tquestion_id\tverdict\tlatency_ms\tanswer\n";
  run_profile("Tsukuyomi",checkpoint.parameters,out,[](const auto& p,const auto& q){return morok::g1::tsukuyomi::generate(p,q);});
  run_profile("Mokosh",checkpoint.parameters,out,[](const auto& p,const auto& q){return morok::g1::mokosh::generate(p,q);});
  run_profile("Apollo",checkpoint.parameters,out,[](const auto& p,const auto& q){return morok::g1::apollo::generate(p,q);});
  if(!out) throw std::runtime_error("failed writing results");
  return 0;
 } catch(const std::exception& e){std::cerr<<"profile-evaluate: "<<e.what()<<'\n';return 1;}
}