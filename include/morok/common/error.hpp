#pragma once

#include "morok/common/ids.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace morok::common {

enum class ErrorCode {
    invalid_argument,
    invalid_utf8,
    input_too_large,
    resource_limit,
    out_of_memory,
    model_not_loaded,
    model_version_mismatch,
    checkpoint_corrupt,
    permission_denied,
    confirmation_required,
    tool_timeout,
    tool_failure,
    cancelled,
    internal_error
};

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

struct Error final {
    ErrorCode code{ErrorCode::internal_error};
    std::optional<RequestId> request_id{};
    std::string subsystem{};
    std::string stage{};
    std::string message{};

    [[nodiscard]] static Error make(
        ErrorCode code,
        std::string message,
        std::string subsystem = {},
        std::string stage = {});
};

} // namespace morok::common
