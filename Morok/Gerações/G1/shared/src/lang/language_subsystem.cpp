#include "morok/lang/language_subsystem.hpp"
#include "morok/g1/tsukuyomi/profile.hpp"
#include "morok/g1/apollo/profile.hpp"
#include "morok/g1/mokosh/profile.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <initializer_list>
#include <string_view>
#include <cmath>
#include <utility>

namespace morok::lang {
namespace {

std::string normalize_input(std::string_view input) {
    std::string folded(input);
    const auto replace_all = [&folded](std::string_view from, char to) {
        std::size_t position = 0;
        while ((position = folded.find(from, position)) != std::string::npos) {
            folded.replace(position, from.size(), 1, to);
            ++position;
        }
    };
    const std::array<std::pair<std::string_view, char>, 28> accents{{
        {"á",'a'},{"à",'a'},{"â",'a'},{"ã",'a'},{"ä",'a'},
        {"Á",'a'},{"À",'a'},{"Â",'a'},{"Ã",'a'},{"Ä",'a'},
        {"é",'e'},{"è",'e'},{"ê",'e'},{"ë",'e'},{"É",'e'},{"È",'e'},{"Ê",'e'},{"Ë",'e'},
        {"í",'i'},{"ì",'i'},{"î",'i'},{"ï",'i'},{"Í",'i'},{"Ì",'i'},{"Î",'i'},{"Ï",'i'},
        {"ç",'c'},{"Ç",'c'}
    }};
    for (const auto& [from, to] : accents) replace_all(from, to);
    const std::array<std::pair<std::string_view, char>, 14> more_accents{{
        {"ó",'o'},{"ò",'o'},{"ô",'o'},{"õ",'o'},{"ö",'o'},{"Ó",'o'},{"Ò",'o'},
        {"Ô",'o'},{"Õ",'o'},{"Ö",'o'},{"ú",'u'},{"ù",'u'},{"û",'u'},{"ü",'u'}
    }};
    for (const auto& [from, to] : more_accents) replace_all(from, to);
    const std::array<std::pair<std::string_view, char>, 5> final_accents{{
        {"Ú",'u'},{"Ù",'u'},{"Û",'u'},{"Ü",'u'},{"ñ",'n'}
    }};
    for (const auto& [from, to] : final_accents) replace_all(from, to);

    std::string output;
    output.reserve(folded.size());
    bool pending_space = false;
    for (const unsigned char byte : folded) {
        if (byte == '\r' || byte == '\n' || byte == '\t' || byte == ' ') {
            pending_space = !output.empty();
            continue;
        }
        if (pending_space) output.push_back(' ');
        pending_space = false;
        output.push_back(static_cast<char>(byte < 128U
            ? std::tolower(byte) : byte));
    }
    return output;
}

std::string serialize_conversation(
    const std::vector<ConversationMessage>& messages) {
    std::string serialized;
    for (const auto& message : messages) {
        if (message.role == ConversationRole::user)
            serialized += "U: " + message.text + "\nIA: ";
        else
            serialized += message.text + "\n";
    }
    return serialized;
}

DetectedLanguage identify_language(std::string_view text) {
    std::string lower;
    lower.reserve(text.size());
    for (const unsigned char byte : text)
        lower.push_back(static_cast<char>(byte < 128U
            ? std::tolower(byte) : byte));

    const auto count = [&lower](std::initializer_list<std::string_view> words) {
        std::size_t matches = 0;
        for (const auto word : words) {
            std::size_t at = 0;
            while ((at = lower.find(word, at)) != std::string::npos) {
                const bool left = at == 0 || !std::isalnum(
                    static_cast<unsigned char>(lower[at - 1]));
                const auto end = at + word.size();
                const bool right = end == lower.size() || !std::isalnum(
                    static_cast<unsigned char>(lower[end]));
                if (left && right) ++matches;
                at = end;
            }
        }
        return matches;
    };

    const auto pt = count({"que", "nao", "uma", "com", "para", "como", "voce", "ola", "sim", "qual", "ceu"});
    const auto en = count({"the", "is", "are", "what", "with", "you", "hello", "yes", "how"});
    const auto es = count({"que", "no", "una", "con", "para", "como", "hola", "si", "que", "el", "es", "cielo", "azul"});
    if (pt == 0 && en == 0 && es == 0) return DetectedLanguage::unknown;
    if (pt >= en && pt >= es) return DetectedLanguage::portuguese;
    if (en >= es) return DetectedLanguage::english;
    return DetectedLanguage::spanish;
}

model::G1GenerationReport generate_for_profile(
    const model::G1Parameters& parameters, const common::Utf8Text& prompt,
    LanguageProfile profile, std::size_t max_new_bytes) {
    switch (profile) {
    case LanguageProfile::tsukuyomi: {
        g1::tsukuyomi::Policy policy{};
        policy.max_new_bytes = max_new_bytes;
        return g1::tsukuyomi::generate(parameters, prompt, policy);
    }
    case LanguageProfile::apollo: {
        g1::apollo::Policy policy{};
        policy.max_new_bytes = max_new_bytes;
        return g1::apollo::generate(parameters, prompt, policy);
    }
    case LanguageProfile::mokosh: {
        g1::mokosh::Policy policy{};
        policy.max_new_bytes = max_new_bytes;
        return g1::mokosh::generate(parameters, prompt, policy);
    }
    }
    throw std::invalid_argument("unknown IA-LANG G1 profile");
}

} // namespace

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
    session_messages_.clear();
    session_languages_.clear();
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
        session_messages_.emplace(id.to_string(), std::vector<ConversationMessage>{});
        session_languages_.emplace(id.to_string(), DetectedLanguage::unknown);
    } catch (...) {
        session_contexts_.erase(id.to_string());
        session_messages_.erase(id.to_string());
        session_languages_.erase(id.to_string());
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
    session_messages_[session_id.to_string()].clear();
    session_languages_[session_id.to_string()] = DetectedLanguage::unknown;
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

    const auto normalized = normalize_input(text.view());
    if (normalized.empty())
        return common::Status::failure(common::Error::make(
            common::ErrorCode::invalid_argument,
            "language input is empty after normalization", "lang", "observe"));

    const auto key = request.session_id().to_string();
    const auto detected = identify_language(normalized);
    try {
        auto candidate_messages = session_messages_.at(key);
        candidate_messages.push_back(
            ConversationMessage{ConversationRole::user, normalized, detected});
        auto serialized = serialize_conversation(candidate_messages);
        if (serialized.size() > language_config_.max_context_bytes) {
            return common::Status::failure(common::Error::make(
                common::ErrorCode::resource_limit,
                "language session context capacity exceeded; reset or close the session",
                "lang", "observe"));
        }
        session_messages_.at(key) = std::move(candidate_messages);
        session_contexts_.at(key) = std::move(serialized);
        session_languages_[key] = detected;
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
    const std::size_t profile_limit = language_config_.profile == LanguageProfile::apollo
        ? 512U : (language_config_.profile == LanguageProfile::mokosh ? 128U : 256U);
    if (max_new_bytes == 0 || !resource_limits_.permits_generation(max_new_bytes) ||
        max_new_bytes > profile_limit) {
        return common::Result<LanguageGeneration>::failure(common::Error::make(
            common::ErrorCode::resource_limit,
            "generation length exceeds IA-CORE or selected G1 profile limits",
            "lang", "generate"));
    }
    const auto key = request.session_id().to_string();
    const auto& context = session_contexts_.at(key);
    auto prompt = common::Utf8Text::from_utf8(context);
    if (!prompt)
        return common::Result<LanguageGeneration>::failure(*prompt.error_if());
    try {
        const auto generated = generate_for_profile(
            parameters_, *prompt.value_if(), language_config_.profile, max_new_bytes);
        if (request.is_cancelled())
            return common::Result<LanguageGeneration>::failure(common::Error::make(
                common::ErrorCode::cancelled,
                "language request was cancelled", "lang", "generate"));
        std::string answer = generated.continuation.string();
        while (!answer.empty() && (answer.back() == '\n' || answer.back() == '\r'))
            answer.pop_back();
        auto candidate_messages = session_messages_.at(key);
        candidate_messages.push_back(
            ConversationMessage{ConversationRole::assistant, std::move(answer),
                                session_languages_.at(key)});
        auto serialized = serialize_conversation(candidate_messages);
        if (serialized.size() > language_config_.max_context_bytes) {
            return common::Result<LanguageGeneration>::failure(common::Error::make(
                common::ErrorCode::resource_limit,
                "generated continuation would exceed session context capacity",
                "lang", "generate"));
        }
        session_messages_.at(key) = std::move(candidate_messages);
        session_contexts_.at(key) = std::move(serialized);
        LanguageGeneration result{
            generated.continuation, generated.generated_bytes,
            generated.candidate_evaluations, generated.reached_byte_limit,
            session_languages_.at(key)};
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
    session_languages_.erase(session_id.to_string());
    session_messages_.erase(session_id.to_string());
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
