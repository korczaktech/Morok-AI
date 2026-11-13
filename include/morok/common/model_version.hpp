#pragma once

#include "morok/common/result.hpp"

#include <cstdint>
#include <string>

namespace morok::common {

struct ModelVersion final {
    std::string architecture_id;
    std::uint64_t parameter_count{0};
    std::uint32_t format_version{0};

    [[nodiscard]] static Result<ModelVersion> create(
        std::string architecture_id,
        std::uint64_t parameter_count,
        std::uint32_t format_version);
};

} // namespace morok::common
