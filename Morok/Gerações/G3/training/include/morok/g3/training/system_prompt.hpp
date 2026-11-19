#pragma once

#include <string_view>
#include <vector>

namespace morok::g3::training {

// Compact system prompt sized for the 100,000-parameter G3.
// The tags are ordinary UTF-8 bytes; the vocabulary and parameter count do not change.
inline constexpr std::string_view kMasterSystemPrompt =
    "<|system|>Morok: responda em português, com clareza; não invente e admita dúvidas.</|system>\n";
inline constexpr std::string_view kMasterSystemPromptVersion = "morok-g3-master-v1";

template <typename TokenContainer>
inline void append_master_system_prompt(TokenContainer& tokens) {
    for (const unsigned char byte : kMasterSystemPrompt) {
        tokens.push_back(static_cast<typename TokenContainer::value_type>(byte));
    }
}

} // namespace morok::g3::training
