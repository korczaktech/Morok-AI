#include "morok/g3/profiles/profile.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace {

class DeterministicBackend final : public morok::model::ModelBackend {
public:
    morok::model::ModelDescriptor descriptor() const noexcept override {
        return {3U, 0x0302U, 261U, 100000U, sizeof(std::uint32_t),
                alignof(std::uint32_t), true};
    }
    morok::common::Status reset_state(std::span<std::byte> state) const override {
        if (state.size() != sizeof(std::uint32_t))
            return morok::common::Status::failure(morok::common::Error::make(
                morok::common::ErrorCode::invalid_argument, "invalid state"));
        const std::uint32_t zero = 0U;
        std::memcpy(state.data(), &zero, sizeof(zero));
        return morok::common::Status::success();
    }
    morok::common::Status consume_byte(
        std::span<std::byte> state, std::uint8_t) const override {
        std::uint32_t count = 0U;
        std::memcpy(&count, state.data(), sizeof(count));
        ++count;
        std::memcpy(state.data(), &count, sizeof(count));
        return morok::common::Status::success();
    }
    morok::common::Status next_token_log_probabilities(
        std::span<const std::byte> state, std::span<float> output) const override {
        if (output.size() != 261U || state.size() != sizeof(std::uint32_t))
            return morok::common::Status::failure(morok::common::Error::make(
                morok::common::ErrorCode::invalid_argument, "invalid output"));
        std::uint32_t count = 0U;
        std::memcpy(&count, state.data(), sizeof(count));
        std::fill(output.begin(), output.end(),
                  -std::numeric_limits<float>::infinity());
        output[count > 1U ? morok::lang::kEosToken
                          : static_cast<std::uint8_t>('A')] = 0.0F;
        return morok::common::Status::success();
    }
};

void test_profile_policy_contracts() {
    using namespace morok::g3::profiles;
    const auto mokosh = profile_config(ProfileId::mokosh);
    const auto tsukuyomi = profile_config(ProfileId::tsukuyomi);
    const auto artemis = profile_config(ProfileId::artemis);
    const auto apollo = profile_config(ProfileId::apollo);
    assert(mokosh.agent_count == 1U && mokosh.generation.top_k == 4U);
    assert(mokosh.generation.max_output_bytes < tsukuyomi.generation.max_output_bytes);
    assert(tsukuyomi.agent_count == 1U);
    assert(artemis.agent_count >= 3U);
    assert(apollo.agent_count == 1U);
    assert(apollo.generation.temperature == 0.0);
    assert(apollo.generation.top_k == 1U);
    assert(mokosh.recommended_session_capacity < tsukuyomi.recommended_session_capacity);
}

void test_profiles_share_backend_and_isolate_agent_sessions() {
    using namespace morok::g3::profiles;
    DeterministicBackend backend;
    morok::g3::inference::SessionManager sessions(64U);
    const auto mokosh = run_profile(backend, sessions, ProfileId::mokosh, "req-1", "x");
    const auto tsukuyomi = run_profile(backend, sessions, ProfileId::tsukuyomi, "req-2", "x");
    const auto artemis = run_profile(backend, sessions, ProfileId::artemis, "req-3", "x");
    const auto apollo = run_profile(backend, sessions, ProfileId::apollo, "req-4", "x");
    assert(mokosh && mokosh.value_if()->size() == 1U);
    assert(tsukuyomi && tsukuyomi.value_if()->size() == 1U);
    assert(artemis && artemis.value_if()->size() == 3U);
    assert(apollo && apollo.value_if()->size() == 1U);
    assert(sessions.size() == 6U);
    for (std::size_t i = 0; i < artemis.value_if()->size(); ++i) {
        assert((*artemis.value_if())[i].agent_index == i);
        assert(!(*artemis.value_if())[i].session_id.empty());
        assert((*artemis.value_if())[i].text == "A");
    }
    assert((*tsukuyomi.value_if())[0].session_id !=
           (*mokosh.value_if())[0].session_id);
}

} // namespace

int main() {
    test_profile_policy_contracts();
    test_profiles_share_backend_and_isolate_agent_sessions();
    return 0;
}
