#include "morok/model/learn/g2_checkpoint.hpp"
#include "morok/model/learn/g2_generation.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    if(argc<4){std::cerr<<"uso: morok-generate-g2 <checkpoint-prefix> <prompt.txt> <saida.txt> [max-bytes]\n";return 2;}
    try {
        auto p=morok::model::load_g2_checkpoint(argv[1]);
        std::ifstream in(argv[2],std::ios::binary); if(!in)throw std::runtime_error("não foi possível abrir prompt");
        std::string raw((std::istreambuf_iterator<char>(in)),{}); auto text=morok::common::Utf8Text::from_utf8(raw);
        if(!text)throw std::runtime_error("prompt UTF-8 inválido");
        morok::model::G1GenerationConfig c;
        c.max_new_bytes=48;
        c.printable_only=true;
        c.repetition_penalty=1.25;
        c.repetition_window=24;
        c.eos_threshold=0.85;
        c.min_new_bytes=2;
        if(argc>4)c.max_new_bytes=static_cast<std::size_t>(std::stoull(argv[4]));
        auto result=morok::model::generate_g2(p,*text.value_if(),c);
        std::ofstream out(argv[3],std::ios::binary|std::ios::trunc); if(!out)throw std::runtime_error("não foi possível criar saída");
        out.write(result.continuation.string().data(),static_cast<std::streamsize>(result.continuation.size_bytes()));
        if(!out)throw std::runtime_error("falha ao gravar saída");
        std::cout<<"generation=G2\ngenerated_bytes="<<result.generated_bytes<<"\ncandidate_evaluations="<<result.candidate_evaluations<<"\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"morok-generate-g2: "<<e.what()<<'\n';return 1;}
}
