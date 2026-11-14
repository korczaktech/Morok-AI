#include "morok/common/build_info.hpp"
#include "morok/common/utf8_text.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/learn/g1_evaluation.hpp"
#include "morok/model/learn/g1_generation.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
std::string read_file(const std::string& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input file: " + path);
    std::string value;
    value.reserve(4096);
    char buffer[4096];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const auto count = input.gcount();
        if (count > 0) {
            if (value.size() + static_cast<std::size_t>(count) > limit)
                throw std::runtime_error("input exceeds configured byte limit");
            value.append(buffer, static_cast<std::size_t>(count));
        }
    }
    if (input.bad()) throw std::runtime_error("failed while reading: " + path);
    return value;
}
void write_file(const std::string& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create output file: " + path);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.flush();
    if (!output) throw std::runtime_error("failed while writing: " + path);
}
void usage() {
    std::cout
        << "Morok AI " << morok::common::version() << "\n"
        << "Usage:\n"
        << "  morok-cli version\n"
        << "  morok-cli inspect <checkpoint>\n"
        << "  morok-cli generate <checkpoint> <prompt.txt> <output.txt> [max_bytes]\n"
        << "\nTraining and benchmarking are provided by morok-train and morok_benchmark.\n";
}
}
int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "help") {
        usage();
        return argc < 2 ? 2 : 0;
    }
    try {
        const std::string command = argv[1];
        if (command == "version") {
            std::cout << morok::common::version() << '\n';
            return 0;
        }
        if (command == "inspect") {
            if (argc != 3) { usage(); return 2; }
            const auto checkpoint = morok::model::load_g1_checkpoint(argv[2]);
            const auto stats = morok::model::evaluate_g1_parameters(checkpoint.parameters);
            std::cout << "{\"format\":\"MOROKCKP\",\"epoch\":" << checkpoint.epoch
                      << ",\"parameters\":" << stats.architecture_parameter_count
                      << ",\"finite\":" << (stats.all_finite ? "true" : "false")
                      << ",\"mean\":" << stats.all.mean
                      << ",\"stddev\":" << stats.all.standard_deviation << "}\n";
            return 0;
        }
        if (command == "generate") {
            if (argc < 5 || argc > 6) { usage(); return 2; }
            const auto checkpoint = morok::model::load_g1_checkpoint(argv[2]);
            const auto prompt_raw = read_file(argv[3], 64U * 1024U);
            const auto prompt_result = morok::common::Utf8Text::from_utf8(prompt_raw);
            if (!prompt_result) throw std::runtime_error("prompt is not valid UTF-8");
            std::size_t max_bytes = 256;
            if (argc == 6) max_bytes = static_cast<std::size_t>(std::stoull(argv[5]));
            if (max_bytes == 0 || max_bytes > 4096)
                throw std::invalid_argument("max_bytes must be between 1 and 4096");
            const auto result = morok::model::generate_g1(
                checkpoint.parameters, *prompt_result.value_if(), {max_bytes});
            write_file(argv[4], result.continuation.string());
            std::cout << "{\"generated_bytes\":" << result.generated_bytes
                      << ",\"candidate_evaluations\":" << result.candidate_evaluations
                      << ",\"reached_byte_limit\":"
                      << (result.reached_byte_limit ? "true" : "false") << "}\n";
            return 0;
        }
        usage();
        std::cerr << "unknown command: " << command << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "morok-cli: " << error.what() << '\n';
        return 1;
    }
}
