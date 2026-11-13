#pragma once
#include "morok/model/learn/optimizer.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace morok::model {
struct G1TrainingConfig {
 std::filesystem::path corpus_directory{}, checkpoint_path{}, resume_from_checkpoint{}, metrics_log_path{}, best_model_path{};
 std::size_t max_epochs{50}, patience{5}, truncation_length{64};
 std::uint64_t seed{42};
 std::size_t max_documents{100000};
 std::uint64_t max_document_bytes{1ULL<<30U};
 G1OptimizerConfig optimizer{G1OptimizerConfig::adamw()};
};
struct G1EpochMetrics {
 std::size_t epoch{0};
 std::uint64_t training_target_bytes{0}, optimizer_updates{0};
 double training_bpb{0}, validation_bpb{0}, last_gradient_norm{0};
};
struct G1TrainingReport {
 std::size_t discovered_documents{0}, unique_documents{0};
 std::size_t training_documents{0}, validation_documents{0}, test_documents{0};
 std::uint64_t corpus_bytes{0};
 std::string corpus_hash{};
 std::size_t best_epoch{0};
 double best_validation_bpb{0}, test_bpb{0};
 std::uint64_t test_target_bytes{0};
 bool stopped_early{false}, resumed{false};
 std::vector<G1EpochMetrics> epochs{};
};
[[nodiscard]] G1TrainingReport train_g1_corpus(const G1TrainingConfig&, G1Parameters&);
} // namespace morok::model