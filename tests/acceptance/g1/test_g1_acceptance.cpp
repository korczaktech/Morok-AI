#include "morok/common/utf8_text.hpp"
#include "morok/lang/rlah/segmenter.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/learn/g1_generation.hpp"
#include "morok/model/learn/g1_training.hpp"
#include "morok/model/parameters/g1_parameters.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main() {
    namespace fs = std::filesystem;
    using namespace morok;

    static_assert(model::G1Parameters::parameter_count() == 1000);
    assert(model::kG1ParameterCount == 1000);

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() /
        ("morok-stage19-g1-" + std::to_string(nonce));
    const auto corpus = root / "corpus";
    const auto checkpoint = root / "resume.morok";
    const auto best = root / "best.morok";
    const auto metrics = root / "metrics.jsonl";
    fs::create_directories(corpus);

    const auto fixture = fs::path(MOROK_TEST_SOURCE_DIR) /
        "tests" / "fixtures" / "benchmark";
    for (const auto& entry : fs::directory_iterator(fixture)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt")
            fs::copy_file(entry.path(), corpus / entry.path().filename());
    }

    model::G1Parameters parameters{};
    model::initialize_g1_parameters(parameters, 20261114);
    const auto initial = parameters.flatten();
    assert(std::all_of(initial.begin(), initial.end(),
        [](float value) { return std::isfinite(value); }));

    model::G1TrainingConfig config;
    config.corpus_directory = corpus;
    config.checkpoint_path = checkpoint;
    config.best_model_path = best;
    config.metrics_log_path = metrics;
    config.max_epochs = 1;
    config.patience = 1;
    config.truncation_length = 8;
    config.seed = 20261114;

    const auto report = model::train_g1_corpus(config, parameters);
    assert(report.discovered_documents >= 3);
    assert(report.unique_documents >= 3);
    assert(report.training_documents > 0);
    assert(report.validation_documents > 0);
    assert(report.test_documents > 0);
    assert(report.test_target_bytes > 0);
    assert(!report.epochs.empty());
    std::uint64_t updates = 0;
    for (const auto& epoch : report.epochs) updates += epoch.optimizer_updates;
    assert(updates > 0);
    const auto trained_values = parameters.flatten();
    assert(std::all_of(trained_values.begin(), trained_values.end(),
        [](float value) { return std::isfinite(value); }));
    assert(parameters.flatten() != initial);
    assert(fs::is_regular_file(checkpoint));
    assert(fs::is_regular_file(best));
    assert(fs::is_regular_file(metrics));

    const auto restored = model::load_g1_checkpoint(checkpoint);
    assert(restored.parameters.flatten() == parameters.flatten());
    auto prompt = common::Utf8Text::from_utf8("Morok AI — teste de aceitação");
    assert(prompt);
    const auto generated = model::generate_g1(
        restored.parameters, *prompt.value_if(), model::G1GenerationConfig{32});
    assert(generated.generated_bytes <= 32);
    assert(generated.continuation.size_bytes() == generated.generated_bytes);
    assert(common::Utf8Text::from_utf8(generated.continuation.string()));

    const std::string repetitive = [] {
        std::string value;
        for (int i = 0; i < 32; ++i) value += "abracadabra";
        return value;
    }();
    const std::vector<std::uint8_t> raw(repetitive.begin(), repetitive.end());
    lang::rlah::Candidate dictionary_entry;
    dictionary_entry.bytes.assign({'a','b','r','a','c','a','d','a','b','r','a'});
    dictionary_entry.active = true;
    dictionary_entry.utility = 1.0F;
    const auto encoded = lang::rlah::encode(
        raw, std::span<const lang::rlah::Candidate>(&dictionary_entry, 1));
    assert(encoded.compressed);
    std::vector<std::uint8_t> decoded;
    std::string decode_error;
    assert(lang::rlah::decode(encoded.data, decoded, decode_error));
    assert(decoded == raw);

    auto corrupted = encoded.data;
    corrupted.back() ^= 1U;
    assert(!lang::rlah::decode(corrupted, decoded, decode_error));

    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cout << "G1 acceptance passed: 1000 parameters, training, checkpoint, "
                 "generation and reversible RLAH\n";
    return 0;
}
