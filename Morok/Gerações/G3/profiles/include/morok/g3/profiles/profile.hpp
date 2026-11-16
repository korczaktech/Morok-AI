#pragma once

#include "morok/g3/inference/session_manager.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace morok::g3::profiles {

enum class ProfileId {
    mokosh,
    tsukuyomi,
    artemis,
    apollo
};

struct ProfileConfig final {
    ProfileId id{ProfileId::tsukuyomi};
    std::string_view name{"Tsukuyomi"};
    std::string_view presentation_theme{"system"};
    std::size_t agent_count{1U};
    std::size_t recommended_session_capacity{16U};
    inference::GenerationConfig generation{};
};

struct AgentResponse final {
    std::size_t agent_index{0U};
    std::string session_id;
    std::string text;
    inference::GenerationResult generation;
};

[[nodiscard]] ProfileConfig profile_config(ProfileId id);

// Each agent receives an independent SessionManager entry and recurrent state.
// Artemis returns three separate results; it does not silently merge them.
[[nodiscard]] common::Result<std::vector<AgentResponse>> run_profile(
    const model::ModelBackend& backend,
    inference::SessionManager& sessions,
    ProfileId profile,
    std::string_view request_id,
    std::string_view prompt,
    const std::function<bool()>& cancellation_requested = {});

} // namespace morok::g3::profiles
