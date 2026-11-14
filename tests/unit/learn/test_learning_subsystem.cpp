#include "morok/learn/learning_subsystem.hpp"
#include "morok/core/cancellation.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
namespace {
morok::core::RequestContext request_for(const morok::common::SessionId& session) {
    auto result = morok::core::RequestContext::create(session);
    assert(result);
    return std::move(*result.value_if());
}
}
int main() {
    using namespace morok;
    auto session = common::SessionId::generate();
    assert(session);
    auto request = request_for(*session.value_if());
    learn::LearningSubsystem learning;
    core::Runtime runtime(core::RuntimeConfig{});
    assert(runtime.initialize({learning.initializer()}, {learning.shutdown_callback()}));
    assert(learning.initialized());
    model::G1Parameters initial;
    model::initialize_g1_parameters(initial, 42);
    const auto original = initial.flatten();
    const auto root = std::filesystem::temp_directory_path() / "morok_ia_learn_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "corpus");
    for (int i = 0; i < 3; ++i) {
        std::ofstream file(root / "corpus" / (std::to_string(i) + ".txt"), std::ios::binary);
        file << "Morok AI experiment document " << i << ".\n";
    }
    model::G1TrainingConfig experiment;
    experiment.corpus_directory = root / "corpus";
    experiment.max_epochs = 1;
    experiment.patience = 1;
    experiment.truncation_length = 8;
    experiment.seed = 42;
    auto trained = learning.run_experiment(request, experiment, initial);
    assert(trained);
    assert(trained.value_if()->report.unique_documents == 3);
    assert(trained.value_if()->report.best_epoch == 1);
    assert(trained.value_if()->parameter_evaluation.all_finite);
    assert(trained.value_if()->request_id == request.request_id());
    assert(initial.flatten() == original);
    CancellationSource cancelled;
    assert(cancelled.cancel());
    auto cancelled_request_result = core::RequestContext::create(*session.value_if(), cancelled.token());
    assert(cancelled_request_result);
    auto cancelled_request = std::move(*cancelled_request_result.value_if());
    auto cancelled_run = learning.run_experiment(cancelled_request, experiment, initial);
    assert(!cancelled_run);
    assert(cancelled_run.error_if()->code == common::ErrorCode::cancelled);
    learn::LearningSubsystem disabled(learn::LearningConfig{false, 10, 100, 1024});
    assert(disabled.initialize(core::RuntimeConfig{}));
    auto denied = disabled.run_experiment(request, experiment, initial);
    assert(!denied);
    assert(denied.error_if()->code == common::ErrorCode::permission_denied);
    disabled.shutdown_callback()();
    assert(runtime.shutdown());
    assert(!learning.initialized());
    std::filesystem::remove_all(root);
}
