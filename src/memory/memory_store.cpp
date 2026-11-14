#include "morok/memory/memory_store.hpp"

#include "morok/common/error.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr) != SQLITE_OK)
            statement_ = nullptr;
    }
    ~Statement() { if (statement_) sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    [[nodiscard]] sqlite3_stmt* get() const noexcept { return statement_; }
    [[nodiscard]] explicit operator bool() const noexcept { return statement_ != nullptr; }
private:
    sqlite3_stmt* statement_{};
};

void bind_text(sqlite3_stmt* stmt, int index, std::string_view value) {
    (void)sqlite3_bind_text(stmt, index, value.data(),
        static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

[[nodiscard]] std::string column_text(sqlite3_stmt* stmt, int column) {
    const auto* value = sqlite3_column_text(stmt, column);
    const int bytes = sqlite3_column_bytes(stmt, column);
    if (value == nullptr || bytes <= 0) return {};
    return std::string(reinterpret_cast<const char*>(value),
                       static_cast<std::size_t>(bytes));
}

[[nodiscard]] std::string literal_fts_query(std::string_view input) {
    std::string query;
    std::string token;
    auto flush = [&]() {
        if (token.empty()) return;
        if (!query.empty()) query += " AND ";
        query += '"';
        for (const char ch : token) {
            if (ch == '"') query += "\"\"";
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

[[nodiscard]] std::string literal_like_pattern(std::string_view input) {
    std::string pattern;
    pattern.reserve(input.size() + 2U);
    pattern.push_back('%');
    for (const char ch : input) {
        if (ch == '%' || ch == '_' || ch == '\\') pattern.push_back('\\');
        pattern.push_back(ch);
    }
    pattern.push_back('%');
    return pattern;
}

[[nodiscard]] Status exec(sqlite3* db, const char* sql, std::string stage) {
    char* message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) == SQLITE_OK)
        return Status::success();
    const std::string detail = message ? message : sqlite3_errmsg(db);
    sqlite3_free(message);
    return Status::failure(Error::make(
        ErrorCode::internal_error, detail, "memory", std::move(stage)));
}

[[nodiscard]] Status insert_audit(sqlite3* db, std::string_view memory_id,
    std::string_view session_id, std::string_view actor, std::string_view action,
    std::string_view outcome, const std::optional<common::RequestId>& request_id) {
    Statement statement(db,
        "INSERT INTO memory_audit(memory_id,session_id,request_id,actor,action,occurred_at,outcome)"
        " VALUES(?,?,?,?,?,?,?)");
    if (!statement) return Status::failure(db_error(db, "prepare_audit"));
    bind_text(statement.get(), 1, memory_id);
    bind_text(statement.get(), 2, session_id);
    if (request_id) bind_text(statement.get(), 3, request_id->to_string());
    else sqlite3_bind_null(statement.get(), 3);
    bind_text(statement.get(), 4, actor);
    bind_text(statement.get(), 5, action);
    sqlite3_bind_int64(statement.get(), 6,
        common::Timestamp::now().unix_nanoseconds());
    bind_text(statement.get(), 7, outcome);
    if (sqlite3_step(statement.get()) != SQLITE_DONE)
        return Status::failure(db_error(db, "insert_audit"));
    return Status::success();
}

[[nodiscard]] Result<MemoryRecord> read_record(sqlite3_stmt* stmt) {
    auto id = common::MemoryId::from_string(column_text(stmt, 0));
    auto session = common::SessionId::from_string(column_text(stmt, 1));
    auto title = common::Utf8Text::from_utf8(column_text(stmt, 4));
    auto body = common::Utf8Text::from_utf8(column_text(stmt, 5));
    if (!id || !session || !title || !body) {
        return Result<MemoryRecord>::failure(Error::make(
            ErrorCode::internal_error, "database contains invalid memory data",
            "memory", "decode_record"));
    }
    auto source_id = column_text(stmt, 6);
    const auto created = common::Timestamp::from_unix_nanoseconds(
        sqlite3_column_int64(stmt, 8));
    std::optional<common::Timestamp> expires;
    if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
        expires = common::Timestamp::from_unix_nanoseconds(
            sqlite3_column_int64(stmt, 9));

    MemoryRecord record{
        *id.value_if(), *session.value_if(),
        static_cast<MemoryKind>(sqlite3_column_int(stmt, 2)),
        std::move(*title.value_if()), std::move(*body.value_if()),
        static_cast<MemorySource>(sqlite3_column_int(stmt, 3)),
        std::move(source_id),
        static_cast<Sensitivity>(sqlite3_column_int(stmt, 7)),
        created, expires
    };
    return Result<MemoryRecord>::success(std::move(record));
}

struct SearchDeadline final {
    std::chrono::steady_clock::time_point expires;
    core::CancellationToken cancellation;
};

int interrupt_search(void* context) {
    const auto* deadline = static_cast<const SearchDeadline*>(context);
    return deadline->cancellation.is_cancelled() ||
        std::chrono::steady_clock::now() >= deadline->expires ? 1 : 0;
}

constexpr const char* record_columns =
    "m.id,m.session_id,m.kind,m.source,m.title,m.body,m.source_id,"
    "m.sensitivity,m.created_at,m.expires_at";

} // namespace

struct MemoryStore::Impl {
    sqlite3* db{};
    MemoryLimits limits{};
    bool fts{false};
    mutable std::recursive_mutex mutex;
    ~Impl() { if (db != nullptr) (void)sqlite3_close_v2(db); }
};

MemoryStore::MemoryStore() : impl_(std::make_unique<Impl>()) {}
MemoryStore::~MemoryStore() { (void)close(); }

Status MemoryStore::open(const std::string& database_path, MemoryLimits limits) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    std::lock_guard lock(impl_->mutex);
    if (database_path.empty() || limits.max_record_bytes == 0 ||
        limits.max_results == 0 || limits.max_context_bytes == 0 ||
        limits.max_database_bytes < 4096U || limits.busy_timeout_ms == 0 ||
        limits.search_timeout_ms == 0) {
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
    (void)sqlite3_busy_timeout(db, static_cast<int>(limits.busy_timeout_ms));

    const auto max_pages = static_cast<sqlite3_int64>(limits.max_database_bytes / 4096U);
    const std::string page_limit = "PRAGMA max_page_count=" + std::to_string(max_pages) + ";";
    const auto page_limit_result = exec(db, page_limit.c_str(), "page_limit");
    if (!page_limit_result) { const auto error = *page_limit_result.error_if(); (void)close(); return Status::failure(error); }

    const char* schema =
        "PRAGMA foreign_keys=ON;"
        "PRAGMA page_size=4096;"
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=FULL;"
        "BEGIN IMMEDIATE;"
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
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at) VALUES(1,CAST(strftime('%s','now') AS INTEGER));"
        "COMMIT;";
    const auto schema_result = exec(db, schema, "schema_migration");
    if (!schema_result) {
        const auto error = *schema_result.error_if();
        (void)exec(db, "ROLLBACK", "rollback_schema");
        (void)close();
        return Status::failure(error);
    }

    const char* fts_schema =
        "CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5(title,body);"
        "CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN "
        "INSERT INTO memories_fts(rowid,title,body) SELECT new.rowid,new.title,new.body WHERE new.sensitivity<2; END;"
        "CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN "
        "INSERT INTO memories_fts(memories_fts,rowid,title,body) SELECT 'delete',old.rowid,old.title,old.body WHERE old.sensitivity<2; END;"
        "CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN "
        "INSERT INTO memories_fts(memories_fts,rowid,title,body) SELECT 'delete',old.rowid,old.title,old.body WHERE old.sensitivity<2;"
        "INSERT INTO memories_fts(rowid,title,body) SELECT new.rowid,new.title,new.body WHERE new.sensitivity<2; END;";
    if (exec(db, fts_schema, "fts5_setup")) {
        impl_->fts = true;
        (void)exec(db,
            "INSERT INTO memories_fts(rowid,title,body) "
            "SELECT m.rowid,m.title,m.body FROM memories m "
            "WHERE m.sensitivity<2 AND m.rowid NOT IN (SELECT rowid FROM memories_fts);",
            "fts5_backfill");
     } else {
        impl_->fts = false;
    }
    return Status::success();
}

bool MemoryStore::is_open() const noexcept {
    if (!impl_) return false;
    std::lock_guard lock(impl_->mutex);
    return impl_->db != nullptr;
}

Status MemoryStore::close() noexcept {
    if (!impl_) return Status::success();
    std::lock_guard lock(impl_->mutex);
    if (!impl_ || impl_->db == nullptr) return Status::success();
    const int rc = sqlite3_close(impl_->db);
    if (rc != SQLITE_OK) return Status::failure(db_error(impl_->db, "close"));
    impl_->db = nullptr;
    impl_->fts = false;
    return Status::success();
}

bool MemoryStore::fts5_available() const noexcept {
    if (!impl_) return false;
    std::lock_guard lock(impl_->mutex);
    return impl_->fts;
}

Result<common::MemoryId> MemoryStore::store(
    const MemoryInput& input,
    const std::optional<common::RequestId>& request_id) {
    std::lock_guard lock(impl_->mutex);
    if (!is_open()) return Result<common::MemoryId>::failure(Error::make(
        ErrorCode::internal_error, "memory store is not open", "memory", "store"));
    const auto total_bytes = input.title.size_bytes() + input.body.size_bytes() +
        input.source_id.size();
    if (total_bytes > impl_->limits.max_record_bytes || input.body.empty() ||
        input.session_id.is_nil() || input.source_id.empty() ||
        static_cast<int>(input.kind) > 2 || static_cast<int>(input.source) > 4 ||
        static_cast<int>(input.sensitivity) > 2) {
        return Result<common::MemoryId>::failure(Error::make(
            total_bytes > impl_->limits.max_record_bytes ? ErrorCode::input_too_large
                                                         : ErrorCode::invalid_argument,
            "memory record is invalid or exceeds the configured size limit",
            "memory", "store"));
    }
    auto id = common::MemoryId::generate();
    if (!id) return id;
    const auto memory_id = id.value_if()->to_string();
    const auto session_id = input.session_id.to_string();
    auto begin = exec(impl_->db, "BEGIN IMMEDIATE", "begin_store");
    if (!begin) return Result<common::MemoryId>::failure(*begin.error_if());

    Statement statement(impl_->db,
        "INSERT INTO memories(id,session_id,kind,source,title,body,source_id,sensitivity,created_at,expires_at)"
        " VALUES(?,?,?,?,?,?,?,?,?,?)");
    if (!statement) {
        (void)exec(impl_->db, "ROLLBACK", "rollback_store");
        return Result<common::MemoryId>::failure(db_error(impl_->db, "prepare_store"));
    }
    bind_text(statement.get(), 1, memory_id);
    bind_text(statement.get(), 2, session_id);
    sqlite3_bind_int(statement.get(), 3, static_cast<int>(input.kind));
    sqlite3_bind_int(statement.get(), 4, static_cast<int>(input.source));
    bind_text(statement.get(), 5, input.title.view());
    bind_text(statement.get(), 6, input.body.view());
    bind_text(statement.get(), 7, input.source_id);
    sqlite3_bind_int(statement.get(), 8, static_cast<int>(input.sensitivity));
    sqlite3_bind_int64(statement.get(), 9, common::Timestamp::now().unix_nanoseconds());
    if (input.expires_at)
        sqlite3_bind_int64(statement.get(), 10, input.expires_at->unix_nanoseconds());
    else sqlite3_bind_null(statement.get(), 10);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        const auto error = db_error(impl_->db, "insert_memory");
        (void)exec(impl_->db, "ROLLBACK", "rollback_store");
        return Result<common::MemoryId>::failure(error);
    }
    const auto audit = insert_audit(impl_->db, memory_id, session_id,
        "system", "store", "success", request_id);
    if (!audit) {
        const auto error = *audit.error_if();
        (void)exec(impl_->db, "ROLLBACK", "rollback_store");
        return Result<common::MemoryId>::failure(error);
    }
    const auto commit = exec(impl_->db, "COMMIT", "commit_store");
    if (!commit) {
        const auto error = *commit.error_if();
        (void)exec(impl_->db, "ROLLBACK", "rollback_store");
        return Result<common::MemoryId>::failure(error);
    }
    return Result<common::MemoryId>::success(*id.value_if());
}

Result<MemoryRecord> MemoryStore::retrieve(
    const common::SessionId& session_id, const common::MemoryId& memory_id) {
    std::lock_guard lock(impl_->mutex);
    if (!is_open()) return Result<MemoryRecord>::failure(Error::make(
        ErrorCode::internal_error, "memory store is not open", "memory", "retrieve"));
    const std::string sql = std::string("SELECT ") + record_columns +
        " FROM memories m WHERE m.id=? AND m.session_id=? AND "
        "(m.expires_at IS NULL OR m.expires_at>?)";
    Statement statement(impl_->db, sql.c_str());
    if (!statement) return Result<MemoryRecord>::failure(db_error(impl_->db, "prepare_retrieve"));
    bind_text(statement.get(), 1, memory_id.to_string());
    bind_text(statement.get(), 2, session_id.to_string());
    sqlite3_bind_int64(statement.get(), 3, common::Timestamp::now().unix_nanoseconds());
    if (sqlite3_step(statement.get()) != SQLITE_ROW)
        return Result<MemoryRecord>::failure(Error::make(
            ErrorCode::invalid_argument, "memory not found in this session",
            "memory", "retrieve"));
    return read_record(statement.get());
}

Result<std::vector<MemoryRecord>> MemoryStore::search(const SearchQuery& query) {
    std::lock_guard lock(impl_->mutex);
    if (!is_open()) return Result<std::vector<MemoryRecord>>::failure(Error::make(
        ErrorCode::internal_error, "memory store is not open", "memory", "search"));
    if (query.cancellation.is_cancelled())
        return Result<std::vector<MemoryRecord>>::failure(Error::make(
            ErrorCode::cancelled, "memory search was cancelled", "memory", "search"));
    if (query.text.empty() || query.session_id.is_nil())
        return Result<std::vector<MemoryRecord>>::failure(Error::make(
            ErrorCode::invalid_argument, "search text and session are required",
            "memory", "search"));
    const auto limit = std::min(query.limit, impl_->limits.max_results);
    const auto context_limit = std::min(query.max_context_bytes,
                                        impl_->limits.max_context_bytes);
    if (limit == 0 || context_limit == 0)
        return Result<std::vector<MemoryRecord>>::failure(Error::make(
            ErrorCode::invalid_argument, "search limits must be positive",
            "memory", "search"));

    std::string sql;
    if (impl_->fts) {
        sql = std::string("SELECT ") + record_columns +
            " FROM memories_fts JOIN memories m ON m.rowid=memories_fts.rowid"
            " WHERE memories_fts MATCH ? AND m.session_id=? AND m.sensitivity<2"
            " AND (m.expires_at IS NULL OR m.expires_at>?)";
    } else {
        sql = std::string("SELECT ") + record_columns +
            " FROM memories m WHERE m.session_id=? AND m.sensitivity<2 AND (m.title LIKE ? ESCAPE '\\' OR m.body LIKE ? ESCAPE '\\')"
            " AND (m.expires_at IS NULL OR m.expires_at>?)";
    }
    if (query.kind) sql += " AND m.kind=?";
    sql += impl_->fts ? " ORDER BY rank LIMIT ?" : " ORDER BY m.created_at DESC LIMIT ?";

    Statement statement(impl_->db, sql.c_str());
    if (!statement) return Result<std::vector<MemoryRecord>>::failure(
        db_error(impl_->db, "prepare_search"));
    int bind_index = 1;
    if (impl_->fts) {
        const auto fts_query = literal_fts_query(query.text.view());
        if (fts_query.empty()) return Result<std::vector<MemoryRecord>>::failure(
            Error::make(ErrorCode::invalid_argument, "search has no searchable terms",
                        "memory", "search"));
        bind_text(statement.get(), bind_index++, fts_query);
        bind_text(statement.get(), bind_index++, query.session_id.to_string());
    } else {
        bind_text(statement.get(), bind_index++, query.session_id.to_string());
        const std::string pattern = literal_like_pattern(query.text.view());
        bind_text(statement.get(), bind_index++, pattern);
        bind_text(statement.get(), bind_index++, pattern);
    }
    sqlite3_bind_int64(statement.get(), bind_index++,
                       common::Timestamp::now().unix_nanoseconds());
    if (query.kind) sqlite3_bind_int(statement.get(), bind_index++,
                                     static_cast<int>(*query.kind));
    sqlite3_bind_int64(statement.get(), bind_index, static_cast<sqlite3_int64>(limit));

    std::vector<MemoryRecord> records;
    std::size_t used_bytes = 0;
    SearchDeadline deadline{
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(impl_->limits.search_timeout_ms),
        query.cancellation};
    sqlite3_progress_handler(impl_->db, 1000, interrupt_search,
                             &deadline);
    int step_result = SQLITE_OK;
    while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        const auto title_bytes = static_cast<std::size_t>(
            std::max(0, sqlite3_column_bytes(statement.get(), 4)));
        const auto body_bytes = static_cast<std::size_t>(
            std::max(0, sqlite3_column_bytes(statement.get(), 5)));
        const auto record_bytes = title_bytes + body_bytes;
        if (record_bytes > context_limit - std::min(used_bytes, context_limit)) continue;
        auto record = read_record(statement.get());
        if (!record) {
            sqlite3_progress_handler(impl_->db, 0, nullptr, nullptr);
            return Result<std::vector<MemoryRecord>>::failure(*record.error_if());
        }
        used_bytes += record_bytes;
        records.push_back(std::move(*record.value_if()));
    }
    sqlite3_progress_handler(impl_->db, 0, nullptr, nullptr);
    if (step_result != SQLITE_DONE) {
        if (step_result == SQLITE_INTERRUPT) {
            const auto code = query.cancellation.is_cancelled()
                ? ErrorCode::cancelled : ErrorCode::tool_timeout;
            return Result<std::vector<MemoryRecord>>::failure(Error::make(
                code, query.cancellation.is_cancelled()
                    ? "memory search was cancelled"
                    : "memory search exceeded its time budget",
                "memory", "search"));
        }
        return Result<std::vector<MemoryRecord>>::failure(db_error(impl_->db, "search"));
    }
    return Result<std::vector<MemoryRecord>>::success(std::move(records));
}

Status MemoryStore::promote(
    const common::SessionId& session_id, const common::MemoryId& memory_id,
    MemoryKind target_kind) {
    std::lock_guard lock(impl_->mutex);
    if (target_kind != MemoryKind::episodic && target_kind != MemoryKind::semantic)
        return Status::failure(Error::make(ErrorCode::invalid_argument,
            "promotion target must be episodic or semantic", "memory", "promote"));
    if (!is_open()) return Status::failure(Error::make(
        ErrorCode::internal_error, "memory store is not open", "memory", "promote"));
    const auto begin = exec(impl_->db, "BEGIN IMMEDIATE", "begin_promote");
    if (!begin) return begin;
    Statement statement(impl_->db,
        "UPDATE memories SET kind=? WHERE id=? AND session_id=? AND kind=0");
    if (!statement) {
        const auto error=db_error(impl_->db,"prepare_promote");
        (void)exec(impl_->db,"ROLLBACK","rollback_promote");
        return Status::failure(error);
    }
    sqlite3_bind_int(statement.get(),1,static_cast<int>(target_kind));
    bind_text(statement.get(),2,memory_id.to_string());
    bind_text(statement.get(),3,session_id.to_string());
    if(sqlite3_step(statement.get())!=SQLITE_DONE){
        const auto error=db_error(impl_->db,"promote");
        (void)exec(impl_->db,"ROLLBACK","rollback_promote");
        return Status::failure(error);
    }
    if(sqlite3_changes(impl_->db)==0){
        (void)exec(impl_->db,"ROLLBACK","rollback_promote");
        return Status::failure(Error::make(ErrorCode::invalid_argument,
            "working memory not found in this session", "memory", "promote"));
    }
    const auto audit=insert_audit(impl_->db,memory_id.to_string(),
        session_id.to_string(),"system","promote","success",std::nullopt);
    if(!audit){const auto error=*audit.error_if();(void)exec(impl_->db,"ROLLBACK","rollback_promote");return Status::failure(error);}
    const auto commit=exec(impl_->db,"COMMIT","commit_promote");
    if(!commit){const auto error=*commit.error_if();(void)exec(impl_->db,"ROLLBACK","rollback_promote");return Status::failure(error);}
    return Status::success();
}

Status MemoryStore::update_metadata(
    const common::SessionId& session_id, const common::MemoryId& memory_id,
    Sensitivity sensitivity, std::optional<common::Timestamp> expires_at) {
    std::lock_guard lock(impl_->mutex);
    if (!is_open()) return Status::failure(Error::make(
        ErrorCode::internal_error, "memory store is not open", "memory", "update_metadata"));
    if (static_cast<int>(sensitivity) > 2)
        return Status::failure(Error::make(ErrorCode::invalid_argument,
            "invalid sensitivity value", "memory", "update_metadata"));
    const auto begin = exec(impl_->db, "BEGIN IMMEDIATE", "begin_metadata_update");
    if (!begin) return begin;
    Statement statement(impl_->db,
        "UPDATE memories SET sensitivity=?,expires_at=? WHERE id=? AND session_id=?");
    if (!statement) {
        const auto error = db_error(impl_->db, "prepare_update_metadata");
        (void)exec(impl_->db, "ROLLBACK", "rollback_metadata_update");
        return Status::failure(error);
    }
    sqlite3_bind_int(statement.get(), 1, static_cast<int>(sensitivity));
    if (expires_at) sqlite3_bind_int64(statement.get(), 2, expires_at->unix_nanoseconds());
    else sqlite3_bind_null(statement.get(), 2);
    bind_text(statement.get(), 3, memory_id.to_string());
    bind_text(statement.get(), 4, session_id.to_string());
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        const auto error = db_error(impl_->db, "update_metadata");
        (void)exec(impl_->db, "ROLLBACK", "rollback_metadata_update");
        return Status::failure(error);
    }
    if (sqlite3_changes(impl_->db) == 0) {
        (void)exec(impl_->db, "ROLLBACK", "rollback_metadata_update");
        return Status::failure(Error::make(ErrorCode::invalid_argument,
            "memory not found in this session", "memory", "update_metadata"));
    }
    const auto audit = insert_audit(impl_->db, memory_id.to_string(),
        session_id.to_string(), "system", "metadata_update", "success", std::nullopt);
    if (!audit) {
        const auto error = *audit.error_if();
        (void)exec(impl_->db, "ROLLBACK", "rollback_metadata_update");
        return Status::failure(error);
    }
    const auto commit = exec(impl_->db, "COMMIT", "commit_metadata_update");
    if (!commit) {
        const auto error = *commit.error_if();
        (void)exec(impl_->db, "ROLLBACK", "rollback_metadata_update");
        return Status::failure(error);
    }
    return Status::success();
}

Status MemoryStore::erase(const common::SessionId& session_id,
    const common::MemoryId& memory_id, std::string actor,
    const std::optional<common::RequestId>& request_id) {
    std::lock_guard lock(impl_->mutex);
    if (!is_open()) return Status::failure(Error::make(
        ErrorCode::internal_error, "memory store is not open", "memory", "erase"));
    if (actor.empty()) return Status::failure(Error::make(
        ErrorCode::invalid_argument, "audit actor is required", "memory", "erase"));
    const auto begin = exec(impl_->db, "BEGIN IMMEDIATE", "begin_erase");
    if (!begin) return begin;
    {
        Statement exists(impl_->db, "SELECT 1 FROM memories WHERE id=? AND session_id=?");
        if (!exists) { (void)exec(impl_->db, "ROLLBACK", "rollback_erase");
            return Status::failure(db_error(impl_->db, "prepare_erase")); }
        bind_text(exists.get(), 1, memory_id.to_string());
        bind_text(exists.get(), 2, session_id.to_string());
        if (sqlite3_step(exists.get()) != SQLITE_ROW) {
            (void)exec(impl_->db, "ROLLBACK", "rollback_erase");
            return Status::failure(Error::make(ErrorCode::invalid_argument,
                "memory not found in this session", "memory", "erase"));
        }
    }
    const auto audit = insert_audit(impl_->db, memory_id.to_string(),
        session_id.to_string(), actor, "delete", "success", request_id);
    if (!audit) { const auto error=*audit.error_if();
        (void)exec(impl_->db, "ROLLBACK", "rollback_erase");
        return Status::failure(error); }
    Statement remove(impl_->db, "DELETE FROM memories WHERE id=? AND session_id=?");
    if (!remove) { (void)exec(impl_->db, "ROLLBACK", "rollback_erase");
        return Status::failure(db_error(impl_->db, "prepare_delete")); }
    bind_text(remove.get(), 1, memory_id.to_string());
    bind_text(remove.get(), 2, session_id.to_string());
    if (sqlite3_step(remove.get()) != SQLITE_DONE) { const auto error=db_error(impl_->db,"delete");
        (void)exec(impl_->db, "ROLLBACK", "rollback_erase");
        return Status::failure(error); }
    const auto commit = exec(impl_->db, "COMMIT", "commit_erase");
    if (!commit) { const auto error=*commit.error_if();
        (void)exec(impl_->db, "ROLLBACK", "rollback_erase");
        return Status::failure(error); }
    return Status::success();
}

Result<std::size_t> MemoryStore::clear_working(const common::SessionId& session_id) {
    std::lock_guard lock(impl_->mutex);
    if (!is_open()) return Result<std::size_t>::failure(Error::make(
        ErrorCode::internal_error, "memory store is not open", "memory", "clear_working"));
    const auto begin = exec(impl_->db, "BEGIN IMMEDIATE", "begin_clear_working");
    if (!begin) return Result<std::size_t>::failure(*begin.error_if());
    Statement select(impl_->db, "SELECT id FROM memories WHERE session_id=? AND kind=0");
    if (!select) { const auto error=db_error(impl_->db,"prepare_clear_working");
        (void)exec(impl_->db,"ROLLBACK","rollback_clear_working");
        return Result<std::size_t>::failure(error); }
    bind_text(select.get(),1,session_id.to_string());
    std::vector<std::string> ids;
    while(sqlite3_step(select.get())==SQLITE_ROW) ids.push_back(column_text(select.get(),0));
    for(const auto& id:ids) {
        const auto audit=insert_audit(impl_->db,id,session_id.to_string(),
            "session","session_close","success",std::nullopt);
        if(!audit){const auto error=*audit.error_if();(void)exec(impl_->db,"ROLLBACK","rollback_clear_working");return Result<std::size_t>::failure(error);}
    }
    Statement remove(impl_->db,"DELETE FROM memories WHERE session_id=? AND kind=0");
    if(!remove){const auto error=db_error(impl_->db,"prepare_clear_delete");(void)exec(impl_->db,"ROLLBACK","rollback_clear_working");return Result<std::size_t>::failure(error);}
    bind_text(remove.get(),1,session_id.to_string());
    if(sqlite3_step(remove.get())!=SQLITE_DONE){const auto error=db_error(impl_->db,"clear_delete");(void)exec(impl_->db,"ROLLBACK","rollback_clear_working");return Result<std::size_t>::failure(error);}
    const auto commit=exec(impl_->db,"COMMIT","commit_clear_working");
    if(!commit){const auto error=*commit.error_if();(void)exec(impl_->db,"ROLLBACK","rollback_clear_working");return Result<std::size_t>::failure(error);}
    return Result<std::size_t>::success(ids.size());
}

Result<std::size_t> MemoryStore::prune_expired(common::Timestamp now) {
    std::lock_guard lock(impl_->mutex);
    if (!is_open()) return Result<std::size_t>::failure(Error::make(
        ErrorCode::internal_error, "memory store is not open", "memory", "prune_expired"));
    const auto begin=exec(impl_->db,"BEGIN IMMEDIATE","begin_prune");
    if(!begin)return Result<std::size_t>::failure(*begin.error_if());
    Statement select(impl_->db,"SELECT id,session_id FROM memories WHERE expires_at IS NOT NULL AND expires_at<=?");
    if(!select){const auto error=db_error(impl_->db,"prepare_prune");(void)exec(impl_->db,"ROLLBACK","rollback_prune");return Result<std::size_t>::failure(error);}
    sqlite3_bind_int64(select.get(),1,now.unix_nanoseconds());
    std::vector<std::pair<std::string,std::string>> expired;
    while(sqlite3_step(select.get())==SQLITE_ROW)expired.emplace_back(column_text(select.get(),0),column_text(select.get(),1));
    for(const auto& item:expired){const auto audit=insert_audit(impl_->db,item.first,item.second,"retention","expire","success",std::nullopt);if(!audit){const auto error=*audit.error_if();(void)exec(impl_->db,"ROLLBACK","rollback_prune");return Result<std::size_t>::failure(error);}}
    Statement remove(impl_->db,"DELETE FROM memories WHERE expires_at IS NOT NULL AND expires_at<=?");
    if(!remove){const auto error=db_error(impl_->db,"prepare_expiry_delete");(void)exec(impl_->db,"ROLLBACK","rollback_prune");return Result<std::size_t>::failure(error);}
    sqlite3_bind_int64(remove.get(),1,now.unix_nanoseconds());
    if(sqlite3_step(remove.get())!=SQLITE_DONE){const auto error=db_error(impl_->db,"expiry_delete");(void)exec(impl_->db,"ROLLBACK","rollback_prune");return Result<std::size_t>::failure(error);}
    const auto commit=exec(impl_->db,"COMMIT","commit_prune");
    if(!commit){const auto error=*commit.error_if();(void)exec(impl_->db,"ROLLBACK","rollback_prune");return Result<std::size_t>::failure(error);}
    return Result<std::size_t>::success(expired.size());
}

Status MemoryStore::backup_to(const std::string& destination_path) {
    std::lock_guard lock(impl_->mutex);
    if(!is_open())return Status::failure(Error::make(ErrorCode::internal_error,"memory store is not open","memory","backup"));
    if(destination_path.empty())return Status::failure(Error::make(ErrorCode::invalid_argument,"backup path is empty","memory","backup"));
    sqlite3* destination=nullptr;
    if(sqlite3_open_v2(destination_path.c_str(),&destination,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK){
        const auto error=db_error(destination,"backup_open");if(destination)sqlite3_close(destination);return Status::failure(error);}
    sqlite3_backup* backup=sqlite3_backup_init(destination,"main",impl_->db,"main");
    if(!backup){const auto error=db_error(destination,"backup_init");sqlite3_close(destination);return Status::failure(error);}
    const int step=sqlite3_backup_step(backup,-1);
    const int finish=sqlite3_backup_finish(backup);
    if((step!=SQLITE_DONE&&step!=SQLITE_OK)||finish!=SQLITE_OK){const auto error=db_error(destination,"backup_copy");sqlite3_close(destination);return Status::failure(error);}
    if(sqlite3_close(destination)!=SQLITE_OK)return Status::failure(Error::make(
        ErrorCode::internal_error,"failed to close backup database","memory","backup_close"));
    return Status::success();
}

Status MemoryStore::integrity_check() {
    std::lock_guard lock(impl_->mutex);
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
