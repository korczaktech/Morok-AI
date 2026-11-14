#include "morok/lang/rlah/segmenter.hpp"
#include <cassert>
#include <string>
#include <iostream>
int main(){using namespace morok::lang::rlah;std::string s="abracadabra abracadabra abracadabra abracadabra ";std::vector<std::uint8_t>b(s.begin(),s.end());Segmenter seg;auto c=seg.evaluate(b);assert(c.size()<=256);auto z=encode(b,c);std::vector<std::uint8_t>o;std::string e;if(!decode(z.data,o,e)){std::cerr<<"RLAH decode failed: "<<e<<'\n';return 1;}assert(o==b);auto bad=z.data;bad.back()^=1;assert(!decode(bad,o,e));Scorer sc;std::vector<Candidate>a(16);std::vector<float>y(16,1);for(auto&x:a)x.bytes={1,2};assert(sc.train(a,y));}