#include "morok/common/utf8_text.hpp"
#include "morok/model/g1_forward.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/learn/g1_evaluation.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr std::uint64_t kMaxCorpusBytes = 1ULL << 30U;
constexpr std::uint64_t kMaxFileBytes = 64ULL << 20U;
std::string read_document(const fs::path& path, std::uint64_t& total) {
    const auto size = fs::file_size(path);
    if (size > kMaxFileBytes || total + size > kMaxCorpusBytes)
        throw std::runtime_error("corpus exceeds evaluation byte limits");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open corpus document: " + path.string());
    std::string text((std::istreambuf_iterator<char>(input)), {});
    if (input.bad()) throw std::runtime_error("failed reading corpus document");
    total += size;
    return text;
}
}
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: morok-evaluate <checkpoint.morok> <corpus-directory>\n";
        return 2;
    }
    try {
        const auto checkpoint = morok::model::load_g1_checkpoint(argv[1]);
        const fs::path root = fs::canonical(argv[2]);
        if (!fs::is_directory(root)) throw std::invalid_argument("corpus path is not a directory");
        std::vector<fs::path> documents;
        for (const auto& entry : fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied)) {
            const auto status = entry.symlink_status();
            if (fs::is_regular_file(status) && entry.path().extension() == ".txt")
                documents.push_back(entry.path());
        }
        std::sort(documents.begin(), documents.end());
        if (documents.empty()) throw std::runtime_error("corpus contains no .txt documents");

        double total_nats = 0.0;
        std::uint64_t targets = 0;
        std::uint64_t total_bytes = 0;
        std::size_t evaluated_documents = 0;
        for (const auto& path : documents) {
            const auto raw = read_document(path, total_bytes);
            const auto valid = morok::common::Utf8Text::from_utf8(raw);
            if (!valid) throw std::runtime_error("invalid UTF-8 in " + path.string());
            if (raw.size() < 2) continue;
            morok::model::G1State state{};
            morok::model::advance_g1_state(
                checkpoint.parameters, state,
                static_cast<std::uint8_t>(static_cast<unsigned char>(raw.front())));
            for (std::size_t i = 1; i < raw.size(); ++i) {
                const auto score = morok::model::evaluate_g1_next_byte(
                    checkpoint.parameters, state,
                    static_cast<std::uint8_t>(static_cast<unsigned char>(raw[i])));
                total_nats += score.negative_log_likelihood_nats;
                ++targets;
                morok::model::advance_g1_state(
                    checkpoint.parameters, state,
                    static_cast<std::uint8_t>(static_cast<unsigned char>(raw[i])));
            }
            ++evaluated_documents;
        }
        if (targets == 0) throw std::runtime_error("corpus has no scoreable target bytes");
        const auto metrics = morok::model::summarize_g1_loss(total_nats, targets);
        std::cout.precision(17);
        std::cout << "{\"documents\":" << evaluated_documents
                  << ",\"corpus_bytes\":" << total_bytes
                  << ",\"target_bytes\":" << metrics.target_count
                  << ",\"mean_nats_per_byte\":" << metrics.mean_nats_per_byte
                  << ",\"bits_per_byte\":" << metrics.bits_per_byte
                  << ",\"byte_perplexity\":" << metrics.byte_perplexity
                  << ",\"checkpoint_epoch\":" << checkpoint.epoch << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok-evaluate: " << error.what() << '\n';
        return 1;
    }
}
