#pragma once

#include "morok/common/error.hpp"
#include "morok/common/result.hpp"

#include <cstddef>
#include <string>

namespace morok::core {

struct ResourceLimits final {
    // G1 defaults: 512 MiB target RSS and a 1 GiB hard process budget.
    std::size_t target_memory_bytes{512ULL * 1024ULL * 1024ULL};
    std::size_t hard_memory_bytes{1024ULL * 1024ULL * 1024ULL};
    std::size_t max_input_bytes{1024ULL * 1024ULL};
    std::size_t max_generation_bytes{256};
    std::size_t max_active_requests{1};

    [[nodiscard]] common::Status validate() const {
        if (target_memory_bytes == 0 || hard_memory_bytes == 0 ||
            target_memory_bytes > hard_memory_bytes ||
            max_input_bytes == 0 || max_generation_bytes == 0 ||
            max_active_requests == 0) {
            return common::Status::failure(common::Error::make(
                common::ErrorCode::invalid_argument,
                "invalid resource limits", "core", "configuration"));
        }
        return common::Status::success();
    }

    [[nodiscard]] bool permits_input(std::size_t bytes) const noexcept {
        return bytes <= max_input_bytes;
    }

    [[nodiscard]] bool permits_generation(std::size_t bytes) const noexcept {
        return bytes <= max_generation_bytes;
    }

    [[nodiscard]] bool permits_memory(std::size_t resident_bytes) const noexcept {
        return resident_bytes <= hard_memory_bytes;
    }
};

} // namespace morok::core
