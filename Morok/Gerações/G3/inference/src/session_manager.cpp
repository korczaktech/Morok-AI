#include "morok/g3/inference/session_manager.hpp"

#include "morok/common/error.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace morok::g3::inference {
namespace {

common::Error session_error(common::ErrorCode code, const char* stage,
                            const char* message) {
    return common::Error::make(code, message, "g3.session_manager", stage);
}

} // namespace

SessionManager::Entry::Entry(const model::ModelDescriptor& descriptor,
                             std::uint64_t seed)
    : state(descriptor.state_size_bytes),
      random(seed),
      last_access(std::chrono::steady_clock::now()) {}

SessionManager::SessionManager(std::size_t maximum_sessions,
                               std::chrono::seconds idle_ttl,
                               std::uint64_t seed)
    : maximum_sessions_(maximum_sessions),
      idle_ttl_(idle_ttl),
      seed_(seed) {
    if (maximum_sessions_ == 0U || idle_ttl_.count() <= 0)
        throw std::invalid_argument("G3 session manager limits must be positive");
}

void SessionManager::prune_expired_locked(
    std::chrono::steady_clock::time_point now) {
    for (auto iterator = sessions_.begin(); iterator != sessions_.end();) {
        const auto& entry = iterator->second;
        if (entry->active_calls.load(std::memory_order_acquire) == 0U &&
            now - entry->last_access >= idle_ttl_)
            iterator = sessions_.erase(iterator);
        else
            ++iterator;
    }
}

common::Result<GenerationResult> SessionManager::generate(
    const model::ModelBackend& backend,
    std::string_view session_id,
    std::string_view input,
    const GenerationConfig& config,
    const std::function<bool()>& cancellation_requested) {
    if (session_id.empty() || session_id.size() > 256U)
        return common::Result<GenerationResult>::failure(session_error(
            common::ErrorCode::invalid_argument, "session.id",
            "Session ID must contain between 1 and 256 bytes"));

    const auto descriptor = backend.descriptor();
    if (!descriptor.valid())
        return common::Result<GenerationResult>::failure(session_error(
            common::ErrorCode::model_version_mismatch, "session.backend",
            "Cannot create a session for an invalid model backend"));

    std::shared_ptr<Entry> entry;
    {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        prune_expired_locked(now);
        auto found = sessions_.find(std::string(session_id));
        if (found == sessions_.end()) {
            if (sessions_.size() >= maximum_sessions_)
                return common::Result<GenerationResult>::failure(session_error(
                    common::ErrorCode::resource_limit, "session.capacity",
                    "Maximum number of active G3 sessions has been reached"));
            const auto hash = static_cast<std::uint64_t>(
                std::hash<std::string_view>{}(session_id));
            entry = std::make_shared<Entry>(descriptor, seed_ ^ hash);
            sessions_.emplace(std::string(session_id), entry);
        } else {
            entry = found->second;
            if (entry->state.size() != descriptor.state_size_bytes)
                return common::Result<GenerationResult>::failure(session_error(
                    common::ErrorCode::model_version_mismatch, "session.architecture",
                    "Session state belongs to a backend with a different state size"));
        }
        entry->last_access = now;
        entry->active_calls.fetch_add(1U, std::memory_order_acq_rel);
    }

    std::lock_guard session_lock(entry->mutex);
    GenerationConfig effective = config;
    effective.reset_session_state = !entry->initialized;
    auto result = generate(backend, entry->state, input, effective,
                           entry->random, cancellation_requested);
    {
        std::lock_guard lock(mutex_);
        entry->last_access = std::chrono::steady_clock::now();
        entry->active_calls.fetch_sub(1U, std::memory_order_acq_rel);
    }
    if (result) {
        entry->initialized = true;
    } else {
        // A cancelled or failed generation may have consumed only part of the
        // prompt/answer. Reset on the next call rather than reuse partial state.
        entry->initialized = false;
        (void)backend.reset_state(entry->state);
    }
    return result;
}

common::Status SessionManager::reset(const model::ModelBackend& backend,
                                     std::string_view session_id) {
    std::shared_ptr<Entry> entry;
    {
        std::lock_guard lock(mutex_);
        const auto found = sessions_.find(std::string(session_id));
        if (found == sessions_.end())
            return common::Status::failure(session_error(
                common::ErrorCode::model_not_loaded, "session.not_found",
                "Requested G3 session does not exist"));
        entry = found->second;
        entry->active_calls.fetch_add(1U, std::memory_order_acq_rel);
    }
    const auto descriptor = backend.descriptor();
    if (!descriptor.valid() || entry->state.size() != descriptor.state_size_bytes) {
        entry->active_calls.fetch_sub(1U, std::memory_order_acq_rel);
        return common::Status::failure(session_error(
            common::ErrorCode::model_version_mismatch, "session.architecture",
            "Session state is incompatible with the supplied backend"));
    }
    std::lock_guard session_lock(entry->mutex);
    const auto status = backend.reset_state(entry->state);
    entry->initialized = static_cast<bool>(status);
    {
        std::lock_guard lock(mutex_);
        entry->last_access = std::chrono::steady_clock::now();
        entry->active_calls.fetch_sub(1U, std::memory_order_acq_rel);
    }
    return status;
}

bool SessionManager::erase(std::string_view session_id) {
    std::lock_guard lock(mutex_);
    return sessions_.erase(std::string(session_id)) != 0U;
}

void SessionManager::clear() {
    std::lock_guard lock(mutex_);
    sessions_.clear();
}

std::size_t SessionManager::size() const {
    std::lock_guard lock(mutex_);
    return sessions_.size();
}

std::size_t SessionManager::prune_expired() {
    std::lock_guard lock(mutex_);
    const auto before = sessions_.size();
    prune_expired_locked(std::chrono::steady_clock::now());
    return before - sessions_.size();
}

} // namespace morok::g3::inference
