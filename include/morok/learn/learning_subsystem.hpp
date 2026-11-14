#pragma once

#include "morok/common/result.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/request_context.hpp"
#include "morok/core/runtime.hpp"
#include "morok/model/learn/g1_training.hpp"
#include "morok/model/learn/g1_evaluation.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <mutex>

namespace morok::learn {

struct LearningConfig final {
    bool enabled{true};
    std::size_t max_epochs{1000};
    std::size_t max_documents{100000};
    std::uint64_t max_document_bytes{1ULL << 30U};

    [[nodiscard]] common::Status validate() const;
};

struct LearningRunResult final {
    model::G1TrainingReport report{};
    model::G1Parameters candidate_parameters{};
    model::G1ParameterEvaluation parameter_evaluation{};
    std::string request_id{};
};

class LearningSubsystem final {
public:
    explicit LearningSubsystem(LearningConfig config = {}) : config_(config) {}
    LearningSubsystem(const LearningSubsystem&) = delete;
    LearningSubsystem& operator=(const LearningSubsystem&) = delete;

    [[nodiscard]] common::Status initialize(const core::RuntimeConfig& runtime);
    [[nodiscard]] core::Runtime::Initializer initializer();
    [[nodiscard]] core::Runtime::Shutdown shutdown_callback();
    [[nodiscard]] common::Result<LearningRunResult> run_experiment(
        const core::RequestContext& request,
        const model::G1TrainingConfig& experiment,
        const model::G1Parameters& initial_parameters);
    [[nodiscard]] bool initialized() const;

private:
    [[nodiscard]] common::Error error(common::ErrorCode code,
                                      std::string message,
                                      std::string stage,
                                      const core::RequestContext* request = nullptr) const;

    LearningConfig config_;
    mutable std::mutex mutex_;
    bool initialized_{false};
    bool running_{false};
};

} // namespace morok::learn
