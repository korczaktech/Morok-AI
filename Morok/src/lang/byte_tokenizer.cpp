#include "morok/lang/byte_tokenizer.hpp"

#include "morok/common/utf8_text.hpp"

#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace morok::lang {
namespace {

using morok::common::Error;
using morok::common::ErrorCode;
using morok::common::Result;
using morok::common::Status;

struct NormalizedText final {
    std::string text;
    bool replaced_invalid_utf8{false};
};

[[nodiscard]] Status fail(ErrorCode code, const char* stage,
                          const char* message) {
    return Status::failure(Error::make(code, message, "lang", stage));
}

template <typename T>
[[nodiscard]] Result<T> fail_result(ErrorCode code, const char* stage,
                                    const char* message) {
    return Result<T>::failure(Error::make(code, message, "lang", stage));
}

[[nodiscard]] Result<NormalizedText> normalize_utf8(
    std::string_view input, const TextCodecConfig& config) {
    if ((config.invalid_utf8 != InvalidUtf8Policy::replace &&
         config.invalid_utf8 != InvalidUtf8Policy::reject) ||
        (config.normalization != UnicodeNormalization::none &&
         config.normalization != UnicodeNormalization::nfc)) {
        return fail_result<NormalizedText>(
            ErrorCode::invalid_argument, "byte_tokenizer.config",
            "Unknown UTF-8 or Unicode normalization policy");
    }
    if (input.size() > static_cast<std::size_t>(
                           std::numeric_limits<int32_t>::max())) {
        return fail_result<NormalizedText>(
            ErrorCode::input_too_large, "byte_tokenizer.input",
            "UTF-8 input exceeds ICU's supported length");
    }

    if (config.invalid_utf8 == InvalidUtf8Policy::reject) {
        const auto strict = morok::common::Utf8Text::from_utf8(input);
        if (!strict) {
            return fail_result<NormalizedText>(
                ErrorCode::invalid_utf8, "byte_tokenizer.utf8",
                "Input contains an invalid UTF-8 sequence");
        }
    }

    const auto input_length = static_cast<int32_t>(input.size());
    const char* input_data = input.empty() ? "" : input.data();
    UErrorCode status = U_ZERO_ERROR;
    int32_t utf16_length = 0;
    int32_t substitutions = 0;
    u_strFromUTF8WithSub(nullptr, 0, &utf16_length, input_data,
                         input_length, 0xFFFD, &substitutions, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
        return fail_result<NormalizedText>(
            ErrorCode::invalid_utf8, "byte_tokenizer.utf8",
            "Unable to convert input from UTF-8");
    }

    status = U_ZERO_ERROR;
    std::vector<UChar> utf16(static_cast<std::size_t>(utf16_length) + 1U);
    substitutions = 0;
    u_strFromUTF8WithSub(utf16.data(),
                         static_cast<int32_t>(utf16.size()), &utf16_length,
                         input_data, input_length, 0xFFFD, &substitutions,
                         &status);
    if (U_FAILURE(status)) {
        return fail_result<NormalizedText>(
            ErrorCode::invalid_utf8, "byte_tokenizer.utf8",
            "Unable to decode UTF-8 input");
    }

    const UChar* output_data = utf16.data();
    int32_t output_length = utf16_length;
    std::vector<UChar> normalized;

    if (config.normalization == UnicodeNormalization::nfc) {
        status = U_ZERO_ERROR;
        const UNormalizer2* nfc = unorm2_getNFCInstance(&status);
        if (U_FAILURE(status) || nfc == nullptr) {
            return fail_result<NormalizedText>(
                ErrorCode::internal_error, "byte_tokenizer.nfc",
                "Unicode NFC normalizer is unavailable");
        }

        int32_t normalized_length = unorm2_normalize(
            nfc, utf16.data(), utf16_length, nullptr, 0, &status);
        if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
            return fail_result<NormalizedText>(
                ErrorCode::internal_error, "byte_tokenizer.nfc",
                "Unable to calculate normalized UTF-16 length");
        }

        status = U_ZERO_ERROR;
        normalized.resize(static_cast<std::size_t>(normalized_length) + 1U);
        output_length = unorm2_normalize(
            nfc, utf16.data(), utf16_length, normalized.data(),
            static_cast<int32_t>(normalized.size()), &status);
        if (U_FAILURE(status)) {
            return fail_result<NormalizedText>(
                ErrorCode::internal_error, "byte_tokenizer.nfc",
                "Unicode NFC normalization failed");
        }
        output_data = normalized.data();
    } else if (config.normalization != UnicodeNormalization::none) {
        return fail_result<NormalizedText>(
            ErrorCode::invalid_argument, "byte_tokenizer.config",
            "Unknown Unicode normalization policy");
    }

    status = U_ZERO_ERROR;
    int32_t utf8_length = 0;
    u_strToUTF8(nullptr, 0, &utf8_length, output_data, output_length, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
        return fail_result<NormalizedText>(
            ErrorCode::internal_error, "byte_tokenizer.utf8",
            "Unable to calculate normalized UTF-8 length");
    }

    status = U_ZERO_ERROR;
    std::string output(static_cast<std::size_t>(utf8_length) + 1U, '\0');
    int32_t written = 0;
    u_strToUTF8(output.data(), static_cast<int32_t>(output.size()), &written,
                output_data, output_length, &status);
    if (U_FAILURE(status)) {
        return fail_result<NormalizedText>(
            ErrorCode::internal_error, "byte_tokenizer.utf8",
            "Unable to encode normalized UTF-8 output");
    }
    output.resize(static_cast<std::size_t>(written));

    return Result<NormalizedText>::success(
        NormalizedText{std::move(output), substitutions > 0});
}

[[nodiscard]] TokenId role_token(MessageRole role) noexcept {
    return role == MessageRole::user ? kUsrToken : kBotToken;
}

} // namespace

Status ByteTokenizer::validate_vocabulary() {
    if (kPadToken != 256 || kBosToken != 257 || kEosToken != 258 ||
        kUsrToken != 259 || kBotToken != 260 ||
        kVocabularySize != 261) {
        return fail(ErrorCode::internal_error, "byte_tokenizer.vocabulary",
                    "G3 vocabulary IDs do not match the stable 261-token contract");
    }
    return Status::success();
}

Result<EncodedText> ByteTokenizer::encode_text(
    std::string_view text, const TextCodecConfig& config) {
    const auto normalized = normalize_utf8(text, config);
    if (!normalized) {
        return Result<EncodedText>::failure(*normalized.error_if());
    }

    EncodedText encoded;
    if (!text.empty()) {
        encoded.original_text.assign(text.data(), text.size());
    }
    encoded.normalized_text = normalized.value_if()->text;
    encoded.replaced_invalid_utf8 = normalized.value_if()->replaced_invalid_utf8;
    encoded.byte_tokens.reserve(encoded.normalized_text.size());
    for (const unsigned char byte : encoded.normalized_text) {
        encoded.byte_tokens.push_back(static_cast<TokenId>(byte));
    }
    return Result<EncodedText>::success(std::move(encoded));
}

Result<std::vector<TokenId>> ByteTokenizer::encode_message(
    std::string_view text, MessageRole role, const TextCodecConfig& config,
    bool include_bos, bool include_eos) {
    if (role != MessageRole::user && role != MessageRole::bot) {
        return fail_result<std::vector<TokenId>>(
            ErrorCode::invalid_argument, "byte_tokenizer.role",
            "Message role must be user or bot");
    }

    const auto encoded = encode_text(text, config);
    if (!encoded) {
        return Result<std::vector<TokenId>>::failure(*encoded.error_if());
    }

    std::vector<TokenId> result;
    result.reserve(encoded.value_if()->byte_tokens.size() +
                   static_cast<std::size_t>(include_bos) +
                   static_cast<std::size_t>(include_eos) + 1U);
    if (include_bos) {
        result.push_back(kBosToken);
    }
    result.push_back(role_token(role));
    result.insert(result.end(), encoded.value_if()->byte_tokens.begin(),
                  encoded.value_if()->byte_tokens.end());
    if (include_eos) {
        result.push_back(kEosToken);
    }
    return Result<std::vector<TokenId>>::success(std::move(result));
}

Status ByteTokenizer::append_message(
    std::vector<TokenId>& sequence, std::string_view text, MessageRole role,
    const TextCodecConfig& config) {
    if (role != MessageRole::user && role != MessageRole::bot) {
        return fail(ErrorCode::invalid_argument, "byte_tokenizer.role",
                    "Message role must be user or bot");
    }

    const auto encoded = encode_text(text, config);
    if (!encoded) {
        return Status::failure(*encoded.error_if());
    }

    sequence.reserve(sequence.size() + encoded.value_if()->byte_tokens.size() + 1U);
    sequence.push_back(role_token(role));
    sequence.insert(sequence.end(), encoded.value_if()->byte_tokens.begin(),
                    encoded.value_if()->byte_tokens.end());
    return Status::success();
}

Result<std::string> ByteTokenizer::decode_text(
    std::span<const TokenId> tokens, const TextCodecConfig& config) {
    std::string bytes;
    bytes.reserve(tokens.size());
    for (const TokenId token : tokens) {
        if (token == kEosToken) {
            break;
        }
        if (!is_byte_token(token)) {
            return fail_result<std::string>(
                ErrorCode::invalid_argument, "byte_tokenizer.decode",
                "Text decoding accepts byte IDs and an optional EOS only");
        }
        bytes.push_back(static_cast<char>(token));
    }

    const auto normalized = normalize_utf8(bytes, config);
    if (!normalized) {
        return Result<std::string>::failure(*normalized.error_if());
    }
    return Result<std::string>::success(normalized.value_if()->text);
}

} // namespace morok::lang
