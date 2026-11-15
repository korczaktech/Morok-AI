#pragma once
#include "morok/g2/engine/optimizer.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace morok::g2::engine {
struct G2ExpertTrainingConfig {
 std::filesystem::path corpus_directory{}, checkpoint_path{}, resume_from_checkpoint{}, metrics_log_path{}, best_model_path{};
 std::size_t max_epochs{50}, patience{5}, truncation_length{64};
 std::uint64_t seed{42};
 std::size_t max_documents{100000};
 std::uint64_t max_document_bytes{1ULL<<30U};
 G2ExpertOptimizerConfig optimizer{G2ExpertOptimizerConfig::adamw()};
 bool allow_single_document{false};
 bool train_all_documents{false};
 std::function<bool()> cancellation_requested{};
};
struct G2ExpertEpochMetrics {
 std::size_t epoch{0};
 std::uint64_t training_target_bytes{0}, optimizer_updates{0};
 double training_bpb{0}, validation_bpb{0}, last_gradient_norm{0};
 double mean_gradient_norm{0}, max_gradient_norm{0};
 double wall_seconds{0};
};
struct G2ExpertTrainingReport {
 std::size_t discovered_documents{0}, unique_documents{0};
 std::size_t training_documents{0}, validation_documents{0}, test_documents{0};
 std::uint64_t corpus_bytes{0};
 std::string corpus_hash{};
 std::size_t best_epoch{0};
 double best_validation_bpb{0}, test_bpb{0};
 double test_evaluation_seconds{0}, test_evaluation_bytes_per_second{0};
 std::uint64_t test_target_bytes{0};
 bool stopped_early{false}, resumed{false}, single_document_evaluation{false};
 bool training_data_evaluation{false};
 std::vector<G2ExpertEpochMetrics> epochs{};
};
[[nodiscard]] G2ExpertTrainingReport train_g2_expert_corpus(const G2ExpertTrainingConfig&, G2ExpertParameters&);
} // namespace morok::g2::engine