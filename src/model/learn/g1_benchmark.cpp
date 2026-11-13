#include "morok/model/learn/g1_benchmark.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace morok::model { namespace {
constexpr std::size_t kBufferSize = 64U * 1024U;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
struct Document { std::filesystem::path path; std::uint64_t hash{}; std::uint64_t size{}; };
enum class Split { train, validation, test };

std::uint64_t hash_file(const std::filesystem::path& path, std::uint64_t limit,
                        std::uint64_t& size) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open corpus file: " + path.string());
    std::array<char, kBufferSize> buffer{};
    std::uint64_t hash = kFnvOffset;
    size = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(input.gcount());
        for (std::size_t i = 0; i < count; ++i) {
            const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(buffer[i]));
            hash ^= byte;
            hash *= kFnvPrime;
        }
        if (count > limit - std::min(size, limit)) throw std::runtime_error("document exceeds benchmark byte limit: " + path.string());
        size += static_cast<std::uint64_t>(count);
    }
    if (!input.eof()) throw std::runtime_error("I/O error reading corpus file: " + path.string());
    if (size < 2) throw std::runtime_error("benchmark documents must contain at least two bytes: " + path.string());
    return hash;
}

bool equal_files(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::ifstream a(left, std::ios::binary), b(right, std::ios::binary);
    if (!a || !b) throw std::runtime_error("cannot compare duplicate corpus files");
    std::array<char, kBufferSize> x{}, y{};
    for (;;) {
        a.read(x.data(), static_cast<std::streamsize>(x.size()));
        b.read(y.data(), static_cast<std::streamsize>(y.size()));
        const auto nx = a.gcount(), ny = b.gcount();
        if (nx != ny || !std::equal(x.begin(), x.begin() + nx, y.begin())) return false;
        if (nx == 0) return a.eof() && b.eof();
    }
}

std::vector<Document> discover(const std::filesystem::path& root, std::size_t max_documents,
                               std::uint64_t max_bytes, std::size_t& discovered) {
    if (!std::filesystem::is_directory(root)) throw std::invalid_argument("corpus path must be a directory");
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
            paths.push_back(entry.path());
            if (paths.size() > max_documents) throw std::runtime_error("max_documents exceeded");
        }
    }
    std::sort(paths.begin(), paths.end());
    discovered = paths.size();
    std::vector<Document> documents;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> seen;
    for (const auto& path : paths) {
        std::uint64_t size = 0;
        const auto hash = hash_file(path, max_bytes, size);
        bool duplicate = false;
        if (const auto it = seen.find(hash); it != seen.end()) {
            for (const auto index : it->second) {
                if (documents[index].size == size && equal_files(documents[index].path, path)) {
                    duplicate = true;
                    break;
                }
            }
        }
        if (!duplicate) {
            seen[hash].push_back(documents.size());
            documents.push_back({path, hash, size});
        }
    }
    if (documents.size() < 3) throw std::runtime_error("at least three unique .txt documents are required");
    return documents;
}

void split_documents(std::vector<Document>& documents, std::uint64_t seed) {
    std::vector<std::size_t> indices(documents.size());
    for (std::size_t i = 0; i < indices.size(); ++i) indices[i] = i;
    std::mt19937_64 rng(seed);
    std::shuffle(indices.begin(), indices.end(), rng);
    const auto train_count = documents.size() >= 20 ? documents.size() * 90 / 100 : documents.size() - 2;
    const auto validation_count = documents.size() >= 20
        ? std::max<std::size_t>(1, documents.size() * 5 / 100) : 1;
    if (train_count == 0 || train_count + validation_count >= documents.size())
        throw std::runtime_error("cannot form benchmark data splits");
    for (std::size_t i = 0; i < indices.size(); ++i)
        documents[indices[i]].split = i < train_count ? Split::train
            : (i < train_count + validation_count ? Split::validation : Split::test);
}

void count_targets(const Document& document, std::array<std::uint64_t, 256>& counts,
                   std::uint64_t& total, bool collect) {
    std::ifstream input(document.path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot reopen corpus file: " + document.path.string());
    std::array<char, kBufferSize> buffer{};
    bool first = true;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto n = static_cast<std::size_t>(input.gcount());
        for (std::size_t i = 0; i < n; ++i) {
            const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(buffer[i]));
            if (first) { first = false; continue; }
            if (collect) ++counts[byte];
            ++total;
        }
    }
    if (!input.eof()) throw std::runtime_error("I/O error counting benchmark targets");
}
} // namespace

G1BaselineMetrics evaluate_g1_softmax256_unigram_baseline(
    const std::filesystem::path& corpus_directory, std::uint64_t seed,
    std::size_t max_documents, std::uint64_t max_document_bytes) {
    G1BaselineMetrics result;
    auto documents = discover(corpus_directory, max_documents, max_document_bytes,
                              result.discovered_documents);
    result.unique_documents = documents.size();
    split_documents(documents, seed);
    std::array<std::uint64_t, 256> counts{};
    for (const auto& document : documents) {
        if (document.split == Split::train) {
            ++result.training_documents;
            count_targets(document, counts, result.training_target_bytes, true);
        } else if (document.split == Split::test) {
            ++result.test_documents;
        }
    }
    if (result.training_target_bytes == 0 || result.test_documents == 0)
        throw std::runtime_error("benchmark train/test split has no targets");
    long double loss_bits = 0.0L;
    for (const auto& document : documents) {
        if (document.split != Split::test) continue;
        std::ifstream input(document.path, std::ios::binary);
        if (!input) throw std::runtime_error("cannot reopen test document");
        std::array<char, kBufferSize> buffer{};
        bool first = true;
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto n = static_cast<std::size_t>(input.gcount());
            for (std::size_t i = 0; i < n; ++i) {
                const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(buffer[i]));
                if (first) { first = false; continue; }
                const long double probability =
                    (static_cast<long double>(counts[byte]) + 1.0L) /
                    (static_cast<long double>(result.training_target_bytes) + 256.0L);
                loss_bits -= std::log2(probability);
                ++result.test_target_bytes;
            }
        }
        if (!input.eof()) throw std::runtime_error("I/O error evaluating benchmark test document");
    }
    if (result.test_target_bytes == 0) throw std::runtime_error("benchmark test split has no next-byte targets");
    result.softmax256_unigram_bpb =
        static_cast<double>(loss_bits / static_cast<long double>(result.test_target_bytes));
    if (!std::isfinite(result.softmax256_unigram_bpb))
        throw std::runtime_error("non-finite softmax-256 baseline BPB");
    return result;
}

} // namespace morok::model
