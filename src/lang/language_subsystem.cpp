#include "morok/lang/language_subsystem.hpp"

#include <algorithm>
#include <exception>
#include <cmath>
#include <utility>

namespace morok::lang {

LanguageSubsystem::LanguageSubsystem(const model::G1Parameters& parameters,
                                     LanguageConfig config)
    : parameters_(parameters), language_config_(config) {}

common::Status LanguageSubsystem::initialize(const core::RuntimeConfig& config) {
    std::lock_guard lock(mutex_);
    if (initialized_)
        return common::Status::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "IA-LANG is already initialized", "lang", "initialize"));
    const auto valid = config.validate();
    if (!valid) return common::Status::failure(*valid.error_if());
    const auto flat_parameters = parameters_.flatten();
    if (!std::all_of(flat_parameters.begin(), flat_parameters.end(),
                     [](float value) { return std::isfinite(value); })) {
        return common::Status::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "G1 parameters contain a non-finite value", "lang", "initialize"));
    }
    if (language_config_.max_context_bytes == 0 ||
        language_config_.max_context_bytes > config.resources.max_input_bytes ||
        config.max_sessions > 4096U) {
        return common::Status::failure(common::Error::make(
            common::ErrorCode::resource_limit,
            "IA-LANG context or session capacity is outside supported limits",
            "lang", "initialize"));
    }
    resource_limits_ = config.resources;
    max_sessions_ = config.max_sessions;
    initialized_ = true;
    return common::Status::success();
}

common::Status LanguageSubsystem::shutdown() noexcept {
    std::lock_guard lock(mutex_);
    session_contexts_.clear();
    initialized_ = false;
    return common::Status::success();
}

common::Result<common::SessionId> LanguageSubsystem::create_session(
    core::SessionRegistry& registry) {
    std::lock_guard lock(mutex_);
    if (!initialized_)
        return common::Result<common::SessionId>::failure(common::Error::make(
            common::ErrorCode::model_not_loaded,
            "IA-LANG is not initialized", "lang", "session.create"));
    if (session_contexts_.size() >= max_sessions_ ||
        registry.size() >= std::min(max_sessions_, registry.capacity())) {
        return common::Result<common::SessionId>::failure(common::Error::make(
            common::ErrorCode::resource_limit,
            "IA-LANG session capacity reached", "lang", "session.create"));
    }
    auto created = registry.create();
    if (!created)
        return common::Result<common::SessionId>::failure(*created.error_if());
    const auto id = *created.value_if();
    try {
        const auto [position, inserted] =
            session_contexts_.emplace(id.to_string(), std::string{});
        (void)position;
        if (!inserted) {
            (void)registry.close(id);
            return common::Result<common::SessionId>::failure(common::Error::make(
                common::ErrorCode::internal_error,
                "duplicate language session identifier", "lang", "session.create"));
        }
    } catch (...) {
        (void)registry.close(id);
        return common::Result<common::SessionId>::failure(common::Error::make(
            common::ErrorCode::out_of_memory,
            "could not allocate language session state", "lang", "session.create"));
    }
    return common::Result<common::SessionId>::success(id);
}

common::Status LanguageSubsystem::reset_session(
    const common::SessionId& session_id) {
    std::lock_guard lock(mutex_);
    if (!initialized_)
        return common::Status::failure(common::Error::make(
            common::ErrorCode::model_not_loaded,
            "IA-LANG is not initialized", "lang", "session.reset"));
    const auto found = session_contexts_.find(session_id.to_string());
    if (found == session_contexts_.end())
        return common::Status::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "language session does not exist", "lang", "session.reset"));
    found->second.clear();
    return common::Status::success();
}

common::Status LanguageSubsystem::validate_request_locked(
    const core::RequestContext& request) const {
    if (!initialized_)
        return common::Status::failure(common::Error::make(
            common::ErrorCode::model_not_loaded,
            "IA-LANG is not initialized", "lang", "request"));
    if (request.is_cancelled())
        return common::Status::failure(common::Error::make(
            common::ErrorCode::cancelled,
            "language request was cancelled", "lang", "request"));
    if (!session_contexts_.contains(request.session_id().to_string()))
        return common::Status::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "request refers to an unknown language session", "lang", "request"));
    return common::Status::success();
}

common::Status LanguageSubsystem::observe(
    const core::RequestContext& request,
    const common::Utf8Text& text) {
    std::lock_guard lock(mutex_);
    const auto valid = validate_request_locked(request);
    if (!valid) return valid;
    if (!resource_limits_.permits_input(text.size_bytes()))
        return common::Status::failure(common::Error::make(
            common::ErrorCode::input_too_large,
            "language input exceeds IA-CORE limit", "lang", "observe"));
    auto& context = session_contexts_.at(request.session_id().to_string());
    if (text.size_bytes() > language_config_.max_context_bytes -
        std::min(context.size(), language_config_.max_context_bytes)) {
        return common::Status::failure(common::Error::make(
            common::ErrorCode::resource_limit,
            "language session context capacity exceeded; reset or close the session",
            "lang", "observe"));
    }
    try {
        context.append(text.view());
    } catch (...) {
        return common::Status::failure(common::Error::make(
            common::ErrorCode::out_of_memory,
            "could not extend language session context", "lang", "observe"));
    }
    return common::Status::success();
}

common::Result<LanguageGeneration> LanguageSubsystem::generate(
    const core::RequestContext& request,
    std::size_t max_new_bytes) {
    std::lock_guard lock(mutex_);
    const auto valid = validate_request_locked(request);
    if (!valid) return common::Result<LanguageGeneration>::failure(*valid.error_if());
    if (max_new_bytes == 0 || !resource_limits_.permits_generation(max_new_bytes) ||
        max_new_bytes > 256U) {
        return common::Result<LanguageGeneration>::failure(common::Error::make(
            common::ErrorCode::resource_limit,
            "generation length must fit IA-CORE and G1 limits",
            "lang", "generate"));
    }
    const auto& context = session_contexts_.at(request.session_id().to_string());
    auto prompt = common::Utf8Text::from_utf8(context);
    if (!prompt)
        return common::Result<LanguageGeneration>::failure(*prompt.error_if());
    try {
        const auto generated = model::generate_g1(
            parameters_, *prompt.value_if(), model::G1GenerationConfig{max_new_bytes});
        if (request.is_cancelled())
            return common::Result<LanguageGeneration>::failure(common::Error::make(
                common::ErrorCode::cancelled,
                "language request was cancelled", "lang", "generate"));
        if (generated.continuation.size_bytes() >
            language_config_.max_context_bytes -
            std::min(context.size(), language_config_.max_context_bytes)) {
            return common::Result<LanguageGeneration>::failure(common::Error::make(
                common::ErrorCode::resource_limit,
                "generated continuation would exceed session context capacity",
                "lang", "generate"));
        }
        session_contexts_.at(request.session_id().to_string()).append(
            generated.continuation.view());
        LanguageGeneration result{
            generated.continuation, generated.generated_bytes,
            generated.candidate_evaluations, generated.reached_byte_limit};
        return common::Result<LanguageGeneration>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return common::Result<LanguageGeneration>::failure(common::Error::make(
            common::ErrorCode::out_of_memory,
            "generation allocation failed", "lang", "generate"));
    } catch (const std::exception& error) {
        return common::Result<LanguageGeneration>::failure(common::Error::make(
            common::ErrorCode::internal_error, error.what(), "lang", "generate"));
    }
}

common::Status LanguageSubsystem::close_session(
    core::SessionRegistry& registry,
    const common::SessionId& session_id) {
    std::lock_guard lock(mutex_);
    const auto found = session_contexts_.find(session_id.to_string());
    if (found == session_contexts_.end() || !registry.contains(session_id))
        return common::Status::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "language session does not exist", "lang", "session.close"));
    const auto closed = registry.close(session_id);
    if (!closed) return closed;
    session_contexts_.erase(found);
    return common::Status::success();
}

core::Runtime::Initializer LanguageSubsystem::initializer() {
    return [this](const core::RuntimeConfig& config) { return initialize(config); };
}

core::Runtime::Shutdown LanguageSubsystem::shutdown_callback() {
    return [this] { (void)shutdown(); };
}

bool LanguageSubsystem::initialized() const noexcept {
    std::lock_guard lock(mutex_);
    return initialized_;
}

std::size_t LanguageSubsystem::active_sessions() const noexcept {
    std::lock_guard lock(mutex_);
    return session_contexts_.size();
}

} // namespace morok::lang
