#include "morok/common/byte_buffer.hpp"
#include "morok/common/error.hpp"
#include "morok/common/ids.hpp"
#include "morok/common/model_version.hpp"
#include "morok/common/result.hpp"
#include "morok/common/timestamp.hpp"
#include "morok/common/utf8_text.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace {

int failures = 0;

void check(bool condition, std::string_view name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

void test_byte_buffer_and_utf8() {
    using morok::common::ByteBuffer;
    using morok::common::ErrorCode;
    using morok::common::Utf8Text;

    const ByteBuffer raw{std::byte{0x41}, std::byte{0x00}, std::byte{0x42}};
    const auto valid = Utf8Text::from_bytes(raw);
    check(valid.has_value(), "ASCII bytes including NUL are valid UTF-8");
    check(valid.value_if() != nullptr && valid.value_if()->size_bytes() == 3U,
          "Utf8Text preserves embedded NUL and byte length");

    const auto portuguese = Utf8Text::from_utf8("Morok — inteligência artificial");
    check(portuguese.has_value(), "valid multibyte UTF-8 is accepted");
    check(portuguese.value_if() != nullptr &&
              portuguese.value_if()->view() == "Morok — inteligência artificial",
          "Utf8Text preserves original text");

    const std::array<std::string_view, 6> invalid_sequences{
        std::string_view("\x80", 1),
        std::string_view("\xC0\xAF", 2),
        std::string_view("\xE0\x80\x80", 3),
        std::string_view("\xED\xA0\x80", 3),
        std::string_view("\xF4\x90\x80\x80", 4),
        std::string_view("\xF0\x9F\x92", 3)
    };
    for (const auto sequence : invalid_sequences) {
        const auto result = Utf8Text::from_utf8(sequence);
        check(!result.has_value(), "invalid UTF-8 sequence is rejected");
        check(result.error_if() != nullptr && result.error_if()->code == ErrorCode::invalid_utf8,
              "invalid UTF-8 returns INVALID_UTF8");
    }

    const auto empty = Utf8Text::from_utf8("");
    check(empty.has_value() && empty.value_if()->empty(), "empty UTF-8 text is valid");
}

void test_result_and_errors() {
    using namespace morok::common;

    const auto success = Result<int>::success(42);
    check(success.has_value(), "Result<T> represents success");
    check(success.value_if() != nullptr && *success.value_if() == 42,
          "Result<T> exposes successful value");
    check(success.error_if() == nullptr, "successful Result has no error");

    const auto failure = Result<int>::failure(Error::make(
        ErrorCode::resource_limit, "budget exceeded", "learn", "batch"));
    check(!failure.has_value(), "Result<T> represents failure");
    check(failure.error_if() != nullptr &&
              failure.error_if()->code == ErrorCode::resource_limit,
          "Result<T> preserves typed error");
    check(failure.error_if() != nullptr && failure.error_if()->subsystem == "learn",
          "Error preserves subsystem context");

    const Status ok = Status::success();
    const Status failed = Status::failure(
        Error::make(ErrorCode::cancelled, "operation cancelled"));
    check(ok.has_value(), "Status represents success");
    check(!failed.has_value() && failed.error_if()->code == ErrorCode::cancelled,
          "Status represents typed failure");
    check(to_string(ErrorCode::invalid_utf8) == "INVALID_UTF8",
          "error code has stable external representation");
}

void test_identifiers() {
    using namespace morok::common;

    static_assert(!std::is_same_v<SessionId, MemoryId>);
    static_assert(!std::is_same_v<SessionId, RequestId>);

    const auto session = SessionId::generate();
    const auto another_session = SessionId::generate();
    const auto memory = MemoryId::generate();
    const auto request = RequestId::generate();

    check(session.has_value(), "SessionId can be generated");
    check(another_session.has_value(), "second SessionId can be generated");
    check(memory.has_value(), "MemoryId can be generated");
    check(request.has_value(), "RequestId can be generated");

    if (session && another_session && memory && request) {
        check(!session.value_if()->is_nil(), "generated SessionId is non-nil");
        check(session.value_if()->to_string().size() == 36U,
              "identifier uses canonical UUID text length");
        check(*session.value_if() != *another_session.value_if(),
              "independently generated SessionIds are distinct in test sample");
        check(memory.value_if()->to_string().size() == 36U,
              "MemoryId has canonical UUID text representation");
        check(request.value_if()->to_string().size() == 36U,
              "RequestId has canonical UUID text representation");
    }
}

void test_model_version_and_timestamp() {
    using namespace morok::common;

    const auto version = ModelVersion::create("RLAH-G1-fastslow-d11-bit8", 1000U, 1U);
    check(version.has_value(), "valid model version is accepted");
    check(version.value_if() != nullptr &&
              version.value_if()->parameter_count == 1000U,
          "model version stores parameter inventory");

    const auto invalid = ModelVersion::create("", 1000U, 1U);
    check(!invalid.has_value() && invalid.error_if()->code == ErrorCode::invalid_argument,
          "empty architecture ID is rejected");
    const auto zero_parameters = ModelVersion::create("RLAH-G1", 0U, 1U);
    check(!zero_parameters.has_value(), "zero parameter count is rejected");

    const auto timestamp = Timestamp::from_unix_nanoseconds(123456789);
    check(timestamp.unix_nanoseconds() == 123456789,
          "timestamp preserves Unix nanoseconds");
    check(Timestamp::now().unix_nanoseconds() > 0,
          "current timestamp is initialized");
}

} // namespace

int main() {
    test_byte_buffer_and_utf8();
    test_result_and_errors();
    test_identifiers();
    test_model_version_and_timestamp();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All shared type tests passed.\n";
    return EXIT_SUCCESS;
}
