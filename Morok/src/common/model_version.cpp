#include "morok/common/model_version.hpp"

#include <utility>

namespace morok::common {

Result<ModelVersion> ModelVersion::create(
    std::string architecture_id,
    std::uint64_t parameter_count,
    std::uint32_t format_version) {
    if (architecture_id.empty() || architecture_id.size() > 64U) {
        return Result<ModelVersion>::failure(Error::make(
            ErrorCode::invalid_argument,
            "Architecture ID must contain between 1 and 64 bytes.",
            "core",
            "model_version_validation"));
    }
    if (parameter_count == 0U || format_version == 0U) {
        return Result<ModelVersion>::failure(Error::make(
            ErrorCode::invalid_argument,
            "Parameter count and format version must be non-zero.",
            "core",
            "model_version_validation"));
    }

    return Result<ModelVersion>::success(ModelVersion(
        std::move(architecture_id), parameter_count, format_version));
}

const std::string& ModelVersion::architecture_id() const noexcept {
    return architecture_id_;
}

std::uint64_t ModelVersion::parameter_count() const noexcept {
    return parameter_count_;
}

std::uint32_t ModelVersion::format_version() const noexcept {
    return format_version_;
}

} // namespace morok::common
