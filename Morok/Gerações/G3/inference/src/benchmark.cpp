#include "morok/g3/inference/benchmark.hpp"

#include "morok/common/error.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

#if defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#include <fstream>
#endif

namespace morok::g3::inference {
namespace {

template <typename T>
common::Result<T> failure(common::ErrorCode code, const char* stage,
                           const char* message) {
    return common::Result<T>::failure(
        common::Error::make(code, message, "g3.benchmark", stage));
}

std::uint64_t resident_bytes() noexcept {
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::uint64_t total_pages = 0;
    std::uint64_t resident_pages = 0;
    if (statm >> total_pages >> resident_pages) {
        const long page_size = ::sysconf(_SC_PAGESIZE);
        if (page_size > 0) {
            return resident_pages * static_cast<std::uint64_t>(page_size);
        }
    }
#endif
    return 0U;
}

std::uint64_t peak_resident_bytes() noexcept {
#if defined(__linux__)
    struct rusage usage {};
    if (::getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0) {
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
    }
#endif
    return 0U;
}

std::uint64_t percentile(std::vector<std::uint64_t> samples, double fraction) {
    if (samples.empty()) return 0U;
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(samples.size()))) - 1U;
    return samples[std::min(index, samples.size() - 1U)];
}

} // namespace

common::Result<BenchmarkReport> benchmark_inference(
    const model::ModelBackend& backend,
    std::span<std::byte> session_storage,
    std::string_view input,
    const GenerationConfig& generation,
    const BenchmarkConfig& benchmark,
    std::mt19937_64& random) {
    if (benchmark.measured_runs == 0U || benchmark.measured_runs > 10000U ||
        benchmark.warmup_runs > 10000U) {
        return failure<BenchmarkReport>(
            common::ErrorCode::invalid_argument, "benchmark.config",
            "Benchmark run counts are outside the supported range");
    }

    BenchmarkReport report;
    report.state_bytes_per_session = backend.descriptor().state_size_bytes;
    report.resident_bytes_before = resident_bytes();

    for (std::size_t index = 0; index < benchmark.warmup_runs; ++index) {
        const auto warmup = generate(
            backend, session_storage, input, generation, random);
        if (!warmup) {
            return common::Result<BenchmarkReport>::failure(*warmup.error_if());
        }
    }

    std::uint64_t total_output_bytes = 0U;
    std::uint64_t total_elapsed_ns = 0U;
    report.latency_samples_ns.reserve(benchmark.measured_runs);
    for (std::size_t index = 0; index < benchmark.measured_runs; ++index) {
        const auto sample = generate(
            backend, session_storage, input, generation, random);
        if (!sample) {
            return common::Result<BenchmarkReport>::failure(*sample.error_if());
        }
        const auto elapsed = static_cast<std::uint64_t>(
            std::max<std::int64_t>(sample.value_if()->elapsed.count(), 1));
        report.latency_samples_ns.push_back(elapsed);
        total_elapsed_ns += elapsed;
        total_output_bytes += sample.value_if()->output_bytes;
    }

    report.measured_runs = benchmark.measured_runs;
    report.resident_bytes_after = resident_bytes();
    report.peak_resident_bytes = std::max(
        peak_resident_bytes(), report.resident_bytes_after);
    report.median_latency_ns = percentile(report.latency_samples_ns, 0.50);
    report.p95_latency_ns = percentile(report.latency_samples_ns, 0.95);
    if (total_elapsed_ns > 0U) {
        report.output_bytes_per_second =
            static_cast<double>(total_output_bytes) * 1.0e9 /
            static_cast<double>(total_elapsed_ns);
    }
    return common::Result<BenchmarkReport>::success(std::move(report));
}

} // namespace morok::g3::inference
