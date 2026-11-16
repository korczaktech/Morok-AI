#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace morok::g3::training {

struct DatasetExample final {
    std::string example_id;
    std::string group_id;
    std::string category;
    std::string question;
    std::string answer;
    std::string source;
    std::string language;
    std::string dataset_version;
    double quality{1.0};
    bool sensitive{false};
    std::vector<std::string> question_variants;
};

struct Dataset final {
    std::vector<DatasetExample> examples;
    std::string sha256;
};

struct DatasetSplits final {
    std::vector<std::size_t> train;
    std::vector<std::size_t> validation;
    std::vector<std::size_t> test;
};

struct SplitConfig final {
    double train_fraction{0.85};
    double validation_fraction{0.10};
    double test_fraction{0.05};
    std::uint64_t seed{42U};
};

[[nodiscard]] Dataset load_jsonl(const std::filesystem::path& path);
[[nodiscard]] DatasetSplits split_by_group_id(const Dataset& dataset,
                                               SplitConfig config = {});
[[nodiscard]] std::vector<DatasetExample> expand_controlled(
    const DatasetExample& example, std::size_t maximum_variants,
    std::uint64_t seed);

} // namespace morok::g3::training
