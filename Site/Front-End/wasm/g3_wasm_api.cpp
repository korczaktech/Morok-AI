#include "morok/g3/neural/gru_backend.hpp"
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace {
using morok::g3::neural::G3Backend;
constexpr std::size_t kHeaderBytes=64,kPayloadBytes=400000,kStateBytes=660,kVocabulary=261;
constexpr std::uint16_t kEos=258,kPad=256,kBos=257,kUsr=259,kBot=260;
std::uint16_t u16(const std::vector<std::uint8_t>& b,std::size_t o){return static_cast<std::uint16_t>(b[o]|(static_cast<std::uint16_t>(b[o+1])<<8U));}
std::uint32_t u32(const std::vector<std::uint8_t>& b,std::size_t o){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=static_cast<std::uint32_t>(b[o+i])<<(8U*i);return v;}
std::uint64_t u64(const std::vector<std::uint8_t>& b,std::size_t o){std::uint64_t v=0;for(unsigned i=0;i<8;++i)v|=static_cast<std::uint64_t>(b[o+i])<<(8U*i);return v;}
std::uint32_t artifact_crc(const std::vector<std::uint8_t>& b){std::uint32_t crc=0xffffffffU;for(std::size_t i=0;i<b.size();++i){crc^=(i>=52&&i<56)?0U:b[i];for(unsigned bit=0;bit<8;++bit){const std::uint32_t mask=0U-(crc&1U);crc=(crc>>1U)^(0xedb88320U&mask);}}return ~crc;}
bool repeats(std::span<const std::uint16_t> h,std::uint16_t c){const std::size_t total=h.size()+1U;for(std::size_t n=3;n<=6&&n<=total/2U;++n){bool same=true;for(std::size_t j=0;j<n;++j){const std::size_t current=total-n+j;const auto a=current==h.size()?c:h[current];if(a!=h[total-2U*n+j]){same=false;break;}}if(same)return true;}return false;}
class TsukuyomiG3Runtime {
public:
 bool modelLoaded()const{return static_cast<bool>(backend_);}
 bool loadModel(const emscripten::val& source){
  try{
   const auto b=emscripten::convertJSArrayToNumberVector<std::uint8_t>(source);
   if(b.size()!=kHeaderBytes+kPayloadBytes||b[0]!='M'||b[1]!='R'||b[2]!='K'||b[3]!='3'||u16(b,4)!=2||u16(b,6)!=64||u16(b,8)!=3||u16(b,10)!=0x0302||u16(b,12)!=0||u32(b,16)!=261||u16(b,20)!=60||u16(b,22)!=97||u16(b,24)!=2||u16(b,26)!=68||u32(b,28)!=660||u64(b,32)!=100000||u64(b,40)!=kPayloadBytes||u32(b,48)!=0||u32(b,52)!=artifact_crc(b))return false;
   auto candidate=std::make_unique<G3Backend>();auto view=candidate->mutable_weights();
   const std::array<std::span<float>,11> tensors{view.embedding,view.layer1_input,view.layer1_recurrent,view.layer1_bias,view.layer2_input,view.layer2_recurrent,view.layer2_bias,view.rms_scale,view.projection,view.projection_bias,view.output_bias};
   std::size_t offset=kHeaderBytes;
   for(auto tensor:tensors)for(float& value:tensor){value=std::bit_cast<float>(u32(b,offset));if(!std::isfinite(value))return false;offset+=4;}
   if(offset!=b.size()||candidate->parameter_count()!=100000)return false;
   backend_=std::move(candidate);return true;
  }catch(...){return false;}
 }
 std::string generate(const std::string& prompt,std::size_t max_bytes,double temperature,std::size_t top_k,std::uint64_t seed)const{
  if(!backend_||prompt.empty()||prompt.size()>4096||max_bytes==0||max_bytes>1024||!std::isfinite(temperature)||temperature<0||top_k>kVocabulary)return {};
  std::array<std::byte,kStateBytes> state{};if(!backend_->reset_state(state))return {};
  for(unsigned char byte:prompt)if(!backend_->consume_byte(state,byte))return {};
  std::mt19937_64 random(seed);std::array<float,kVocabulary> logp{};std::array<std::pair<double,std::uint16_t>,kVocabulary> candidates{};
  std::vector<std::uint16_t> history;history.reserve(max_bytes);std::string answer;answer.reserve(max_bytes);
  for(std::size_t step=0;step<max_bytes;++step){
   if(!backend_->next_token_log_probabilities(state,logp))return {};
   std::size_t count=0;
   for(std::size_t i=0;i<kVocabulary;++i){const auto token=static_cast<std::uint16_t>(i);const double value=logp[i];if(token==kPad||token==kBos||token==kUsr||token==kBot||(std::isinf(value)&&value<0))continue;if(!std::isfinite(value))return {};if(token<256&&repeats(history,token))continue;candidates[count++]={temperature==0?value:value/temperature,token};}
   if(count==0)break;const std::size_t selected=top_k?std::min(top_k,count):count;
   std::partial_sort(candidates.begin(),candidates.begin()+selected,candidates.begin()+count,[](const auto&a,const auto&b){return a.first>b.first;});
   std::uint16_t token=candidates[0].second;
   if(temperature>0){const double maximum=candidates[0].first;std::array<double,kVocabulary> weights{};double total=0;for(std::size_t i=0;i<selected;++i){weights[i]=std::exp(candidates[i].first-maximum);total+=weights[i];}if(!(total>0)||!std::isfinite(total))return {};const double target=std::generate_canonical<double,53>(random)*total;double cumulative=0;for(std::size_t i=0;i<selected;++i){cumulative+=weights[i];if(target<cumulative){token=candidates[i].second;break;}}}
   if(token==kEos)break;if(token>=256)return {};answer.push_back(static_cast<char>(token));history.push_back(token);if(!backend_->consume_byte(state,static_cast<std::uint8_t>(token)))return {};
  }return answer;
 }
private:std::unique_ptr<G3Backend> backend_;
};
}
EMSCRIPTEN_BINDINGS(morok_tsukuyomi_g3_web){emscripten::class_<TsukuyomiG3Runtime>("TsukuyomiG3Runtime").constructor<>().function("modelLoaded",&TsukuyomiG3Runtime::modelLoaded).function("loadModel",&TsukuyomiG3Runtime::loadModel).function("generate",&TsukuyomiG3Runtime::generate);}
