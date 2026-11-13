#include "morok/common/error.hpp"

#include <utility>

namespace morok::common {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::invalid_argument: return "INVALID_ARGUMENT";
    case ErrorCode::invalid_utf8: return "INVALID_UTF8";
    case ErrorCode::input_too_large: return "INPUT_TOO_LARGE";
    case ErrorCode::resource_limit: return "RESOURCE_LIMIT";
    case ErrorCode::out_of_memory: return "OUT_OF_MEMORY";
    case ErrorCode::model_not_loaded: return "MODEL_NOT_LOADED";
    case ErrorCode::model_version_mismatch: return "MODEL_VERSION_MISMATCH";
    case ErrorCode::checkpoint_corrupt: return "CHECKPOINT_CORRUPT";
    case ErrorCode::permission_denied: return "PERMISSION_DENIED";
    case ErrorCode::confirmation_required: return "CONFIRMATION_REQUIRED";
    case ErrorCode::tool_timeout: return "TOOL_TIMEOUT";
    case ErrorCode::tool_failure: return "TOOL_FAILURE";
    case ErrorCode::cancelled: return "CANCELLED";
    case ErrorCode::internal_error: return "INTERNAL_ERROR";
    }
    return "UNKNOWN_ERROR";
}

Error Error::make(
    ErrorCode code,
    std::string message,
    std::string subsystem,
    std::string stage) {
    return Error{
        .code = code,
        .request_id = std::nullopt,
        .subsystem = std::move(subsystem),
        .stage = std::move(stage),
        .message = std::move(message)
    };
}

} // namespace morok::common
