#pragma once

#include "morok/common/ids.hpp"
#include "morok/common/result.hpp"
#include "morok/common/timestamp.hpp"
#include "morok/common/utf8_text.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace morok::memory {

enum class MemoryKind : std::uint8_t { working, episodic, semantic };
enum class MemorySource : std::uint8_t { user, tool, document, inference, system };
enum class Sensitivity : std::uint8_t { normal, private_data, sensitive };

struct MemoryLimits final {
    std::size_t max_record_bytes{64U * 1024U};
    std::size_t max_results{20};
    std::size_t max_context_bytes{4U * 1024U};
    std::size_t max_database_bytes{256U * 1024U * 1024U};
    std::uint32_t busy_timeout_ms{100};
    std::uint32_t search_timeout_ms{100};
};

struct MemoryInput final {
    common::SessionId session_id;
    MemoryKind kind{MemoryKind::episodic};
    common::Utf8Text title;
    common::Utf8Text body;
    MemorySource source{MemorySource::user};
    std::string source_id;
    Sensitivity sensitivity{Sensitivity::normal};
    std::optional<common::Timestamp> expires_at;
};

struct MemoryRecord final {
    common::MemoryId id;
    common::SessionId session_id;
    MemoryKind kind{MemoryKind::episodic};
    common::Utf8Text title;
    common::Utf8Text body;
    MemorySource source{MemorySource::user};
    std::string source_id;
    Sensitivity sensitivity{Sensitivity::normal};
    common::Timestamp created_at;
    std::optional<common::Timestamp> expires_at;
};

struct SearchQuery final {
    common::SessionId session_id;
    common::Utf8Text text;
    std::size_t limit{20};
    std::size_t max_context_bytes{4U * 1024U};
    std::optional<MemoryKind> kind;
};

class MemoryStore final {
public:
    MemoryStore();
    ~MemoryStore();

    MemoryStore(const MemoryStore&) = delete;
    MemoryStore& operator=(const MemoryStore&) = delete;
    MemoryStore(MemoryStore&&) noexcept;
    MemoryStore& operator=(MemoryStore&&) noexcept;

    [[nodiscard]] common::Status open(
        const std::string& database_path,
        MemoryLimits limits = {});
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] common::Status close() noexcept;

    [[nodiscard]] common::Result<common::MemoryId> store(
        const MemoryInput& input,
        const std::optional<common::RequestId>& request_id = std::nullopt);

    [[nodiscard]] common::Result<MemoryRecord> retrieve(
        const common::SessionId& session_id,
        const common::MemoryId& memory_id);

    [[nodiscard]] common::Result<std::vector<MemoryRecord>> search(
        const SearchQuery& query);

    [[nodiscard]] common::Status promote(
        const common::SessionId& session_id,
        const common::MemoryId& memory_id,
        MemoryKind target_kind = MemoryKind::episodic);

    [[nodiscard]] common::Status update_metadata(
        const common::SessionId& session_id,
        const common::MemoryId& memory_id,
        Sensitivity sensitivity,
        std::optional<common::Timestamp> expires_at);

    [[nodiscard]] common::Status erase(
        const common::SessionId& session_id,
        const common::MemoryId& memory_id,
        std::string actor,
        const std::optional<common::RequestId>& request_id = std::nullopt);

    [[nodiscard]] common::Result<std::size_t> clear_working(
        const common::SessionId& session_id);

    [[nodiscard]] common::Result<std::size_t> prune_expired(
        common::Timestamp now = common::Timestamp::now());

    [[nodiscard]] common::Status backup_to(const std::string& destination_path);
    [[nodiscard]] common::Status integrity_check();
    [[nodiscard]] bool fts5_available() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace morok::memory
