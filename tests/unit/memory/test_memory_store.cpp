#include "morok/common/ids.hpp"
#include "morok/common/utf8_text.hpp"
#include "morok/memory/memory_store.hpp"
#include "morok/memory/memory_subsystem.hpp"
#include "morok/core/configuration.hpp"
#include "morok/core/session_registry.hpp"

#include <sqlite3.h>

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    using namespace morok;

    const std::string database = "morok-memory-test.sqlite";
    const std::string backup = "morok-memory-test-backup.sqlite";
    const std::string integrated_database = "morok-memory-integration.sqlite";
    std::remove(database.c_str());
    std::remove(backup.c_str());
    std::remove(integrated_database.c_str());

    auto session_result = common::SessionId::generate();
    auto other_session_result = common::SessionId::generate();
    assert(session_result && other_session_result);
    const auto session = *session_result.value_if();
    const auto other_session = *other_session_result.value_if();

    auto title = common::Utf8Text::from_utf8("Projeto Morok");
    auto body = common::Utf8Text::from_utf8("Memória episódica sobre treinamento e arquitetura.");
    auto query_text = common::Utf8Text::from_utf8("treinamento");
    auto working_title = common::Utf8Text::from_utf8("Estado temporário");
    auto working_body = common::Utf8Text::from_utf8("Rascunho da sessão ativa.");
    assert(title && body && query_text && working_title && working_body);

    memory::MemoryStore store;
    assert(store.open(database));
    assert(store.is_open());

    memory::MemoryInput episodic{
        session, memory::MemoryKind::episodic,
        *title.value_if(), *body.value_if(),
        memory::MemorySource::user, "conversation:turn-1",
        memory::Sensitivity::normal, std::nullopt};
    auto id = store.store(episodic);
    assert(id);

    auto retrieved = store.retrieve(session, *id.value_if());
    assert(retrieved);
    assert(retrieved.value_if()->body.view() == body.value_if()->view());
    assert(retrieved.value_if()->source_id == "conversation:turn-1");

    memory::SearchQuery search{
        session, *query_text.value_if(), 20, 4096, std::nullopt};
    auto results = store.search(search);
    assert(results);
    assert(results.value_if()->size() == 1);
    search.max_context_bytes = 1;
    auto bounded_context = store.search(search);
    assert(bounded_context && bounded_context.value_if()->empty());
    search.max_context_bytes = 4096;

    assert(store.update_metadata(session, *id.value_if(),
        memory::Sensitivity::sensitive, std::nullopt));
    auto hidden_sensitive = store.search(search);
    assert(hidden_sensitive);
    assert(hidden_sensitive.value_if()->empty());
    assert(store.update_metadata(session, *id.value_if(),
        memory::Sensitivity::normal, std::nullopt));

    const auto id_roundtrip = common::MemoryId::from_string(id.value_if()->to_string());
    assert(id_roundtrip && *id_roundtrip.value_if() == *id.value_if());

    search.session_id = other_session;
    auto isolated = store.search(search);
    assert(isolated);
    assert(isolated.value_if()->empty());
    assert(!store.retrieve(other_session, *id.value_if()));

    memory::MemoryInput working{
        session, memory::MemoryKind::working,
        *working_title.value_if(), *working_body.value_if(),
        memory::MemorySource::system, "runtime:session",
        memory::Sensitivity::private_data, std::nullopt};
    auto working_id = store.store(working);
    assert(working_id);
    assert(store.promote(session, *working_id.value_if(),
        memory::MemoryKind::semantic));
    auto promoted = store.retrieve(session, *working_id.value_if());
    assert(promoted);
    assert(promoted.value_if()->kind == memory::MemoryKind::semantic);

    auto second_working = store.store(working);
    assert(second_working);
    auto cleared = store.clear_working(session);
    assert(cleared && *cleared.value_if() == 1);
    assert(store.retrieve(session, *working_id.value_if()));
    assert(!store.retrieve(session, *second_working.value_if()));

    memory::MemoryInput expiring{
        session, memory::MemoryKind::semantic,
        *title.value_if(), *body.value_if(),
        memory::MemorySource::document, "document:test",
        memory::Sensitivity::normal,
        common::Timestamp::from_unix_nanoseconds(1)};
    auto expired_id = store.store(expiring);
    assert(expired_id);
    auto pruned = store.prune_expired(common::Timestamp::now());
    assert(pruned && *pruned.value_if() == 1);
    assert(!store.retrieve(session, *expired_id.value_if()));

    assert(store.erase(session, *working_id.value_if(), "test"));
    assert(store.erase(session, *id.value_if(), "user"));
    assert(!store.retrieve(session, *id.value_if()));
    assert(store.integrity_check());
    assert(store.backup_to(backup));
    assert(store.close());
    assert(!store.is_open());

    sqlite3* audit_db = nullptr;
    assert(sqlite3_open(database.c_str(), &audit_db) == SQLITE_OK);
    sqlite3_stmt* audit = nullptr;
    assert(sqlite3_prepare_v2(audit_db,
        "SELECT COUNT(*) FROM memory_audit WHERE action='delete' AND outcome='success'",
        -1, &audit, nullptr) == SQLITE_OK);
    assert(sqlite3_step(audit) == SQLITE_ROW);
    assert(sqlite3_column_int(audit, 0) == 2);
    sqlite3_finalize(audit);
    sqlite3_close(audit_db);

    core::SessionRegistry registry(2);
    auto active_session = registry.create();
    assert(active_session);
    memory::MemorySubsystem subsystem(integrated_database);
    assert(subsystem.initialize(core::RuntimeConfig{}));
    memory::MemoryInput transient{
        *active_session.value_if(), memory::MemoryKind::working,
        *working_title.value_if(), *working_body.value_if(),
        memory::MemorySource::user, "session:transient",
        memory::Sensitivity::normal, std::nullopt};
    auto transient_id = subsystem.store().store(transient);
    assert(transient_id);
    assert(subsystem.close_session(registry, *active_session.value_if()));
    assert(!registry.contains(*active_session.value_if()));
    assert(!subsystem.store().retrieve(*active_session.value_if(), *transient_id.value_if()));
    assert(subsystem.shutdown());

    std::remove(database.c_str());
    std::remove(backup.c_str());
    std::remove(integrated_database.c_str());
    std::remove((integrated_database + "-wal").c_str());
    std::remove((integrated_database + "-shm").c_str());
    std::remove((database + "-wal").c_str());
    std::remove((database + "-shm").c_str());
    std::remove((backup + "-wal").c_str());
    std::remove((backup + "-shm").c_str());
    return 0;
}
