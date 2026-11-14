#include "morok/common/utf8_text.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/learn/g1_generation.hpp"
#include "morok/model/learn/g1_training.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

int main() {
    namespace fs = std::filesystem;
    using namespace morok;

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() /
        ("morok-stage15-training-" + std::to_string(nonce));
    const auto corpus = root / "corpus";
    const auto checkpoint = root / "model.morok";
    const auto best = root / "best.morok";
    const auto metrics = root / "metrics.jsonl";
    fs::create_directories(corpus);

    const auto fixture = fs::path(MOROK_TEST_SOURCE_DIR) /
        "tests" / "fixtures" / "benchmark";
    for (const auto& entry : fs::directory_iterator(fixture)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt")
            fs::copy_file(entry.path(), corpus / entry.path().filename());
    }

    model::G1TrainingConfig config;
    config.corpus_directory = corpus;
    config.checkpoint_path = checkpoint;
    config.best_model_path = best;
    config.metrics_log_path = metrics;
    config.max_epochs = 1;
    config.patience = 1;
    config.truncation_length = 8;
    config.seed = 20261113;

    model::G1Parameters parameters{};
    const auto report = model::train_g1_corpus(config, parameters);
    assert(report.discovered_documents >= 3);
    assert(report.unique_documents >= 3);
    assert(report.training_documents > 0);
    assert(report.validation_documents > 0);
    assert(report.test_documents > 0);
    assert(report.test_target_bytes > 0);
    assert(!report.epochs.empty());
    assert(fs::is_regular_file(checkpoint));
    assert(fs::is_regular_file(best));
    assert(fs::is_regular_file(metrics));

    const auto restored = model::load_g1_checkpoint(checkpoint);
    assert(restored.parameters.flatten() == parameters.flatten());

    const auto prompt = common::Utf8Text::from_utf8("Morok AI");
    assert(prompt.has_value());
    const auto generated = model::generate_g1(
        restored.parameters, *prompt.value_if(), {16});
    assert(generated.generated_bytes <= 16);
    assert(common::Utf8Text::from_utf8(
        generated.continuation.string()).has_value());

    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cout << "Training-to-checkpoint-to-generation integration passed\n";
    return 0;
}
