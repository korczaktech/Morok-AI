#include "morok/model/learn/g1_checkpoint.hpp"
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <type_traits>
namespace morok::model { namespace {
using Bytes=std::vector<std::byte>;
constexpr std::array<char,8> magic{'M','O','R','O','K','C','K','P'};
constexpr std::array<char,32> arch=[] {std::array<char,32>a{};constexpr char s[]="RLAH-G1-fastslow-d11-bit8";for(std::size_t i=0;i<sizeof(s)-1;++i)a[i]=s[i];return a;}();
template<class T>void put(Bytes&b,T v){static_assert(std::is_integral_v<T>);using U=std::make_unsigned_t<T>;U x=static_cast<U>(v);for(std::size_t i=0;i<sizeof(T);++i)b.push_back(std::byte((x>>(i*8U))&0xffU));}
void putf(Bytes&b,float v){put(b,std::bit_cast<std::uint32_t>(v));}
template<class T>T get(std::span<const std::byte>b,std::size_t&p){static_assert(std::is_integral_v<T>);if(p+sizeof(T)>b.size())throw std::runtime_error("checkpoint truncado");using U=std::make_unsigned_t<T>;U x=0;for(std::size_t i=0;i<sizeof(T);++i)x|=U(std::to_integer<unsigned char>(b[p++]))<<(i*8U);return static_cast<T>(x);}
float getf(std::span<const std::byte>b,std::size_t&p){float x=std::bit_cast<float>(get<std::uint32_t>(b,p));if(!std::isfinite(x))throw std::runtime_error("checkpoint contém FP32 não finito");return x;}
void append(Bytes&b,std::span<const std::byte>x){b.insert(b.end(),x.begin(),x.end());}
void write_all(const std::filesystem::path&p,std::span<const std::byte>b){auto tmp=p;tmp+=".tmp";{std::ofstream f(tmp,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("não foi possível criar checkpoint temporário");f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));f.flush();if(!f)throw std::runtime_error("falha ao gravar checkpoint");}std::error_code ec;std::filesystem::rename(tmp,p,ec);if(ec){std::filesystem::remove(tmp);throw std::runtime_error("falha no rename atômico do checkpoint: "+ec.message());}}
}
std::uint32_t g1_crc32(std::span<const std::byte>d)noexcept{std::uint32_t c=0xffffffffU;for(auto v:d){c^=std::to_integer<std::uint8_t>(v);for(int i=0;i<8;++i)c=(c>>1U)^(0xedb88320U&(0U-(c&1U)));}return c^0xffffffffU;}
void save_g1_checkpoint(const std::filesystem::path&path,const G1Checkpoint&c){
 if(path.empty())throw std::invalid_argument("caminho de checkpoint vazio");if(c.metadata.size()>65536)throw std::invalid_argument("metadados excedem 64 KiB");if(c.document_order.size()>100000||c.document_position>c.document_order.size())throw std::invalid_argument("cursor de documentos inválido");if(c.has_rng&&!(c.rng_state[0]|c.rng_state[1]|c.rng_state[2]|c.rng_state[3]))throw std::invalid_argument("estado RNG totalmente zero");
 Bytes payload;for(float x:c.parameters.flatten()){if(!std::isfinite(x))throw std::invalid_argument("parâmetro não finito");putf(payload,x);}
 std::uint64_t step=0,optbytes=0;std::uint32_t flags=0;
 if(c.has_optimizer){if(std::holds_alternative<G1AdamWState>(c.optimizer)){flags|=1U;const auto&s=std::get<G1AdamWState>(c.optimizer);step=s.step;for(float x:s.first_moment)putf(payload,x);for(float x:s.second_moment){if(x<0)throw std::invalid_argument("segundo momento negativo");putf(payload,x);}optbytes=8000;}else{const auto&s=std::get<G1SGDMomentumState>(c.optimizer);step=s.step;for(float x:s.velocity)putf(payload,x);optbytes=4000;}}
 if(c.has_rng){flags|=2U;for(auto x:c.rng_state)put(payload,x);}
 if(!c.document_order.empty()||c.document_position||c.byte_offset){flags|=4U;put(payload,static_cast<std::uint32_t>(c.document_order.size()));for(auto x:c.document_order)put(payload,x);put(payload,c.document_position);put(payload,c.byte_offset);}
 append(payload,std::as_bytes(std::span(c.metadata.data(),c.metadata.size())));
 Bytes out;for(char x:magic)out.push_back(std::byte(x));put(out,std::uint16_t(1));put(out,std::uint16_t(104));put(out,flags);for(char x:arch)out.push_back(std::byte(x));put(out,std::uint32_t(1000));put(out,std::uint8_t(1));put(out,std::uint8_t(0));put(out,std::uint8_t(0));put(out,std::uint8_t(0));put(out,step);put(out,c.epoch);put(out,c.block_index);put(out,std::uint64_t(4000));put(out,optbytes);put(out,std::uint32_t(c.has_rng?32:0));put(out,static_cast<std::uint32_t>(c.metadata.size()));put(out,g1_crc32(payload));if(out.size()!=104)throw std::logic_error("header MOROKCKP não tem 104 bytes");append(out,payload);write_all(path,out);
}
G1Checkpoint load_g1_checkpoint(const std::filesystem::path&path){
 std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("não foi possível abrir checkpoint");auto n=f.tellg();if(n<104||n>static_cast<std::streamoff>(16*1024*1024))throw std::runtime_error("tamanho de checkpoint inválido");Bytes file(static_cast<std::size_t>(n));f.seekg(0);f.read(reinterpret_cast<char*>(file.data()),n);if(!f)throw std::runtime_error("falha ao ler checkpoint");auto b=std::span<const std::byte>(file);std::size_t p=0;
 for(char x:magic)if(get<std::uint8_t>(b,p)!=static_cast<std::uint8_t>(x))throw std::runtime_error("magic MOROKCKP inválido");if(get<std::uint16_t>(b,p)!=1||get<std::uint16_t>(b,p)!=104)throw std::runtime_error("versão/header MOROKCKP incompatível");auto flags=get<std::uint32_t>(b,p);if(flags&~7U)throw std::runtime_error("flags MOROKCKP desconhecidas");for(char x:arch)if(get<std::uint8_t>(b,p)!=static_cast<std::uint8_t>(x))throw std::runtime_error("architecture_id incompatível");if(get<std::uint32_t>(b,p)!=1000||get<std::uint8_t>(b,p)!=1)throw std::runtime_error("inventário/dtype incompatível");if(get<std::uint8_t>(b,p)||get<std::uint8_t>(b,p)||get<std::uint8_t>(b,p))throw std::runtime_error("reserved MOROKCKP não zero");
 auto step=get<std::uint64_t>(b,p);G1Checkpoint c;c.epoch=get<std::uint32_t>(b,p);c.block_index=get<std::uint64_t>(b,p);auto pb=get<std::uint64_t>(b,p),ob=get<std::uint64_t>(b,p);auto rb=get<std::uint32_t>(b,p),mb=get<std::uint32_t>(b,p),crc=get<std::uint32_t>(b,p);
 if(pb!=4000||mb>65536||rb!=((flags&2U)?32U:0U)||(flags&1U?ob!=8000U:(ob!=0U&&ob!=4000U)))throw std::runtime_error("comprimentos MOROKCKP inconsistentes");auto payload=b.subspan(104);if(g1_crc32(payload)!=crc)throw std::runtime_error("CRC32 MOROKCKP inválido");
 p=104;std::array<float,1000>params{};for(auto&x:params)x=getf(b,p);if(!c.parameters.assign(params))throw std::runtime_error("parâmetros inválidos");
 if(ob){c.has_optimizer=true;if(flags&1U){G1AdamWState s;s.step=step;for(auto&x:s.first_moment)x=getf(b,p);for(auto&x:s.second_moment){x=getf(b,p);if(x<0)throw std::runtime_error("segundo momento negativo");}c.optimizer=s;}else{G1SGDMomentumState s;s.step=step;for(auto&x:s.velocity)x=getf(b,p);c.optimizer=s;}}
 if(flags&2U){c.has_rng=true;for(auto&x:c.rng_state)x=get<std::uint64_t>(b,p);if(!(c.rng_state[0]|c.rng_state[1]|c.rng_state[2]|c.rng_state[3]))throw std::runtime_error("estado RNG inválido");}
 if(flags&4U){auto count=get<std::uint32_t>(b,p);if(count>100000)throw std::runtime_error("cursor excede limite");c.document_order.reserve(count);for(std::uint32_t i=0;i<count;++i)c.document_order.push_back(get<std::uint32_t>(b,p));c.document_position=get<std::uint32_t>(b,p);c.byte_offset=get<std::uint64_t>(b,p);if(c.document_position>count)throw std::runtime_error("posição de cursor inválida");}
 if(p+mb!=b.size())throw std::runtime_error("bytes extras/truncados após metadados");c.metadata.assign(reinterpret_cast<const char*>(b.data()+p),mb);return c;
}
} // namespace morok::model