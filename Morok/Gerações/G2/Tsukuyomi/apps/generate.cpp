#include "morok/g2/tsukuyomi/model.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Uso: morok-g2-tsukuyomi-generate <checkpoint> <prompt> <max_bytes>\n";
        return 2;
    }
    std::size_t maximum = 0;
    const std::string count(argv[3]);
    const auto parsed = std::from_chars(count.data(),
        count.data() + count.size(), maximum);
    if (parsed.ec != std::errc{} || parsed.ptr != count.data() + count.size() ||
        maximum > 65536) {
        std::cerr << "Limite de geração inválido (máximo: 65536 bytes).\n";
        return 2;
    }

    morok::g2::tsukuyomi::Model model;
    if (!morok::g2::tsukuyomi::load_checkpoint(model, argv[1])) {
        std::cerr << "Checkpoint inválido ou incompatível.\n";
        return 1;
    }

    const std::string prompt(argv[2]);
    std::vector<std::uint8_t> input(prompt.begin(), prompt.end());
    const auto result = morok::g2::tsukuyomi::generate(model, input, maximum);
    for (std::size_t i = input.size(); i < result.size(); ++i)
        std::cout.put(static_cast<char>(result[i]));
    std::cout.put('\n');
    return std::cout.good() ? 0 : 1;
}
