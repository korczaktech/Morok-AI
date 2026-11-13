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

    return Result<ModelVersion>::success(ModelVersion{
        .architecture_id = std::move(architecture_id),
        .parameter_count = parameter_count,
        .format_version = format_version
    });
}

} // namespace morok::common
