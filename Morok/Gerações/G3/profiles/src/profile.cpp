#include "morok/g3/profiles/profile.hpp"

#include <stdexcept>
#include <string>

namespace morok::g3::profiles {

ProfileConfig profile_config(ProfileId id) {
    ProfileConfig result;
    result.id = id;
    result.generation.reset_session_state = false;

    switch (id) {
    case ProfileId::mokosh:
        result.name = "Mokosh";
        result.agent_count = 1U;
        result.recommended_session_capacity = 8U;
        result.generation.temperature = 0.55;
        result.generation.top_k = 4U;
        result.generation.max_input_bytes = 2048U;
        result.generation.max_output_bytes = 192U;
        result.generation.timeout = std::chrono::milliseconds{2500};
        result.generation.repetition_ngram_min = 3U;
        result.generation.repetition_ngram_max = 4U;
        break;
    case ProfileId::tsukuyomi:
        result.name = "Tsukuyomi";
        result.agent_count = 1U;
        result.recommended_session_capacity = 16U;
        result.generation.temperature = 0.7;
        result.generation.top_k = 10U;
        result.generation.max_input_bytes = 4096U;
        result.generation.max_output_bytes = 512U;
        result.generation.timeout = std::chrono::milliseconds{5000};
        break;
    case ProfileId::artemis:
        result.name = "Artemis";
        result.agent_count = 3U;
        result.recommended_session_capacity = 48U;
        result.generation.temperature = 0.7;
        result.generation.top_k = 10U;
        result.generation.max_input_bytes = 4096U;
        result.generation.max_output_bytes = 384U;
        result.generation.timeout = std::chrono::milliseconds{5000};
        break;
    case ProfileId::apollo:
        result.name = "Apollo";
        result.agent_count = 1U;
        result.recommended_session_capacity = 16U;
        result.generation.temperature = 0.0;
        result.generation.top_k = 1U;
        result.generation.max_input_bytes = 2048U;
        result.generation.max_output_bytes = 256U;
        result.generation.timeout = std::chrono::milliseconds{2000};
        break;
    default:
        throw std::invalid_argument("unknown G3 profile");
    }
    return result;
}

common::Result<std::vector<AgentResponse>> run_profile(
    const model::ModelBackend& backend,
    inference::SessionManager& sessions,
    ProfileId profile,
    std::string_view request_id,
    std::string_view prompt,
    const std::function<bool()>& cancellation_requested) {
    if (request_id.empty() || request_id.size() > 128U)
        return common::Result<std::vector<AgentResponse>>::failure(
            common::Error::make(common::ErrorCode::invalid_argument,
                "profile request ID must contain 1 to 128 bytes",
                "g3.profiles", "request_id"));

    const auto config = profile_config(profile);
    std::vector<AgentResponse> responses;
    responses.reserve(config.agent_count);
    for (std::size_t index = 0; index < config.agent_count; ++index) {
        if (cancellation_requested && cancellation_requested())
            return common::Result<std::vector<AgentResponse>>::failure(
                common::Error::make(common::ErrorCode::cancelled,
                    "profile execution was cancelled",
                    "g3.profiles", "cancelled"));
        const auto session_id = std::string(request_id) + "/" +
            std::string(config.name) + "/agent-" + std::to_string(index + 1U);
        auto generated = sessions.generate(
            backend, session_id, prompt, config.generation,
            cancellation_requested);
        if (!generated)
            return common::Result<std::vector<AgentResponse>>::failure(
                *generated.error_if());
        AgentResponse response;
        response.agent_index = index;
        response.session_id = session_id;
        response.text = generated.value_if()->text;
        response.generation = std::move(*generated.value_if());
        responses.push_back(std::move(response));
    }
    return common::Result<std::vector<AgentResponse>>::success(std::move(responses));
}

} // namespace morok::g3::profiles
