#include "morok/lang/byte_tokenizer.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

void test_vocabulary_contract() {
    using namespace morok::lang;

    check(ByteTokenizer::vocabulary_size() == 261U,
          "G3 vocabulary contains exactly 261 IDs");
    check(kPadToken == 256U && kBosToken == 257U && kEosToken == 258U &&
              kUsrToken == 259U && kBotToken == 260U,
          "special token IDs are stable and distinct");
    check(ByteTokenizer::is_byte_token(0U) &&
              ByteTokenizer::is_byte_token(255U),
          "all raw byte IDs are represented");
    check(!ByteTokenizer::is_byte_token(kPadToken),
          "special IDs are not treated as byte IDs");
    check(ByteTokenizer::is_special_token(kPadToken) &&
              ByteTokenizer::is_special_token(kBotToken),
          "control IDs are recognized as special tokens");
    check(ByteTokenizer::is_valid_token(kBotToken) &&
              !ByteTokenizer::is_valid_token(261U),
          "IDs outside the vocabulary are rejected");
    check(ByteTokenizer::validate_vocabulary().has_value(),
          "vocabulary contract validates");
}

void test_byte_encoding_and_message_roles() {
    using namespace morok::lang;

    const auto encoded = ByteTokenizer::encode_text("Olá");
    check(encoded.has_value(), "Portuguese UTF-8 text is encoded");
    if (encoded) {
        const auto& value = *encoded.value_if();
        const std::vector<TokenId> expected{0x4FU, 0x6CU, 0xC3U, 0xA1U};
        check(value.byte_tokens == expected,
              "text is represented as its exact UTF-8 byte sequence");
        check(value.original_text == "Olá",
              "original input is retained for diagnostics");
        check(value.normalized_text == "Olá",
              "normalized text is retained for downstream processing");
        check(!value.replaced_invalid_utf8,
              "valid UTF-8 does not report replacement");
    }

    const auto user = ByteTokenizer::encode_message("Oi", MessageRole::user);
    check(user.has_value(), "user message is encoded");
    if (user) {
        const std::vector<TokenId> expected{
            kBosToken, kUsrToken, 0x4FU, 0x69U, kEosToken};
        check(*user.value_if() == expected,
              "user sequence has BOS, USR, bytes and EOS");
    }

    const auto bot = ByteTokenizer::encode_message(
        "Olá", MessageRole::bot, {}, false, false);
    check(bot.has_value(), "bot message can be appended without sequence markers");
    if (bot) {
        check(bot.value_if()->front() == kBotToken &&
                  bot.value_if()->back() == 0xA1U,
              "bot role uses its dedicated control token");
    }

    std::vector<TokenId> conversation{kBosToken};
    auto status = ByteTokenizer::append_message(
        conversation, "Oi", MessageRole::user);
    status = status.has_value()
        ? ByteTokenizer::append_message(conversation, "Olá", MessageRole::bot)
        : status;
    check(status.has_value(), "multiple messages append to one sequence");
    check(conversation.size() == 1U + 1U + 2U + 1U + 4U,
          "conversation append does not inject extra BOS/EOS markers");
    check(conversation.size() > 1U && conversation[1] == kUsrToken &&
              conversation[4] == kBotToken,
          "conversation preserves user/bot role boundaries");
}

void test_unicode_normalization() {
    using namespace morok::lang;

    const std::string decomposed{"e\xCC\x81", 3};
    const auto normalized = ByteTokenizer::encode_text(decomposed);
    check(normalized.has_value(), "decomposed Unicode input is accepted");
    if (normalized) {
        check(normalized.value_if()->normalized_text == "\xC3\xA9",
              "NFC composes equivalent Unicode sequences");
        check(normalized.value_if()->byte_tokens ==
                  std::vector<TokenId>{0xC3U, 0xA9U},
              "token IDs represent normalized NFC bytes");
    }

    TextCodecConfig preserve;
    preserve.normalization = UnicodeNormalization::none;
    const auto unchanged = ByteTokenizer::encode_text(decomposed, preserve);
    check(unchanged.has_value(), "normalization can be disabled");
    if (unchanged) {
        check(unchanged.value_if()->normalized_text == decomposed,
              "disabled normalization preserves original valid UTF-8 bytes");
    }
}

void test_invalid_utf8_policies() {
    using namespace morok::lang;

    const std::string malformed{"A\xF0\x28\x8C\x28Z", 6};
    const auto replaced = ByteTokenizer::encode_text(malformed);
    check(replaced.has_value(), "default policy replaces malformed UTF-8");
    if (replaced) {
        check(replaced.value_if()->original_text == malformed,
              "malformed original bytes remain available for diagnostics");
        check(replaced.value_if()->replaced_invalid_utf8,
              "replacement occurrence is exposed to diagnostics");
        check(replaced.value_if()->normalized_text.find("\xEF\xBF\xBD") !=
                  std::string::npos,
              "invalid sequence is represented by U+FFFD");
    }

    TextCodecConfig strict;
    strict.invalid_utf8 = InvalidUtf8Policy::reject;
    const auto rejected = ByteTokenizer::encode_text(malformed, strict);
    check(!rejected.has_value(), "strict policy rejects malformed UTF-8");
    check(rejected.error_if() != nullptr &&
              rejected.error_if()->code ==
                  morok::common::ErrorCode::invalid_utf8,
          "strict policy reports INVALID_UTF8");
}

void test_decoding_and_control_validation() {
    using namespace morok::lang;

    const std::array<TokenId, 5> valid{
        0x4FU, 0x6CU, 0xC3U, 0xA1U, kEosToken};
    const auto decoded = ByteTokenizer::decode_text(valid);
    check(decoded.has_value() && *decoded.value_if() == "Olá",
          "byte IDs decode into validated UTF-8 and stop at EOS");

    const std::array<TokenId, 1> invalid_utf8{0xC3U};
    const auto replaced = ByteTokenizer::decode_text(invalid_utf8);
    check(replaced.has_value() &&
              replaced.value_if()->find("\xEF\xBF\xBD") != std::string::npos,
          "invalid generated UTF-8 is replaced before delivery");

    TextCodecConfig strict;
    strict.invalid_utf8 = InvalidUtf8Policy::reject;
    const auto rejected = ByteTokenizer::decode_text(invalid_utf8, strict);
    check(!rejected.has_value(), "strict output validation rejects invalid UTF-8");

    const std::array<TokenId, 1> pad{ kPadToken };
    const auto control_rejected = ByteTokenizer::decode_text(pad);
    check(!control_rejected.has_value(),
          "PAD/BOS/USR/BOT cannot leak into decoded user text");

    const std::array<TokenId, 2> after_eos{kEosToken, 0xFFU};
    const auto stopped = ByteTokenizer::decode_text(after_eos);
    check(stopped.has_value() && stopped.value_if()->empty(),
          "decoder stops at EOS without consuming later tokens");
}

} // namespace

int main() {
    test_vocabulary_contract();
    test_byte_encoding_and_message_roles();
    test_unicode_normalization();
    test_invalid_utf8_policies();
    test_decoding_and_control_validation();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All G3 byte-tokenizer tests passed.\n";
    return EXIT_SUCCESS;
}
