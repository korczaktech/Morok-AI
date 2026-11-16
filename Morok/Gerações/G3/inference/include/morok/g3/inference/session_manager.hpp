#pragma once

#include "morok/g3/inference/generator.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace morok::g3::inference {

// Owns isolated recurrent state and RNG per conversation/session ID.
// Calls for one ID are serialized; different IDs may run concurrently.
class SessionManager final {
public:
    explicit SessionManager(std::size_t maximum_sessions = 1024U,
                            std::chrono::seconds idle_ttl = std::chrono::minutes{30},
                            std::uint64_t seed = 0x4D4F524F4B4733ULL);

    [[nodiscard]] common::Result<GenerationResult> generate(
        const model::ModelBackend& backend,
        std::string_view session_id,
        std::string_view input,
        const GenerationConfig& config = {},
        const std::function<bool()>& cancellation_requested = {});

    [[nodiscard]] common::Status reset(const model::ModelBackend& backend,
                                     std::string_view session_id);
    [[nodiscard]] bool erase(std::string_view session_id);
    void clear();
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t prune_expired();

private:
    struct Entry final {
        explicit Entry(const model::ModelDescriptor& descriptor,
                       std::uint64_t seed);
        std::vector<std::byte> state;
        std::mt19937_64 random;
        std::mutex mutex;
        std::chrono::steady_clock::time_point last_access;
        bool initialized{false};
        std::atomic<std::size_t> active_calls{0U};
    };

    void prune_expired_locked(std::chrono::steady_clock::time_point now);

    const std::size_t maximum_sessions_;
    const std::chrono::seconds idle_ttl_;
    const std::uint64_t seed_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Entry>> sessions_;
};

} // namespace morok::g3::inference
