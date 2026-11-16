#pragma once

#include "morok/common/result.hpp"
#include "morok/g3/inference/generator.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace morok::g3::inference {

struct BenchmarkConfig final {
    std::size_t warmup_runs{2};
    std::size_t measured_runs{10};
};

struct BenchmarkReport final {
    std::size_t measured_runs{0};
    std::uint32_t state_bytes_per_session{0};
    std::uint64_t resident_bytes_before{0};
    std::uint64_t resident_bytes_after{0};
    std::uint64_t peak_resident_bytes{0};
    std::uint64_t median_latency_ns{0};
    std::uint64_t p95_latency_ns{0};
    double output_bytes_per_second{0.0};
    std::vector<std::uint64_t> latency_samples_ns;
};

// Runs the real supplied backend. RSS fields are zero on platforms where the
// process memory counters are unavailable; state bytes are always reported.
[[nodiscard]] common::Result<BenchmarkReport> benchmark_inference(
    const model::ModelBackend& backend,
    std::span<std::byte> session_storage,
    std::string_view input,
    const GenerationConfig& generation,
    const BenchmarkConfig& benchmark,
    std::mt19937_64& random);

} // namespace morok::g3::inference
