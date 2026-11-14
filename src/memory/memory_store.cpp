#include "morok/memory/memory_store.hpp"

#include "morok/common/error.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace morok::memory {
namespace {

using common::Error;
using common::ErrorCode;
using common::Result;
using common::Status;

[[nodiscard]] Error db_error(sqlite3* db, std::string stage) {
    return Error::make(ErrorCode::internal_error,
        db != nullptr ? sqlite3_errmsg(db) : "SQLite operation failed",
        "memory", std::move(stage));
}

class Statement final {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr) != SQLITE_OK) {
            statement_ = nullptr;
        }
    }
    ~Statement() { if (statement_) sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    [[nodiscard]] sqlite3_stmt* get() const noexcept { return statement_; }
    [[nodiscard]] explicit operator bool() const noexcept { return statement_ != nullptr; }
private:
    sqlite3* db_{};
    sqlite3_stmt* statement_{};
};

void bind_text(sqlite3_stmt* stmt, int index, std::string_view value) {
    sqlite3_bind_text(stmt, index, value.data(),
        static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

[[nodiscard]] std::string column_text(sqlite3_stmt* stmt, int column) {
    const auto* value = sqlite3_column_text(stmt, column);
    const auto bytes = sqlite3_column_bytes(stmt, column);
    if (value == nullptr || bytes <= 0) return {};
    return std::string(reinterpret_cast<const char*>(value),
                       static_cast<std::size_t>(bytes));
}

[[nodiscard]] int kind_value(MemoryKind kind) {
    return static_cast<int>(kind);
}
[[nodiscard]] int source_value(MemorySource source) {
    return static_cast<int>(source);
}
[[nodiscard]] int sensitivity_value(Sensitivity sensitivity) {
    return static_cast<int>(sensitivity);
}

[[nodiscard]] std::string literal_fts_query(std::string_view input) {
    std::string query;
    std::string token;
    const auto flush = [&]() mutable {
        if (token.empty()) return;
        if (!query.empty()) query += " AND ";
        query += '"';
        for (const char ch : token) {
            if (ch == '"') query += """";
            else query += ch;
        }
        query += '"';
        token.clear();
    };
    for (const unsigned char ch : input) {
        if (std::isspace(ch) != 0) flush();
        else token += static_cast<char>(ch);
    }
    flush();
    return query;
}

[[nodiscard]] Result<MemoryRecord> read_record(sqlite3_stmt* stmt) {
    auto id = common::Utf8Text::from_utf8(column_text(stmt, 0));
    auto session = common::Utf8Text::from_utf8(column_text(stmt, 1));
    auto title = common::Utf8Text::from_utf8(column_text(stmt, 4));
    auto body = common::Utf8Text::from_utf8(column_text(stmt, 5));
    auto source_id = common::Utf8Text::from_utf8(column_text(stmt, 7));
    if (!id || !session || !title || !body || !source_id) {
        return Result<MemoryRecord>::failure(Error::make(
            ErrorCode::internal_error, "database contains invalid UTF-8",
            "memory", "decode_record"));
    }
    // Strong IDs are created at write time; records are addressed externally by their UUID strings.
    auto parsed_id = common::MemoryId::generate();
    auto parsed_session = common::SessionId::generate();
    if (!parsed_id || !parsed_session) {
        return Result<MemoryRecord>::failure(Error::make(
            ErrorCode::internal_error, "unable to create typed memory result",
            "memory", "decode_record"));
    }
    const auto created = common::Timestamp::from_unix_nanoseconds(
        sqlite3_column_int64(stmt, 9));
    std::optional<common::Timestamp> expires;
    if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
        expires = common::Timestamp::from_unix_nanoseconds(
            sqlite3_column_int64(stmt, 10));
    }
    // UUID value constructors are intentionally private; return the canonical IDs via
    // dedicated string fields in the public record in a future schema revision.
    return Result<MemoryRecord>::failure(Error::make(
        ErrorCode::internal_error,
        "typed identifier decoding is unavailable for persisted UUID values",
        "memory", "decode_record"));
}

constexpr const char* record_columns =
    "m.id,m.session_id,m.kind,m.source,m.title,m.body,m.source_id,"
    "m.sensitivity,m.created_at,m.expires_at";

} // namespace

struct MemoryStore::Impl {
    sqlite3* db{};
    MemoryLimits limits{};
    bool fts{false};
};

MemoryStore::MemoryStore() : impl_(std::make_unique<Impl>()) {}
MemoryStore::~MemoryStore() { (void)close(); }
MemoryStore::MemoryStore(MemoryStore&&) noexcept = default;
MemoryStore& MemoryStore::operator=(MemoryStore&&) noexcept = default;

Status MemoryStore::open(const std::string& database_path, MemoryLimits limits) {
    if (database_path.empty() || limits.max_record_bytes == 0 ||
        limits.max_results == 0 || limits.max_context_bytes == 0 ||
        limits.busy_timeout_ms == 0) {
        return Status::failure(Error::make(ErrorCode::invalid_argument,
            "invalid database path or memory limits", "memory", "open"));
    }
    if (impl_->db != nullptr) return Status::failure(Error::make(
        ErrorCode::invalid_argument, "memory store is already open", "memory", "open"));

    sqlite3* db = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(database_path.c_str(), &db, flags, nullptr) != SQLITE_OK) {
        const auto error = db_error(db, "open");
        if (db) sqlite3_close(db);
        return Status::failure(error);
    }
    impl_->db = db;
    impl_->limits = limits;
    sqlite3_busy_timeout(db, static_cast<int>(limits.busy_timeout_ms));

    const char* schema =
        "PRAGMA foreign_keys=ON;"
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=FULL;"
        "CREATE TABLE IF NOT EXISTS schema_migrations("
        " version INTEGER PRIMARY KEY, applied_at INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS memories("
        " id TEXT PRIMARY KEY, session_id TEXT NOT NULL, kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 2),"
        " source INTEGER NOT NULL CHECK(source BETWEEN 0 AND 4), title TEXT NOT NULL, body TEXT NOT NULL,"
        " source_id TEXT NOT NULL, sensitivity INTEGER NOT NULL CHECK(sensitivity BETWEEN 0 AND 2),"
        " created_at INTEGER NOT NULL, expires_at INTEGER);"
        "CREATE INDEX IF NOT EXISTS idx_memories_session_kind ON memories(session_id,kind);"
        "CREATE INDEX IF NOT EXISTS idx_memories_expiry ON memories(expires_at);"
        "CREATE TABLE IF NOT EXISTS memory_audit("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, memory_id TEXT NOT NULL, session_id TEXT NOT NULL,"
        " request_id TEXT, actor TEXT NOT NULL, action TEXT NOT NULL, occurred_at INTEGER NOT NULL,"
        " outcome TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_memory_audit_session ON memory_audit(session_id,occurred_at);"
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at) VALUES(1,unixepoch());";
    char* message = nullptr;
    if (sqlite3_exec(db, schema, nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string detail = message ? message : sqlite3_errmsg(db);
        sqlite3_free(message);
        (void)close();
        return Status::failure(Error::make(ErrorCode::internal_error, detail,
            "memory", "schema_migration"));
    }

    const char* fts_schema =
        "CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5("
        "title,body,content='memories',content_rowid='rowid');"
        "CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN "
        "INSERT INTO memories_fts(rowid,title,body) VALUES(new.rowid,new.title,new.body); END;"
        "CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN "
        "INSERT INTO memories_fts(memories_fts,rowid,title,body) VALUES('delete',old.rowid,old.title,old.body); END;"
        "CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN "
        "INSERT INTO memories_fts(memories_fts,rowid,title,body) VALUES('delete',old.rowid,old.title,old.body);"
        "INSERT INTO memories_fts(rowid,title,body) VALUES(new.rowid,new.title,new.body); END;";
    if (sqlite3_exec(db, fts_schema, nullptr, nullptr, nullptr) == SQLITE_OK) {
        impl_->fts = true;
        (void)sqlite3_exec(db,
            "INSERT INTO memories_fts(memories_fts) VALUES('rebuild');",
            nullptr, nullptr, nullptr);
    } else {
        impl_->fts = false;
    }
    return Status::success();
}

bool MemoryStore::is_open() const noexcept { return impl_ && impl_->db != nullptr; }

Status MemoryStore::close() noexcept {
    if (!impl_ || impl_->db == nullptr) return Status::success();
    const int rc = sqlite3_close(impl_->db);
    if (rc != SQLITE_OK) return Status::failure(db_error(impl_->db, "close"));
    impl_->db = nullptr;
    impl_->fts = false;
    return Status::success();
}

bool MemoryStore::fts5_available() const noexcept { return impl_ && impl_->fts; }

Result<common::MemoryId> MemoryStore::store(
    const MemoryInput& input,
    const std::optional<common::RequestId>& request_id) {
    if (!is_open()) return Result<common::MemoryId>::failure(Error::make(
        ErrorCode::internal_error, "memory store is not open", "memory", "store"));
    if (input.title.size_bytes() + input.body.size_bytes() > impl_->limits.max_record_bytes ||
        input.body.empty() || input.session_id.is_nil()) {
        return Result<common::MemoryId>::failure(Error::make(
            ErrorCode::input_too_large, "memory record is empty or exceeds size limit",
            "memory", "store"));
    }
    auto id = common::MemoryId::generate();
    if (!id) return id;
    Statement statement(impl_->db,
        "INSERT INTO memories(id,session_id,kind,source,title,body,source_id,sensitivity,created_at,expires_at)"
        " VALUES(?,?,?,?,?,?,?,?,?,?)");
    if (!statement) return Result<common::MemoryId>::failure(db_error(impl_->db, "prepare_store"));
    const auto memory_id = id.value_if()->to_string();
    const auto session_id = input.session_id.to_string();
    bind_text(statement.get(), 1, memory_id);
    bind_text(statement.get(), 2, session_id);
    sqlite3_bind_int(statement.get(), 3, kind_value(input.kind));
    sqlite3_bind_int(statement.get(), 4, source_value(input.source));
    bind_text(statement.get(), 5, input.title.view());
    bind_text(statement.get(), 6, input.body.view());
    bind_text(statement.get(), 7, input.source_id);
    sqlite3_bind_int(statement.get(), 8, sensitivity_value(input.sensitivity));
    sqlite3_bind_int64(statement.get(), 9, common::Timestamp::now().unix_nanoseconds());
    if (input.expires_at) sqlite3_bind_int64(statement.get(), 10, input.expires_at->unix_nanoseconds());
    else sqlite3_bind_null(statement.get(), 10);
    if (sqlite3_step(statement.get()) != SQLITE_DONE)
        return Result<common::MemoryId>::failure(db_error(impl_->db, "insert_memory"));

    Statement audit(impl_->db,
        "INSERT INTO memory_audit(memory_id,session_id,request_id,actor,action,occurred_at,outcome)"
        " VALUES(?,?,?,'system','store',?,'success')");
    if (audit) {
        bind_text(audit.get(), 1, memory_id);
        bind_text(audit.get(), 2, session_id);
        if (request_id) bind_text(audit.get(), 3, request_id->to_string());
        else sqlite3_bind_null(audit.get(), 3);
        sqlite3_bind_int64(audit.get(), 4, common::Timestamp::now().unix_nanoseconds());
        (void)sqlite3_step(audit.get());
    }
    return Result<common::MemoryId>::success(*id.value_if());
}

Result<MemoryRecord> MemoryStore::retrieve(
    const common::SessionId& session_id, const common::MemoryId& memory_id) {
    (void)session_id; (void)memory_id;
    return Result<MemoryRecord>::failure(Error::make(
        ErrorCode::internal_error, "typed UUID decoding is pending", "memory", "retrieve"));
}

Result<std::vector<MemoryRecord>> MemoryStore::search(const SearchQuery& query) {
    (void)query;
    return Result<std::vector<MemoryRecord>>::failure(Error::make(
        ErrorCode::internal_error, "typed UUID decoding is pending", "memory", "search"));
}

Status MemoryStore::erase(const common::SessionId& session_id,
    const common::MemoryId& memory_id, std::string actor,
    const std::optional<common::RequestId>& request_id) {
    (void)session_id; (void)memory_id; (void)actor; (void)request_id;
    return Status::failure(Error::make(ErrorCode::internal_error,
        "typed UUID decoding is pending", "memory", "erase"));
}

Result<std::size_t> MemoryStore::clear_working(const common::SessionId& session_id) {
    (void)session_id;
    return Result<std::size_t>::failure(Error::make(ErrorCode::internal_error,
        "working memory cleanup is pending", "memory", "clear_working"));
}

Result<std::size_t> MemoryStore::prune_expired(common::Timestamp now) {
    (void)now;
    return Result<std::size_t>::failure(Error::make(ErrorCode::internal_error,
        "retention pruning is pending", "memory", "prune_expired"));
}

Status MemoryStore::backup_to(const std::string& destination_path) {
    (void)destination_path;
    return Status::failure(Error::make(ErrorCode::internal_error,
        "backup is pending", "memory", "backup"));
}

Status MemoryStore::integrity_check() {
    if (!is_open()) return Status::failure(Error::make(ErrorCode::internal_error,
        "memory store is not open", "memory", "integrity"));
    Statement statement(impl_->db, "PRAGMA integrity_check");
    if (!statement || sqlite3_step(statement.get()) != SQLITE_ROW)
        return Status::failure(db_error(impl_->db, "integrity"));
    const auto result = column_text(statement.get(), 0);
    if (result != "ok") return Status::failure(Error::make(
        ErrorCode::internal_error, result, "memory", "integrity"));
    return Status::success();
}

} // namespace morok::memory
