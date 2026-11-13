#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace morok::model {

struct G1BaselineMetrics {
    std::size_t discovered_documents{0};
    std::size_t unique_documents{0};
    std::size_t training_documents{0};
    std::size_t test_documents{0};
    std::uint64_t training_target_bytes{0};
    std::uint64_t test_target_bytes{0};
    double softmax256_unigram_bpb{0.0};
};

// Closed-form 256-class categorical baseline with add-one smoothing.
// Uses the same sorted-document, exact-deduplication and seeded split policy as G1.
[[nodiscard]] G1BaselineMetrics evaluate_g1_softmax256_unigram_baseline(
    const std::filesystem::path& corpus_directory,
    std::uint64_t seed = 42,
    std::size_t max_documents = 100000,
    std::uint64_t max_document_bytes = (1ULL << 30U));

} // namespace morok::model
