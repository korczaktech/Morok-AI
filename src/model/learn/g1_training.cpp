#include "morok/model/learn/g1_training.hpp"
#include "morok/model/learn/g1_bptt.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <unordered_map>
namespace morok::model { namespace {
constexpr std::size_t kRead=64U*1024U;
constexpr double kLn2=0.693147180559945309417232121458176568;
constexpr std::uint64_t kOffset=14695981039346656037ULL, kPrime=1099511628211ULL;
enum class Split { train, valid, test };
struct Doc { std::filesystem::path path; std::uint64_t hash{}, size{}; Split split{Split::train}; };
class Utf8 {
 unsigned left{}; std::uint32_t cp{}, min{};
public:
 bool feed(std::uint8_t b) {
  if(!left) {
   if(b<=0x7f) return true;
   if(b>=0xc2&&b<=0xdf){left=1;cp=b&0x1f;min=0x80;return true;}
   if(b>=0xe0&&b<=0xef){left=2;cp=b&0x0f;min=0x800;return true;}
   if(b>=0xf0&&b<=0xf4){left=3;cp=b&7;min=0x10000;return true;}
   return false;
  }
  if((b&0xc0)!=0x80) return false;
  cp=(cp<<6)|(b&0x3f); --left;
  return left || (cp>=min && !(cp>=0xd800&&cp<=0xdfff) && cp<=0x10ffff);
 }
 bool complete() const {return left==0;}
};
Doc scan(const std::filesystem::path& p,std::uint64_t limit) {
 std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("cannot open "+p.string());
 std::array<char,kRead> buf{}; Utf8 utf; Doc d; d.path=p; d.hash=kOffset;
 while(f){f.read(buf.data(),buf.size()); auto n=static_cast<std::size_t>(f.gcount());
  for(std::size_t i=0;i<n;++i){auto b=static_cast<std::uint8_t>(static_cast<unsigned char>(buf[i]));
   if(!utf.feed(b)) throw std::runtime_error("invalid UTF-8 in "+p.string()+" at byte "+std::to_string(d.size+i));
   d.hash^=b; d.hash*=kPrime;
  }
  d.size+=n; if(d.size>limit) throw std::runtime_error("document exceeds byte limit: "+p.string());
 }
 if(!f.eof()) throw std::runtime_error("I/O error reading "+p.string());
 if(!utf.complete()) throw std::runtime_error("truncated UTF-8 at end of "+p.string());
 return d;
}
bool equal_files(const std::filesystem::path& a,const std::filesystem::path& b){
 std::ifstream x(a,std::ios::binary),y(b,std::ios::binary); if(!x||!y) throw std::runtime_error("cannot compare corpus files");
 std::array<char,kRead> p{},q{};
 for(;;){x.read(p.data(),p.size());y.read(q.data(),q.size());auto n=x.gcount(),m=y.gcount();
  if(n!=m||!std::equal(p.begin(),p.begin()+n,q.begin())) return false;
  if(!n) return x.eof()&&y.eof();
 }
}
std::vector<Doc> discover(const G1TrainingConfig& c,std::size_t& count){
 if(!std::filesystem::is_directory(c.corpus_directory)) throw std::invalid_argument("corpus_directory must be a directory");
 std::vector<std::filesystem::path> paths;
 for(const auto& e:std::filesystem::recursive_directory_iterator(c.corpus_directory))
  if(e.is_regular_file()&&e.path().extension()==".txt"){paths.push_back(e.path());if(paths.size()>c.max_documents)throw std::runtime_error("max_documents exceeded");}
 std::sort(paths.begin(),paths.end()); count=paths.size();
 std::vector<Doc> docs; std::unordered_map<std::uint64_t,std::vector<std::size_t>> seen;
 for(const auto& p:paths){auto d=scan(p,c.max_document_bytes);bool duplicate=false;
  auto it=seen.find(d.hash);if(it!=seen.end())for(auto i:it->second)
   if(docs[i].size==d.size&&equal_files(docs[i].path,p)){duplicate=true;break;}
  if(!duplicate){seen[d.hash].push_back(docs.size());docs.push_back(std::move(d));}
 }
 if(docs.size()<3) throw std::runtime_error("at least three unique documents are required");
 return docs;
}
std::string corpus_hash(const std::vector<Doc>& docs){
 std::uint64_t h=kOffset;for(const auto& d:docs)for(auto v:{d.hash,d.size})for(int i=0;i<8;++i){h^=static_cast<std::uint8_t>(v&255);h*=kPrime;v>>=8;}
 constexpr char hex[]="0123456789abcdef";std::string s(16,'0');for(int i=15;i>=0;--i){s[i]=hex[h&15];h>>=4;}return s;
}
void split(std::vector<Doc>& d,std::uint64_t seed){
 std::vector<std::size_t> ix(d.size());for(std::size_t i=0;i<ix.size();++i)ix[i]=i;
 std::mt19937_64 rng(seed);std::shuffle(ix.begin(),ix.end(),rng);
 auto train=d.size()>=20?d.size()*90/100:d.size()-2;
 auto valid=d.size()>=20?std::max<std::size_t>(1,d.size()*5/100):1;
 if(!train||train+valid>=d.size())throw std::runtime_error("cannot form all corpus splits");
 for(std::size_t i=0;i<ix.size();++i)d[ix[i]].split=i<train?Split::train:(i<train+valid?Split::valid:Split::test);
}
struct Eval{double bpb;std::uint64_t count;};
Eval evaluate(const std::vector<Doc>& docs,Split split,const G1Parameters& p){
 double nats=0;std::uint64_t count=0;std::array<char,kRead> buf{};
 for(const auto& d:docs)if(d.split==split){std::ifstream f(d.path,std::ios::binary);if(!f)throw std::runtime_error("cannot reopen "+d.path.string());
  G1State state{};bool has=false;std::uint8_t prev=0;
  while(f){f.read(buf.data(),buf.size());auto n=static_cast<std::size_t>(f.gcount());
   for(std::size_t i=0;i<n;++i){auto b=static_cast<std::uint8_t>(static_cast<unsigned char>(buf[i]));
    if(has){advance_g1_state(p,state,prev);nats+=evaluate_g1_next_byte(p,state,b).negative_log_likelihood_nats;++count;}
    prev=b;has=true;
   }
  }if(!f.eof())throw std::runtime_error("evaluation I/O error");
 }
 if(!count)throw std::runtime_error("evaluation split has no next-byte targets");
 return {nats/(static_cast<double>(count)*kLn2),count};
}
struct Totals{double nats{};std::uint64_t targets{},updates{};double norm{};};
void train_doc(const Doc& d,const G1TrainingConfig& c,G1Parameters& p,G1OptimizerState& opt,Totals& t,std::size_t& high,std::size_t& zero){
 std::ifstream f(d.path,std::ios::binary);if(!f)throw std::runtime_error("cannot reopen "+d.path.string());
 std::vector<std::uint8_t> w;w.reserve(c.truncation_length+1);char ch;
 while(w.size()<c.truncation_length+1&&f.get(ch))w.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
 G1State state{};
 while(w.size()>=2){
  auto r=compute_g1_bptt_gradients(p,w,c.truncation_length,state);
  auto step=apply_g1_optimizer_step(p,r.gradients,c.optimizer,opt);
  t.nats+=r.mean_loss_nats*r.target_byte_count;t.targets+=r.target_byte_count;++t.updates;t.norm=step.gradient_norm_before_clipping;
  high=step.gradient_norm_before_clipping>100?high+1:0;
  zero=step.gradient_norm_before_clipping==0?zero+1:0;
  if(high>=10)throw std::runtime_error("gradient norm exceeded 100 for 10 updates");
  if(zero>=100)throw std::runtime_error("gradient norm was zero for 100 updates");
  state=r.final_state;if(w.size()<c.truncation_length+1)break;
  auto overlap=w.back();w.clear();w.push_back(overlap);
  while(w.size()<c.truncation_length+1&&f.get(ch))w.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
 }
 if(!f.eof())throw std::runtime_error("training I/O error");
}
} // namespace
G1TrainingReport train_g1_corpus(const G1TrainingConfig& c,G1Parameters& p){
 if(!c.max_epochs||c.max_epochs>50||!c.patience||!c.truncation_length||!c.max_documents||!c.max_document_bytes)
  throw std::invalid_argument("invalid G1 training limits");
 G1TrainingReport out;auto docs=discover(c,out.discovered_documents);out.unique_documents=docs.size();
 for(const auto& d:docs){if(d.size>std::numeric_limits<std::uint64_t>::max()-out.corpus_bytes)throw std::overflow_error("corpus size overflow");out.corpus_bytes+=d.size;}
 out.corpus_hash=corpus_hash(docs);split(docs,c.seed);
 std::vector<std::size_t> train;
 for(std::size_t i=0;i<docs.size();++i){if(docs[i].split==Split::train){++out.training_documents;train.push_back(i);}else if(docs[i].split==Split::valid)++out.validation_documents;else ++out.test_documents;}
 std::mt19937_64 rng(c.seed^0x9e3779b97f4a7c15ULL);auto opt=make_g1_optimizer_state(c.optimizer.kind);
 auto best_p=p;auto best_opt=opt;double best=std::numeric_limits<double>::infinity();std::size_t stale=0,high=0,zero=0;
 for(std::size_t ep=1;ep<=c.max_epochs;++ep){
  std::shuffle(train.begin(),train.end(),rng);Totals t;
  for(auto i:train)train_doc(docs[i],c,p,opt,t,high,zero);
  if(!t.targets)throw std::runtime_error("training split has no targets");
  auto v=evaluate(docs,Split::valid,p);G1EpochMetrics m;
  m.epoch=ep;m.training_target_bytes=t.targets;m.optimizer_updates=t.updates;
  m.training_bpb=t.nats/(static_cast<double>(t.targets)*kLn2);m.validation_bpb=v.bpb;m.last_gradient_norm=t.norm;out.epochs.push_back(m);
  if(v.bpb<best){best=v.bpb;out.best_epoch=ep;best_p=p;best_opt=opt;stale=0;}
  else if(++stale>=c.patience){out.stopped_early=true;break;}
 }
 p=best_p;opt=best_opt;out.best_validation_bpb=best;
 if(out.test_documents){auto test=evaluate(docs,Split::test,p);out.test_bpb=test.bpb;out.test_target_bytes=test.count;}
 return out;
}
} // namespace morok::model
