#pragma once

#include "morok/common/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace morok::lang {

using TokenId = std::uint16_t;

inline constexpr TokenId kPadToken = 256;
inline constexpr TokenId kBosToken = 257;
inline constexpr TokenId kEosToken = 258;
inline constexpr TokenId kUsrToken = 259;
inline constexpr TokenId kBotToken = 260;
inline constexpr std::size_t kByteVocabularySize = 256;
inline constexpr std::size_t kSpecialTokenCount = 5;
inline constexpr std::size_t kVocabularySize =
    kByteVocabularySize + kSpecialTokenCount;

enum class InvalidUtf8Policy {
    replace,
    reject
};

enum class UnicodeNormalization {
    none,
    nfc
};

enum class MessageRole {
    user,
    bot
};

struct TextCodecConfig final {
    InvalidUtf8Policy invalid_utf8{InvalidUtf8Policy::replace};
    UnicodeNormalization normalization{UnicodeNormalization::nfc};
};

struct EncodedText final {
    // One token per UTF-8 byte. Control tokens are added by encode_message().
    std::vector<TokenId> byte_tokens;
    // Exact caller input, retained for privacy-aware diagnostics and auditing.
    std::string original_text;
    // The exact normalized UTF-8 text represented by byte_tokens.
    std::string normalized_text;
    bool replaced_invalid_utf8{false};
};

class ByteTokenizer final {
public:
    [[nodiscard]] static constexpr std::size_t vocabulary_size() noexcept {
        return kVocabularySize;
    }

    [[nodiscard]] static constexpr bool is_byte_token(TokenId id) noexcept {
        return id < kByteVocabularySize;
    }

    [[nodiscard]] static constexpr bool is_special_token(TokenId id) noexcept {
        return id >= kPadToken && id <= kBotToken;
    }

    [[nodiscard]] static constexpr bool is_valid_token(TokenId id) noexcept {
        return id < kVocabularySize;
    }

    [[nodiscard]] static common::Status validate_vocabulary();

    [[nodiscard]] static common::Result<EncodedText> encode_text(
        std::string_view text,
        const TextCodecConfig& config = {});

    // A standalone message is [BOS?][USR|BOT][UTF-8 bytes][EOS?].
    [[nodiscard]] static common::Result<std::vector<TokenId>> encode_message(
        std::string_view text,
        MessageRole role,
        const TextCodecConfig& config = {},
        bool include_bos = true,
        bool include_eos = true);

    // Appends [USR|BOT][UTF-8 bytes] to an existing conversation sequence.
    // It does not add BOS/EOS, allowing multiple turns in one sequence.
    [[nodiscard]] static common::Status append_message(
        std::vector<TokenId>& sequence,
        std::string_view text,
        MessageRole role,
        const TextCodecConfig& config = {});

    // Decodes byte IDs and stops at EOS. PAD/BOS/USR/BOT are rejected.
    [[nodiscard]] static common::Result<std::string> decode_text(
        std::span<const TokenId> tokens,
        const TextCodecConfig& config = {});
};

} // namespace morok::lang
