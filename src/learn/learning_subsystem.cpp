#include "morok/learn/learning_subsystem.hpp"

#include <cmath>
#include <new>
#include <utility>

namespace morok::learn {
namespace {
template <typename T>
common::Result<T> failure(common::Error error) {
    return common::Result<T>::failure(std::move(error));
}

bool finite_parameters(const model::G1Parameters& parameters) {
    for (const float value : parameters.flatten())
        if (!std::isfinite(value)) return false;
    return true;
}
} // namespace

common::Status LearningConfig::validate() const {
    if (max_epochs == 0 || max_epochs > 100000 ||
        max_documents == 0 || max_documents > 100000 ||
        max_document_bytes == 0 || max_document_bytes > (1ULL << 30U)) {
        return common::Status::failure(common::Error::make(
            common::ErrorCode::resource_limit,
            "IA-LEARN limits are outside the supported range",
            "learn", "configuration"));
    }
    return common::Status::success();
}

common::Error LearningSubsystem::error(
    common::ErrorCode code, std::string message, std::string stage,
    const core::RequestContext* request) const {
    auto result = common::Error::make(code, std::move(message), "learn",
                                      std::move(stage));
    if (request) result.request_id = request->request_id();
    return result;
}

common::Status LearningSubsystem::initialize(const core::RuntimeConfig& runtime) {
    std::lock_guard lock(mutex_);
    if (initialized_)
        return common::Status::failure(error(
            common::ErrorCode::invalid_argument,
            "IA-LEARN is already initialized", "initialize"));
    const auto limits = config_.validate();
    if (!limits) return limits;
    const auto valid_runtime = runtime.validate();
    if (!valid_runtime) return valid_runtime;
    initialized_ = true;
    return common::Status::success();
}

core::Runtime::Initializer LearningSubsystem::initializer() {
    return [this](const core::RuntimeConfig& config) {
        return initialize(config);
    };
}

core::Runtime::Shutdown LearningSubsystem::shutdown_callback() {
    return [this] {
        std::lock_guard lock(mutex_);
        initialized_ = false;
        running_ = false;
    };
}

bool LearningSubsystem::initialized() const {
    std::lock_guard lock(mutex_);
    return initialized_;
}

common::Result<LearningRunResult> LearningSubsystem::run_experiment(
    const core::RequestContext& request,
    const model::G1TrainingConfig& experiment,
    const model::G1Parameters& initial_parameters) {
    std::unique_lock lock(mutex_);
    if (!initialized_)
        return failure<LearningRunResult>(error(
            common::ErrorCode::model_not_loaded,
            "IA-LEARN is not initialized", "run", &request));
    if (!config_.enabled)
        return failure<LearningRunResult>(error(
            common::ErrorCode::permission_denied,
            "training is disabled by IA-LEARN policy", "authorize", &request));
    if (running_)
        return failure<LearningRunResult>(error(
            common::ErrorCode::resource_limit,
            "another experiment is already active", "schedule", &request));
    if (request.is_cancelled())
        return failure<LearningRunResult>(error(
            common::ErrorCode::cancelled,
            "experiment was cancelled before execution", "cancel", &request));
    if (experiment.corpus_directory.empty() ||
        experiment.max_epochs == 0 ||
        experiment.max_epochs > config_.max_epochs ||
        experiment.max_documents == 0 ||
        experiment.max_documents > config_.max_documents ||
        experiment.max_document_bytes == 0 ||
        experiment.max_document_bytes > config_.max_document_bytes ||
        experiment.truncation_length == 0 ||
        !finite_parameters(initial_parameters)) {
        return failure<LearningRunResult>(error(
            common::ErrorCode::invalid_argument,
            "experiment configuration or initial parameters are invalid",
            "validate", &request));
    }

    running_ = true;
    struct RunningReset final {
        bool& value;
        ~RunningReset() { value = false; }
    } reset{running_};

    try {
        // Train a private candidate. The caller's live/inference parameters are
        // never modified by IA-LEARN.
        auto candidate = initial_parameters;
        auto report = model::train_g1_corpus(experiment, candidate);
        if (request.is_cancelled())
            return failure<LearningRunResult>(error(
                common::ErrorCode::cancelled,
                "experiment completed after cancellation; candidate was not published",
                "cancel", &request));
        if (!finite_parameters(candidate))
            return failure<LearningRunResult>(error(
                common::ErrorCode::internal_error,
                "training produced non-finite parameters", "validate_artifact", &request));
        auto evaluation = model::evaluate_g1_parameters(candidate);
        LearningRunResult result;
        result.report = std::move(report);
        result.candidate_parameters = std::move(candidate);
        result.parameter_evaluation = std::move(evaluation);
        result.request_id = request.request_id().to_string();
        return common::Result<LearningRunResult>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return failure<LearningRunResult>(error(
            common::ErrorCode::out_of_memory,
            "memory allocation failed during experiment", "run", &request));
    } catch (const std::exception& exception) {
        return failure<LearningRunResult>(error(
            common::ErrorCode::internal_error,
            exception.what(), "run", &request));
    } catch (...) {
        return failure<LearningRunResult>(error(
            common::ErrorCode::internal_error,
            "unknown training failure", "run", &request));
    }
}

} // namespace morok::learn
