#pragma once

#include "morok/common/error.hpp"
#include "morok/common/result.hpp"
#include "morok/core/resource_limits.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace morok::core {

struct RuntimeConfig final {
    std::string runtime_name{"morok"};
    std::uint32_t contract_version{1};
    std::size_t max_sessions{1};
    ResourceLimits resources{};

    [[nodiscard]] common::Status validate() const {
        if (runtime_name.empty() || contract_version != 1 || max_sessions == 0) {
            return common::Status::failure(common::Error::make(
                common::ErrorCode::invalid_argument,
                "invalid runtime configuration", "core", "configuration"));
        }
        return resources.validate();
    }
};

} // namespace morok::core
