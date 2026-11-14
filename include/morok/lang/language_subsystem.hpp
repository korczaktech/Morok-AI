#pragma once

#include "morok/common/ids.hpp"
#include "morok/common/result.hpp"
#include "morok/common/utf8_text.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/request_context.hpp"
#include "morok/core/runtime.hpp"
#include "morok/core/session_registry.hpp"
#include "morok/model/learn/g1_generation.hpp"

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace morok::lang {

struct LanguageConfig final {
    std::size_t max_context_bytes{64U * 1024U};
};

struct LanguageGeneration final {
    common::Utf8Text text;
    std::size_t generated_bytes{0};
    std::size_t candidate_evaluations{0};
    bool reached_byte_limit{false};
};

class LanguageSubsystem final {
public:
    explicit LanguageSubsystem(const model::G1Parameters& parameters,
                               LanguageConfig config = {});

    [[nodiscard]] common::Status initialize(const core::RuntimeConfig& config);
    [[nodiscard]] common::Status shutdown() noexcept;

    [[nodiscard]] common::Result<common::SessionId> create_session(
        core::SessionRegistry& registry);
    [[nodiscard]] common::Status reset_session(
        const common::SessionId& session_id);
    [[nodiscard]] common::Status observe(
        const core::RequestContext& request,
        const common::Utf8Text& text);
    [[nodiscard]] common::Result<LanguageGeneration> generate(
        const core::RequestContext& request,
        std::size_t max_new_bytes);
    [[nodiscard]] common::Status close_session(
        core::SessionRegistry& registry,
        const common::SessionId& session_id);

    [[nodiscard]] core::Runtime::Initializer initializer();
    [[nodiscard]] core::Runtime::Shutdown shutdown_callback();
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t active_sessions() const noexcept;

private:
    [[nodiscard]] common::Status validate_request_locked(
        const core::RequestContext& request) const;

    model::G1Parameters parameters_;
    LanguageConfig language_config_;
    core::ResourceLimits resource_limits_{};
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> session_contexts_;
    bool initialized_{false};
};

} // namespace morok::lang
