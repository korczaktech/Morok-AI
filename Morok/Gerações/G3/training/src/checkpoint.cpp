#include "morok/g3/training/checkpoint.hpp"

#include "morok/common/error.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_set>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace morok::g3::training {
namespace {

using common::Error;
using common::ErrorCode;
using common::Result;
using common::Status;

constexpr std::array<std::byte, 8> kMagic{
    std::byte{'M'}, std::byte{'R'}, std::byte{'K'}, std::byte{'3'},
    std::byte{'C'}, std::byte{'K'}, std::byte{'P'}, std::byte{'T'}};
constexpr std::size_t kChecksumOffset = 28U;
constexpr std::size_t kMaximumStringBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumIndexCount = 10U * 1000U * 1000U;

Status fail(ErrorCode code, const char* stage, const char* message) {
    return Status::failure(Error::make(code, message, "g3.checkpoint", stage));
}

template <typename T>
Result<T> fail_result(ErrorCode code, const char* stage, const char* message) {
    return Result<T>::failure(Error::make(code, message, "g3.checkpoint", stage));
}

class Writer final {
public:
    std::vector<std::byte> data;

    template <typename T>
    void integer(T value) {
        static_assert(std::is_unsigned_v<T>);
        for (std::size_t i = 0; i < sizeof(T); ++i)
            data.push_back(static_cast<std::byte>((value >> (i * 8U)) & 0xffU));
    }

    void real(double value) { integer(std::bit_cast<std::uint64_t>(value)); }
    void real(float value) { integer(std::bit_cast<std::uint32_t>(value)); }

    void bytes(std::span<const std::byte> value) {
        integer<std::uint64_t>(static_cast<std::uint64_t>(value.size()));
        data.insert(data.end(), value.begin(), value.end());
    }

    void string(std::string_view value) {
        if (value.size() > kMaximumStringBytes)
            throw std::length_error("checkpoint string exceeds limit");
        integer<std::uint32_t>(static_cast<std::uint32_t>(value.size()));
        for (const char raw_byte : value)
            data.push_back(static_cast<std::byte>(static_cast<unsigned char>(raw_byte)));
    }

    template <typename T>
    void vector(const std::vector<T>& values) {
        integer<std::uint64_t>(static_cast<std::uint64_t>(values.size()));
        if constexpr (std::is_same_v<T, float>) {
            for (const float value : values) real(value);
        } else {
            for (const T value : values) integer<std::uint64_t>(value);
        }
    }
};

class Reader final {
public:
    explicit Reader(std::span<const std::byte> source) : source_(source) {}

    template <typename T>
    T integer() {
        static_assert(std::is_unsigned_v<T>);
        require(sizeof(T));
        T value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            value |= static_cast<T>(std::to_integer<std::uint8_t>(source_[offset_++]))
                     << (i * 8U);
        return value;
    }

    double real64() { return std::bit_cast<double>(integer<std::uint64_t>()); }
    float real32() { return std::bit_cast<float>(integer<std::uint32_t>()); }

    std::string string() {
        const auto size = integer<std::uint32_t>();
        if (size > kMaximumStringBytes) throw std::length_error("checkpoint string exceeds limit");
        require(size);
        std::string value;
        value.resize(size);
        for (std::size_t i = 0; i < size; ++i)
            value[i] = static_cast<char>(std::to_integer<unsigned char>(source_[offset_++]));
        return value;
    }

    std::vector<std::byte> bytes() {
        const auto size = integer<std::uint64_t>();
        if (size > remaining() || size > kTrainingCheckpointMaximumBytes)
            throw std::length_error("checkpoint byte section exceeds limit");
        std::vector<std::byte> value(source_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                     source_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
        offset_ += static_cast<std::size_t>(size);
        return value;
    }

    template <typename T>
    std::vector<T> vector() {
        const auto count = integer<std::uint64_t>();
        const std::uint64_t maximum = std::is_same_v<T, float>
            ? kG3CheckpointParameterCount : kMaximumIndexCount;
        if (count > maximum || count > remaining() / sizeof(T))
            throw std::length_error("checkpoint vector count exceeds limit");
        std::vector<T> values;
        values.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            if constexpr (std::is_same_v<T, float>) values.push_back(real32());
            else values.push_back(integer<std::uint64_t>());
        }
        return values;
    }

    std::size_t remaining() const noexcept { return source_.size() - offset_; }
    bool done() const noexcept { return offset_ == source_.size(); }

private:
    void require(std::size_t count) {
        if (count > remaining()) throw std::out_of_range("truncated checkpoint payload");
    }
    std::span<const std::byte> source_;
    std::size_t offset_{0U};
};

void write_payload(Writer& w, const G3TrainingCheckpoint& c) {
    w.string(c.compatibility.code_version);
    w.string(c.compatibility.dataset_fingerprint);
    w.string(c.compatibility.optimizer_id);
    w.string(c.compatibility.configuration_fingerprint);
    w.string(c.run_id);
    w.string(c.configuration_snapshot);
    w.vector(c.model_weights);
    w.vector(c.optimizer_first_moment);
    w.vector(c.optimizer_second_moment);
    w.integer<std::uint64_t>(c.optimizer_step);
    w.integer<std::uint64_t>(c.scheduler_step);
    w.real(c.scheduler_learning_rate);
    w.integer<std::uint64_t>(c.completed_epochs);
    w.integer<std::uint64_t>(c.next_batch);
    w.integer<std::uint64_t>(c.sampler_position);
    w.vector(c.sampler_order);
    w.vector(c.train_split);
    w.vector(c.validation_split);
    w.vector(c.test_split);
    w.string(c.cpp_rng_state);
    w.string(c.python_rng_state);
    w.string(c.numpy_rng_state);
    w.string(c.backend_rng_state);
    w.integer<std::uint64_t>(c.rl_category_baselines.size());
    for (const auto& [category, baseline] : c.rl_category_baselines) {
        w.string(category);
        w.real(baseline);
    }
    w.string(c.rl_rng_state);
    w.vector(c.rl_optimizer_first_moment);
    w.vector(c.rl_optimizer_second_moment);
    w.integer<std::uint64_t>(c.rl_optimizer_step);
    w.real(c.kl_beta);
    w.bytes(c.frozen_sft_reference);
    w.bytes(c.replay_buffer_state);
    w.bytes(c.training_history);
}

G3TrainingCheckpoint read_payload(std::span<const std::byte> payload,
                                  std::uint16_t generation,
                                  std::uint16_t architecture) {
    Reader r(payload);
    G3TrainingCheckpoint c;
    c.compatibility.generation_id = generation;
    c.compatibility.architecture_id = architecture;
    c.compatibility.code_version = r.string();
    c.compatibility.dataset_fingerprint = r.string();
    c.compatibility.optimizer_id = r.string();
    c.compatibility.configuration_fingerprint = r.string();
    c.run_id = r.string();
    c.configuration_snapshot = r.string();
    c.model_weights = r.vector<float>();
    c.optimizer_first_moment = r.vector<float>();
    c.optimizer_second_moment = r.vector<float>();
    c.optimizer_step = r.integer<std::uint64_t>();
    c.scheduler_step = r.integer<std::uint64_t>();
    c.scheduler_learning_rate = r.real64();
    c.completed_epochs = r.integer<std::uint64_t>();
    c.next_batch = r.integer<std::uint64_t>();
    c.sampler_position = r.integer<std::uint64_t>();
    c.sampler_order = r.vector<std::uint64_t>();
    c.train_split = r.vector<std::uint64_t>();
    c.validation_split = r.vector<std::uint64_t>();
    c.test_split = r.vector<std::uint64_t>();
    c.cpp_rng_state = r.string();
    c.python_rng_state = r.string();
    c.numpy_rng_state = r.string();
    c.backend_rng_state = r.string();
    const auto baseline_count = r.integer<std::uint64_t>();
    if (baseline_count > 100000U) throw std::length_error("too many RL baseline categories");
    for (std::uint64_t i = 0; i < baseline_count; ++i) {
        auto key = r.string();
        const auto value = r.real64();
        if (!c.rl_category_baselines.emplace(std::move(key), value).second)
            throw std::invalid_argument("duplicate RL baseline category");
    }
    c.rl_rng_state = r.string();
    c.rl_optimizer_first_moment = r.vector<float>();
    c.rl_optimizer_second_moment = r.vector<float>();
    c.rl_optimizer_step = r.integer<std::uint64_t>();
    c.kl_beta = r.real64();
    c.frozen_sft_reference = r.bytes();
    c.replay_buffer_state = r.bytes();
    c.training_history = r.bytes();
    if (!r.done()) throw std::invalid_argument("checkpoint contains trailing payload bytes");
    return c;
}

bool finite_vector(const std::vector<float>& values) {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

bool valid_optional_optimizer(const std::vector<float>& first,
                              const std::vector<float>& second) {
    return (first.empty() && second.empty()) ||
           (first.size() == kG3CheckpointParameterCount &&
            second.size() == kG3CheckpointParameterCount &&
            finite_vector(first) && finite_vector(second) &&
            std::all_of(second.begin(), second.end(),
                        [](float value) { return value >= 0.0F; }));
}

std::uint32_t crc32_impl(std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto value = i >= kChecksumOffset && i < kChecksumOffset + 4U
            ? 0U : std::to_integer<std::uint8_t>(bytes[i]);
        crc ^= value;
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

void put_u16(std::span<std::byte> out, std::size_t offset, std::uint16_t value) {
    out[offset] = static_cast<std::byte>(value & 0xffU);
    out[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}
void put_u32(std::span<std::byte> out, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4U; ++i)
        out[offset + i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
}
void put_u64(std::span<std::byte> out, std::size_t offset, std::uint64_t value) {
    for (unsigned i = 0; i < 8U; ++i)
        out[offset + i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
}
std::uint16_t get_u16(std::span<const std::byte> in, std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(in[offset]) |
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(in[offset + 1U])) << 8U));
}
std::uint32_t get_u32(std::span<const std::byte> in, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4U; ++i)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(in[offset + i])) << (i * 8U);
    return value;
}
std::uint64_t get_u64(std::span<const std::byte> in, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8U; ++i)
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset + i])) << (i * 8U);
    return value;
}

Status durable_flush(const std::filesystem::path& path) {
#if defined(_WIN32)
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return fail(ErrorCode::internal_error, "save.flush", "Could not open checkpoint for durable flush");
    const BOOL ok = FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) return fail(ErrorCode::internal_error, "save.flush", "Could not flush checkpoint to storage");
#else
    const int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) return fail(ErrorCode::internal_error, "save.flush", "Could not open checkpoint for fsync");
    const int result = ::fsync(fd);
    ::close(fd);
    if (result != 0) return fail(ErrorCode::internal_error, "save.flush", "Could not fsync checkpoint");
#endif
    return Status::success();
}

Status atomic_replace(const std::filesystem::path& temporary,
                      const std::filesystem::path& destination) {
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return fail(ErrorCode::internal_error, "save.rename", "Atomic checkpoint replacement failed");
#else
    std::error_code ec;
    std::filesystem::rename(temporary, destination, ec);
    if (ec) return fail(ErrorCode::internal_error, "save.rename", "Atomic checkpoint rename failed");
    const auto parent = destination.parent_path().empty()
        ? std::filesystem::current_path() : destination.parent_path();
    const int dir = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir >= 0) { (void)::fsync(dir); ::close(dir); }
#endif
    return Status::success();
}

} // namespace

std::uint32_t training_checkpoint_crc32(std::span<const std::byte> bytes) noexcept {
    return crc32_impl(bytes);
}

Status validate_training_checkpoint(const G3TrainingCheckpoint& c,
                                    const CheckpointCompatibility* expected) {
    if (c.compatibility.generation_id != 3U ||
        c.compatibility.architecture_id != kG3CheckpointArchitectureId)
        return fail(ErrorCode::model_version_mismatch, "validate.architecture",
                    "Checkpoint generation or architecture is not supported by G3");
    if (c.compatibility.code_version.empty() || c.compatibility.optimizer_id.empty() ||
        c.compatibility.dataset_fingerprint.empty() ||
        c.compatibility.configuration_fingerprint.empty() || c.run_id.empty())
        return fail(ErrorCode::invalid_argument, "validate.metadata",
                    "Checkpoint compatibility metadata and run ID are required");
    if (c.model_weights.size() != kG3CheckpointParameterCount ||
        !finite_vector(c.model_weights))
        return fail(ErrorCode::invalid_argument, "validate.weights",
                    "Checkpoint must contain exactly 100000 finite FP32 model weights");
    if (!valid_optional_optimizer(c.optimizer_first_moment, c.optimizer_second_moment) ||
        !valid_optional_optimizer(c.rl_optimizer_first_moment, c.rl_optimizer_second_moment) ||
        ((c.optimizer_step == 0U) != c.optimizer_first_moment.empty()) ||
        ((c.rl_optimizer_step == 0U) != c.rl_optimizer_first_moment.empty()))
        return fail(ErrorCode::invalid_argument, "validate.optimizer",
                    "Optimizer moments must match the step counters and contain 100000 finite FP32 values when initialized");
    if (!std::isfinite(c.scheduler_learning_rate) || c.scheduler_learning_rate <= 0.0 ||
        !std::isfinite(c.kl_beta) || c.kl_beta < 0.0 ||
        (c.kl_beta > 0.0 &&
         (c.rl_optimizer_step > 0U || !c.rl_rng_state.empty() ||
          !c.rl_category_baselines.empty() || !c.rl_optimizer_first_moment.empty()) &&
         c.frozen_sft_reference.empty()))
        return fail(ErrorCode::invalid_argument, "validate.scheduler",
                    "Scheduler learning rate and KL beta must be finite; active KL requires a frozen reference");
    if ((c.rl_optimizer_step > 0U &&
         (c.rl_rng_state.empty() || c.rl_category_baselines.empty())) ||
        (c.rl_optimizer_step == 0U && !c.rl_category_baselines.empty()))
        return fail(ErrorCode::invalid_argument, "validate.rl_state",
                    "RL optimizer steps, RNG state and category baselines are inconsistent");
    for (const auto& [category, baseline] : c.rl_category_baselines) {
        if (category.empty() || !std::isfinite(baseline) || baseline < -1.0 || baseline > 1.0)
            return fail(ErrorCode::invalid_argument, "validate.rl_baseline",
                        "RL category baselines must have names and finite scores in [-1,1]");
    }
    if (c.sampler_position > c.sampler_order.size() ||
        c.training_history.size() > kTrainingCheckpointMaximumBytes ||
        c.frozen_sft_reference.size() > kTrainingCheckpointMaximumBytes ||
        c.replay_buffer_state.size() > kTrainingCheckpointMaximumBytes)
        return fail(ErrorCode::resource_limit, "validate.progress",
                    "Checkpoint progress or opaque state exceeds supported limits");

    std::unordered_set<std::uint64_t> split_indices;
    for (const auto* split : {&c.train_split, &c.validation_split, &c.test_split}) {
        for (const auto index : *split) {
            if (!split_indices.insert(index).second)
                return fail(ErrorCode::invalid_argument, "validate.splits",
                            "Training, validation and test splits must be disjoint and unique");
        }
    }
    if (!c.sampler_order.empty()) {
        if (c.sampler_order.size() != c.train_split.size())
            return fail(ErrorCode::invalid_argument, "validate.sampler",
                        "Sampler order must contain every training position exactly once");
        std::unordered_set<std::uint64_t> sampler_positions;
        for (const auto position : c.sampler_order) {
            if (position >= c.train_split.size() ||
                !sampler_positions.insert(position).second)
                return fail(ErrorCode::invalid_argument, "validate.sampler",
                            "Sampler order must be a permutation of training positions");
        }
    }
    if (expected != nullptr) {
        if (expected->generation_id != c.compatibility.generation_id ||
            expected->architecture_id != c.compatibility.architecture_id ||
            expected->code_version != c.compatibility.code_version ||
            expected->dataset_fingerprint != c.compatibility.dataset_fingerprint ||
            expected->optimizer_id != c.compatibility.optimizer_id ||
            expected->configuration_fingerprint != c.compatibility.configuration_fingerprint)
            return fail(ErrorCode::model_version_mismatch, "validate.compatibility",
                        "Checkpoint is incompatible with the requested architecture, code, dataset, optimizer or configuration");
    }
    return Status::success();
}

Result<std::vector<std::byte>> serialize_training_checkpoint(
    const G3TrainingCheckpoint& checkpoint) {
    const auto valid = validate_training_checkpoint(checkpoint);
    if (!valid) return Result<std::vector<std::byte>>::failure(*valid.error_if());
    try {
        Writer payload;
        write_payload(payload, checkpoint);
        if (payload.data.size() > kTrainingCheckpointMaximumBytes -
                                  kTrainingCheckpointHeaderBytes)
            return fail_result<std::vector<std::byte>>(ErrorCode::resource_limit,
                "serialize.size", "Checkpoint exceeds the maximum supported file size");
        std::vector<std::byte> output(kTrainingCheckpointHeaderBytes, std::byte{0});
        std::copy(kMagic.begin(), kMagic.end(), output.begin());
        auto header = std::span<std::byte>(output).first(kTrainingCheckpointHeaderBytes);
        put_u16(header, 8U, kTrainingCheckpointFormatVersion);
        put_u16(header, 10U, static_cast<std::uint16_t>(kTrainingCheckpointHeaderBytes));
        put_u16(header, 12U, checkpoint.compatibility.generation_id);
        put_u16(header, 14U, checkpoint.compatibility.architecture_id);
        put_u32(header, 16U, 1U); // FP32 master weights; extensible flags.
        put_u64(header, 20U, static_cast<std::uint64_t>(payload.data.size()));
        output.insert(output.end(), payload.data.begin(), payload.data.end());
        // insert() may reallocate the vector; reacquire the header span before
        // writing the checksum instead of using the invalidated pre-insert view.
        auto final_header =
            std::span<std::byte>(output).first(kTrainingCheckpointHeaderBytes);
        put_u32(final_header, kChecksumOffset, crc32_impl(output));
        return Result<std::vector<std::byte>>::success(std::move(output));
    } catch (const std::exception&) {
        return fail_result<std::vector<std::byte>>(ErrorCode::resource_limit,
            "serialize.payload", "Checkpoint payload could not be serialized safely");
    }
}

Result<G3TrainingCheckpoint> deserialize_training_checkpoint(
    std::span<const std::byte> bytes, const CheckpointCompatibility* expected) {
    if (bytes.size() < kTrainingCheckpointHeaderBytes ||
        bytes.size() > kTrainingCheckpointMaximumBytes ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
        return fail_result<G3TrainingCheckpoint>(ErrorCode::checkpoint_corrupt,
            "load.magic", "Checkpoint is truncated, oversized or has an invalid magic value");
    const auto header = bytes.first(kTrainingCheckpointHeaderBytes);
    if (get_u16(header, 8U) != kTrainingCheckpointFormatVersion ||
        get_u16(header, 10U) != kTrainingCheckpointHeaderBytes)
        return fail_result<G3TrainingCheckpoint>(ErrorCode::model_version_mismatch,
            "load.version", "Checkpoint format version or header size is unsupported");
    if (get_u32(header, 16U) != 1U)
        return fail_result<G3TrainingCheckpoint>(ErrorCode::model_version_mismatch,
            "load.flags", "Checkpoint precision or feature flags are unsupported");
    for (std::size_t i = 32U; i < kTrainingCheckpointHeaderBytes; ++i)
        if (header[i] != std::byte{0})
            return fail_result<G3TrainingCheckpoint>(ErrorCode::checkpoint_corrupt,
                "load.reserved", "Reserved checkpoint header bytes must be zero");
    const auto payload_size = get_u64(header, 20U);
    if (payload_size != bytes.size() - kTrainingCheckpointHeaderBytes)
        return fail_result<G3TrainingCheckpoint>(ErrorCode::checkpoint_corrupt,
            "load.length", "Checkpoint payload length does not match the file size");
    if (crc32_impl(bytes) != get_u32(header, kChecksumOffset))
        return fail_result<G3TrainingCheckpoint>(ErrorCode::checkpoint_corrupt,
            "load.checksum", "Checkpoint checksum validation failed");
    try {
        auto checkpoint = read_payload(bytes.subspan(kTrainingCheckpointHeaderBytes),
                                       get_u16(header, 12U), get_u16(header, 14U));
        const auto valid = validate_training_checkpoint(checkpoint, expected);
        if (!valid) return Result<G3TrainingCheckpoint>::failure(*valid.error_if());
        return Result<G3TrainingCheckpoint>::success(std::move(checkpoint));
    } catch (const std::exception&) {
        return fail_result<G3TrainingCheckpoint>(ErrorCode::checkpoint_corrupt,
            "load.payload", "Checkpoint payload is malformed or truncated");
    }
}

Status save_training_checkpoint_atomic(const std::filesystem::path& destination,
                                       const G3TrainingCheckpoint& checkpoint) {
    if (destination.empty())
        return fail(ErrorCode::invalid_argument, "save.path", "Checkpoint destination is empty");
    const auto serialized = serialize_training_checkpoint(checkpoint);
    if (!serialized) return Status::failure(*serialized.error_if());
    std::error_code ec;
    if (!destination.parent_path().empty())
        std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) return fail(ErrorCode::internal_error, "save.directory",
                        "Could not create checkpoint destination directory");
    auto temporary = destination;
    temporary += ".tmp." + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return fail(ErrorCode::internal_error, "save.open",
                                 "Could not create temporary checkpoint");
        output.write(reinterpret_cast<const char*>(serialized.value_if()->data()),
                     static_cast<std::streamsize>(serialized.value_if()->size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, ec);
            return fail(ErrorCode::internal_error, "save.write",
                        "Could not write complete temporary checkpoint");
        }
    }
    const auto flushed = durable_flush(temporary);
    if (!flushed) { std::filesystem::remove(temporary, ec); return flushed; }
    const auto verified = read_training_checkpoint(temporary);
    if (!verified) {
        std::filesystem::remove(temporary, ec);
        return Status::failure(*verified.error_if());
    }
    const auto replaced = atomic_replace(temporary, destination);
    if (!replaced) { std::filesystem::remove(temporary, ec); return replaced; }
    return Status::success();
}

Result<G3TrainingCheckpoint> read_training_checkpoint(
    const std::filesystem::path& source, const CheckpointCompatibility* expected) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(source, ec);
    if (ec) return fail_result<G3TrainingCheckpoint>(ErrorCode::model_not_loaded,
        "load.open", "Could not inspect checkpoint file");
    if (size < kTrainingCheckpointHeaderBytes || size > kTrainingCheckpointMaximumBytes)
        return fail_result<G3TrainingCheckpoint>(ErrorCode::resource_limit,
            "load.size", "Checkpoint file size is outside supported limits");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::ifstream input(source, std::ios::binary);
    if (!input) return fail_result<G3TrainingCheckpoint>(ErrorCode::model_not_loaded,
        "load.open", "Could not open checkpoint file");
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input || static_cast<std::size_t>(input.gcount()) != bytes.size())
        return fail_result<G3TrainingCheckpoint>(ErrorCode::checkpoint_corrupt,
            "load.read", "Checkpoint file is truncated or could not be read completely");
    return deserialize_training_checkpoint(bytes, expected);
}


std::vector<float> capture_g3_model_weights(const neural::G3Backend& backend) {
    const auto& w = backend.weights();
    std::vector<float> result;
    result.reserve(static_cast<std::size_t>(backend.parameter_count()));
    const std::array<const std::vector<float>*, 11> tensors{
        &w.embedding, &w.layer1_input, &w.layer1_recurrent, &w.layer1_bias,
        &w.layer2_input, &w.layer2_recurrent, &w.layer2_bias, &w.rms_scale,
        &w.projection, &w.projection_bias, &w.output_bias};
    for (const auto* tensor : tensors)
        result.insert(result.end(), tensor->begin(), tensor->end());
    return result;
}

Status restore_g3_model_weights(std::span<const float> weights,
                                neural::G3Backend& backend) {
    if (weights.size() != kG3CheckpointParameterCount ||
        backend.parameter_count() != kG3CheckpointParameterCount ||
        !std::all_of(weights.begin(), weights.end(),
                     [](float value) { return std::isfinite(value); }))
        return fail(ErrorCode::model_version_mismatch, "restore.weights",
                    "Model weights do not match the finite 100000-parameter G3 architecture");
    auto view = backend.mutable_weights();
    const std::array<std::span<float>, 11> tensors{
        view.embedding, view.layer1_input, view.layer1_recurrent, view.layer1_bias,
        view.layer2_input, view.layer2_recurrent, view.layer2_bias, view.rms_scale,
        view.projection, view.projection_bias, view.output_bias};
    std::size_t offset = 0U;
    for (auto tensor : tensors) {
        if (offset + tensor.size() > weights.size())
            return fail(ErrorCode::model_version_mismatch, "restore.layout",
                        "G3 tensor layout exceeds the checkpoint weight payload");
        std::copy_n(weights.begin() + static_cast<std::ptrdiff_t>(offset),
                    tensor.size(), tensor.begin());
        offset += tensor.size();
    }
    if (offset != weights.size())
        return fail(ErrorCode::model_version_mismatch, "restore.layout",
                    "G3 tensor layout does not consume the complete checkpoint weight payload");
    return Status::success();
}

} // namespace morok::g3::training
