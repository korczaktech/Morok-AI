#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace morok::lang::rlah {
struct Candidate { std::vector<std::uint8_t> bytes; std::array<float,22> features{}; std::uint64_t frequency{}; float predictive{},compression{},utility{},score{}; bool active{},reversible{true}; };
struct Weights { float frequency=.10F,predictive=.40F,compression=.25F,preservation=.25F; };
struct Config { std::size_t min_frequency=4,max_candidates=256,max_active=64,max_dictionary=64;float create=.60F,keep=.50F; };
struct Scorer { std::array<float,29> p{},m{},v{};std::uint64_t step{};float score(const Candidate&)const noexcept;bool train(std::span<const Candidate>,std::span<const float>) noexcept; };
class Segmenter { Config c_;Weights w_;Scorer s_;std::vector<std::vector<std::uint8_t>> active_;public:explicit Segmenter(Config c={}):c_(c){};std::vector<Candidate> evaluate(std::span<const std::uint8_t>,std::span<const float> fast_state={});bool train_scorer(std::span<const Candidate>a,std::span<const float>b){return s_.train(a,b);}const Weights& weights()const{return w_;}const Scorer& scorer()const{return s_;}};
struct Encoded {std::vector<std::uint8_t> data;bool compressed{};};
std::uint32_t crc32(std::span<const std::uint8_t>) noexcept;
Encoded encode(std::span<const std::uint8_t>,std::span<const Candidate>);
bool decode(std::span<const std::uint8_t>,std::vector<std::uint8_t>&,std::string&);
}