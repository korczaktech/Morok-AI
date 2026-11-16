#include "morok/g2/tsukuyomi/model.hpp"

#include <emscripten/bind.h>

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

using morok::g2::tsukuyomi::Model;

std::string question_checkpoint_path(const std::string& question) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : question) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }

    std::ostringstream name;
    name << "/models/q_" << std::hex << std::setw(16)
         << std::setfill('0') << hash << ".ckpt";
    return name.str();
}

class TsukuyomiRuntime {
public:
    bool hasCheckpoint(const std::string& question) const {
        return std::filesystem::is_regular_file(
            question_checkpoint_path(question));
    }

    std::string generate(const std::string& question,
                         std::size_t max_new_bytes) const {
        if (max_new_bytes == 0 || max_new_bytes > 4096) return {};

        Model model;
        const auto checkpoint = question_checkpoint_path(question);
        if (!morok::g2::tsukuyomi::load_checkpoint(model, checkpoint)) return {};

        const std::string prompt = "U: " + question + "\nIA:";
        const std::vector<std::uint8_t> input(prompt.begin(), prompt.end());
        const auto result = morok::g2::tsukuyomi::generate(
            model, input, max_new_bytes);

        if (result.size() < input.size()) return {};
        std::string answer;
        answer.reserve(result.size() - input.size());
        for (std::size_t index = input.size(); index < result.size(); ++index) {
            const char byte = static_cast<char>(result[index]);
            if (byte == '\n' || byte == '\r' || byte == '\0') break;
            answer.push_back(byte);
        }
        return answer;
    }
};

} // namespace

EMSCRIPTEN_BINDINGS(morok_tsukuyomi_web) {
    emscripten::class_<TsukuyomiRuntime>("TsukuyomiRuntime")
        .constructor<>()
        .function("hasCheckpoint", &TsukuyomiRuntime::hasCheckpoint)
        .function("generate", &TsukuyomiRuntime::generate);
}
