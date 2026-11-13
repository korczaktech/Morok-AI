#pragma once

#include "morok/common/result.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace morok::common {

// Validated, immutable identity for a model artifact.
class ModelVersion final {
public:
    [[nodiscard]] static Result<ModelVersion> create(
        std::string architecture_id,
        std::uint64_t parameter_count,
        std::uint32_t format_version);

    [[nodiscard]] const std::string& architecture_id() const noexcept;
    [[nodiscard]] std::uint64_t parameter_count() const noexcept;
    [[nodiscard]] std::uint32_t format_version() const noexcept;

private:
    ModelVersion(
        std::string architecture_id,
        std::uint64_t parameter_count,
        std::uint32_t format_version)
        : architecture_id_(std::move(architecture_id)),
          parameter_count_(parameter_count),
          format_version_(format_version) {}

    std::string architecture_id_;
    std::uint64_t parameter_count_{0};
    std::uint32_t format_version_{0};
};

} // namespace morok::common
