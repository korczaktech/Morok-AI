#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/g1/tsukuyomi/profile.hpp"
#include "morok/g1/mokosh/profile.hpp"
#include "morok/g1/apollo/profile.hpp"
#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace {
struct Question { const char* id; const char* prompt; const char* expected; };
const std::vector<Question> questions{
 {"sky_is_blue","O céu é azul?\\nResposta: ","azul"},
 {"grass_color","Qual a cor da grama?\\nResposta: ","verde"},
 {"mountains_high_or_low","As montanhas são altas?\\nResposta: ","altas"}
};
std::string clean(std::string s) { for(char& c:s) if(c=='\\t'||c=='\\n'||c=='\\r') c=' '; return s; }
template<class Generate>
void run_profile(const char* name,const morok::model::G1Parameters& p,std::ofstream& out,
                 Generate generate,const std::vector<std::string>& selected,std::size_t attempts) {
 for(const auto& q:questions) {
  if(std::find(selected.begin(),selected.end(),q.id)==selected.end()) continue;
  auto prompt=morok::common::Utf8Text::from_utf8(q.prompt);
  if(!prompt) throw std::runtime_error("invalid UTF-8 question");
  for(std::size_t attempt=1;attempt<=attempts;++attempt) {
   const auto start=std::chrono::steady_clock::now();
   const auto result=generate(p,*prompt.value_if(),attempt);
   const auto stop=std::chrono::steady_clock::now();
   const double ms=std::chrono::duration<double,std::milli>(stop-start).count();
   const auto answer=result.continuation.string();
   std::string normalized=answer;
   for(char& ch:normalized) ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
   const bool correct=normalized.find(q.expected)!=std::string::npos;
   out<<name<<'\\t'<<q.id<<'\\t'<<attempt<<'\\t'<<(correct?"correct":"incorrect")<<'\\t'<<ms<<'\\t'<<clean(answer)<<'\\n';
   if(!out) throw std::runtime_error("failed writing evaluation row");
  }
 }
}
std::vector<std::string> split(const std::string& value) {
 std::vector<std::string> result; std::size_t begin=0;
 while(begin<value.size()){auto end=value.find(',',begin);auto item=value.substr(begin,end==std::string::npos?end:end-begin);if(!item.empty())result.push_back(item);if(end==std::string::npos)break;begin=end+1;}
 return result;
}
}
int main(int argc,char** argv) {
 if(argc!=6){std::cerr<<"usage: morok-g1-profile-evaluate <checkpoint> <results.tsv> <profile> <question-ids-csv> <attempts>\\n";return 2;}
 try {
  const std::string profile=argv[3];
  const auto selected=split(argv[4]);
  const auto attempts=static_cast<std::size_t>(std::stoull(argv[5]));
  if(attempts<1||attempts>1000) throw std::invalid_argument("attempts must be in 1..1000");
  if(selected.empty()) throw std::invalid_argument("at least one question is required");
  if(profile!="Tsukuyomi"&&profile!="Apollo"&&profile!="Mokosh") throw std::invalid_argument("unknown profile");
  for(const auto& id:selected) if(std::none_of(questions.begin(),questions.end(),[&](const auto& q){return id==q.id;})) throw std::invalid_argument("unknown question id: "+id);
  const auto checkpoint=morok::model::load_g1_checkpoint(argv[1]);
  std::ofstream out(argv[2],std::ios::app);
  if(!out) throw std::runtime_error("cannot open results file");
  if(std::filesystem::file_size(argv[2])==0) out<<"profile\\tquestion_id\\tattempt\\tverdict\\tlatency_ms\\tanswer\\n";
  if(profile=="Tsukuyomi") run_profile("Tsukuyomi",checkpoint.parameters,out,[](const auto& p,const auto& q,std::size_t attempt){morok::g1::tsukuyomi::Policy policy{};policy.seed+=attempt-1;return morok::g1::tsukuyomi::generate(p,q,policy);},selected,attempts);
  else if(profile=="Apollo") run_profile("Apollo",checkpoint.parameters,out,[](const auto& p,const auto& q,std::size_t attempt){morok::g1::apollo::Policy policy{};policy.seed+=attempt-1;return morok::g1::apollo::generate(p,q,policy);},selected,attempts);
  else run_profile("Mokosh",checkpoint.parameters,out,[](const auto& p,const auto& q,std::size_t attempt){morok::g1::mokosh::Policy policy{};policy.seed+=attempt-1;return morok::g1::mokosh::generate(p,q,policy);},selected,attempts);
  return 0;
 } catch(const std::exception& e){std::cerr<<"profile-evaluate: "<<e.what()<<'\\n';return 1;}
}