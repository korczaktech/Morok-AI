#include "morok/model/learn/g1_benchmark.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void write(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot create benchmark fixture");
    file << content;
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / "morok-g1-baseline-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    try {
        write(root / "a.txt", "aaaaabbbbbccccc");
        write(root / "b.txt", "aaaaabbbbbddddd");
        write(root / "c.txt", "aaaaabbbbbcccccxxxxx");
        write(root / "duplicate.txt", "aaaaabbbbbccccc");
        const auto metrics = morok::model::evaluate_g1_softmax256_unigram_baseline(root, 42);
        require(metrics.discovered_documents == 4, "discovered count mismatch");
        require(metrics.unique_documents == 3, "exact deduplication mismatch");
        require(metrics.training_documents == 1 && metrics.test_documents == 1,
                "three-document split mismatch");
        require(metrics.training_target_bytes == 14, "training target count mismatch");
        require(metrics.test_target_bytes == 14, "test target count mismatch");
        require(std::isfinite(metrics.softmax256_unigram_bpb) &&
                metrics.softmax256_unigram_bpb > 0.0, "baseline BPB invalid");
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::filesystem::remove_all(root);
    return 0;
}
