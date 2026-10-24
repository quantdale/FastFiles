#include <windows.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "IndexPipeline.h"
#include "../TestSupport.h"
#include "../indexstore/SqliteWriteFaultVfs.h"

namespace {
using namespace ffindexstore;
using namespace fftest;
using Fault = SqliteWriteFaultVfs;

bool Sql(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}
int64_t Scalar(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    int64_t result = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        result = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}
std::vector<EntryChange> Rows(uint64_t first, uint64_t count, const std::u16string& name) {
    std::vector<EntryChange> result;
    for (uint64_t i = first; i < first + count; ++i) {
        EntryRecord row;
        row.id = {i, 0};
        row.parentId = {1, 0};
        row.name = name;
        result.push_back({EntryChangeKind::Upsert, row});
    }
    return result;
}
void Verify(const std::string& path, int64_t count, bool ready = true) {
    sqlite3* db = nullptr;
    Check(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "raw reopen succeeds before Store can repair derived state");
    if (!db) return;
    sqlite3_stmt* stmt = nullptr;
    bool integrity = false;
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        integrity = text && std::strcmp(text, "ok") == 0;
    }
    sqlite3_finalize(stmt);
    Check(integrity, "SQLite integrity_check returns ok");
    Check(Scalar(db, "SELECT count(*) FROM entries;") == count, "authoritative count equals expected committed prefix");
    Check(Scalar(db, "SELECT count(*) FROM volumes;") == 1, "volume identity survives failure");
    if (ready) {
        Check(Scalar(db, "SELECT count(*) FROM entry_name_index_map;") == count &&
              Scalar(db, "SELECT count(*) FROM entry_name_terms;") == count,
              "FTS and identity map cover authoritative rows before repair");
        Check(Scalar(db, "SELECT count(*) FROM entry_name_index_map m LEFT JOIN entries e ON "
                         "e.volume_id=m.volume_id AND e.frn_low=m.frn_low AND e.frn_high=m.frn_high "
                         "LEFT JOIN entry_name_terms t ON t.rowid=m.term_rowid "
                         "WHERE e.volume_id IS NULL OR t.rowid IS NULL OR t.name_folded != lower(e.name);") == 0,
              "no orphan or stale candidate identity/name");
        Check(Scalar(db, "SELECT count(*) FROM entry_name_exceptions x LEFT JOIN entry_name_index_map m "
                         "ON m.term_rowid=x.term_rowid WHERE m.term_rowid IS NULL;") == 0,
              "no orphan candidate exceptions");
        Check(Sql(db, "INSERT INTO entry_name_terms(entry_name_terms) VALUES('integrity-check');"),
              "FTS internal integrity check succeeds");
    }
    sqlite3_close(db);
}
uint64_t Bytes(const std::string& path) {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    return error ? 0 : bytes;
}
bool OpenCaptured(Store& store, const std::string& path) {
    Fault::captureNext = true;
    return store.Open(path);
}
void ReportFault(const char* cell) {
    std::printf("FULL cell=%s fired=%d wal=%d offset=%lld bytes=%d sql=%s\n", cell,
                Fault::fired ? 1 : 0, Fault::failureWal ? 1 : 0,
                static_cast<long long>(Fault::failureOffset), Fault::failureBytes, Fault::failureSql.c_str());
}
void TestReaders(const std::filesystem::path& dir) {
    const auto path = (dir / "readers.db").string();
    Store store;
    Check(OpenCaptured(store, path), "reader fixture opens");
    const auto volume = store.GetOrCreateVolume(VolumeKey{});
    if (!volume) { Check(false, "reader volume exists"); return; }
    Check(store.ApplyBatch(*volume, Rows(2, 2000, u"pressure_seed.txt")), "reader baseline commits");
    Check(store.CheckpointIfWalExceeds(0), "no reader checkpoint succeeds");
    const auto pin = [&path]() {
        sqlite3* db = nullptr;
        Check(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK,
              "independent reader opens");
        Check(Sql(db, "BEGIN;") && Scalar(db, "SELECT count(*) FROM entries;") >= 2000,
              "reader pins a real snapshot");
        return db;
    };
    sqlite3* first = pin();
    uint64_t peak = 0;
    for (uint64_t pass = 0; pass < 4; ++pass) {
        Check(store.ApplyBatch(*volume, Rows(2, 2000, u"pressure_changed_long_name.txt")),
              "writer progresses while old reader snapshot is pinned");
        peak = (std::max)(peak, Bytes(path + "-wal"));
        const auto start = std::chrono::steady_clock::now();
        Check(!store.CheckpointIfWalExceeds(0), "RESTART reports busy rather than false completion under pinned reader");
        const auto checkpoint = store.GetCheckpointDiagnostics();
        Check(checkpoint.busyResults > 0 && checkpoint.lastResultCode == SQLITE_BUSY &&
              checkpoint.logFrames > checkpoint.checkpointedFrames && checkpoint.restartAttempted,
              "checkpoint diagnostics expose BUSY and pinned-frame backlog");
        Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "checkpoint failure is bounded, not a busy loop");
    }
    sqlite3* second = pin();
    Check(store.ApplyBatch(*volume, Rows(2, 2000, u"pressure_second_generation.txt")), "overlapping-reader writer commits");
    Check(Sql(first, "ROLLBACK;"), "first reader releases snapshot");
    sqlite3_close(first);
    Check(!store.CheckpointIfWalExceeds(0), "second overlapping reader still pins newer frames");
    Check(Sql(second, "ROLLBACK;"), "second reader releases snapshot");
    sqlite3_close(second);
    Check(store.CheckpointIfWalExceeds(0), "maintenance recovers after all readers release");
    int frames = -1;
    int done = -1;
    Check(sqlite3_wal_checkpoint_v2(Fault::capturedDb, nullptr, SQLITE_CHECKPOINT_TRUNCATE, &frames, &done) == SQLITE_OK,
          "unblocked WAL can be physically reclaimed (test-only TRUNCATE probe)");
    Check(Bytes(path + "-wal") == 0, "released WAL truncates to zero bytes");
    // An early-stopped real StoreReadSession keeps its snapshot until teardown.
    auto cancelled = store.OpenReadSession();
    Check(cancelled != nullptr, "cancellable production reader opens");
    if (cancelled) {
        Check(cancelled->ForEachEntryUntil(*volume, [](const EntryRecord&) { return false; }),
              "reader early cancellation stops cleanly");
        Check(store.ApplyBatch(*volume, Rows(2, 2000, u"pressure_after_cancel.txt")), "writer progresses after reader early stop");
        cancelled.reset();
        Check(store.CheckpointIfWalExceeds(0), "cancelled reader teardown releases snapshot");
    }
    std::printf("READERS peak_wal_bytes=%llu\n", static_cast<unsigned long long>(peak));
    store.Close();
    Verify(path, 2000);
}
void TestFull(const std::filesystem::path& dir) {
    // Statement-scoped cases force a tiny SQLite cache so xWrite spills during
    // that statement, rather than merely labelling a COMMIT fault as FTS work.
    struct Case { const char* label; const char* sql; bool removal; };
    const Case cases[] = {
        {"ordinary-ingestion", "INSERT INTO entries", false},
        {"candidate-map", "INSERT INTO entry_name_index_map", false},
        {"fts-mutation", "entry_name_terms", false},
        {"reconciliation-removal", "DELETE FROM entries", true},
        {"commit", "COMMIT", false},
    };
    for (const auto& cell : cases) {
        const auto path = (dir / (std::string(cell.label) + ".db")).string();
        Store store;
        Check(OpenCaptured(store, path), "FULL fixture opens");
        const auto volume = store.GetOrCreateVolume(VolumeKey{});
        if (!volume) { Check(false, "FULL volume exists"); continue; }
        Check(store.ApplyBatch(*volume, Rows(2, 2000, u"original_name.txt")), "FULL baseline commits");
        Check(store.CheckpointIfWalExceeds(0), "FULL baseline checkpoint succeeds");
        Check(Sql(Fault::capturedDb, "PRAGMA cache_size=8; PRAGMA cache_spill=ON; PRAGMA wal_autocheckpoint=0;"),
              "test-only small cache forces actual write operations within SQL statements");
        auto changes = Rows(2, 2000, u"changed_fts_candidate_name_with_new_trigrams.txt");
        if (cell.removal) for (auto& change : changes) change.kind = EntryChangeKind::Remove;
        Fault::Arm(1, Fault::FileKind::Wal, cell.sql);
        const bool applied = store.ApplyBatch(*volume, changes);
        Fault::Disarm();
        ReportFault(cell.label);
        Check(Fault::fired, "specified SQLite statement actually reached failing xWrite");
        Check(!applied, "actual SQLITE_FULL is not reported as successful durable mutation");
        Check(sqlite3_get_autocommit(Fault::capturedDb) != 0, "failed batch leaves no open transaction");
        const auto original = store.FindEntry(*volume, {2, 0});
        Check(original && original->name == u"original_name.txt", "failed mutation preserves original entry");
        store.Close();
        Verify(path, 2000);
        Check(OpenCaptured(store, path), "FULL database reopens");
        Check(store.ApplyBatch(*volume, changes), "retry after space restoration commits");
        store.Close();
        Verify(path, cell.removal ? 0 : 2000);
    }
    const auto path = (dir / "checkpoint-full.db").string();
    Store store;
    Check(OpenCaptured(store, path), "checkpoint FULL fixture opens");
    const auto volume = store.GetOrCreateVolume(VolumeKey{});
    if (!volume) return;
    Check(Sql(Fault::capturedDb, "PRAGMA wal_autocheckpoint=0;"), "auto checkpoint disabled only for controlled checkpoint injection");
    Check(store.ApplyBatch(*volume, Rows(2, 2000, u"checkpoint_name.txt")), "checkpoint input is committed");
    Fault::Arm(1, Fault::FileKind::Database);
    Check(!store.CheckpointIfWalExceeds(0), "checkpoint FULL is propagated as failure");
    Fault::Disarm();
    ReportFault("checkpoint");
    Check(Fault::fired, "checkpoint failed actual database-file xWrite");
    Check(store.GetCheckpointDiagnostics().lastResultCode == SQLITE_FULL &&
          store.GetCheckpointDiagnostics().errorResults > 0,
          "checkpoint FULL is retained in observable diagnostics");
    Check(store.CountEntries(*volume) == 2000, "checkpoint failure does not lose committed WAL entries");
    Check(store.CheckpointIfWalExceeds(0), "checkpoint retry succeeds after restoration");
    store.Close();
    Verify(path, 2000);
}
void TestRebuildFull(const std::filesystem::path& dir) {
    for (const bool migration : {false, true}) {
        const auto path = (dir / (migration ? "migration.db" : "rebuild.db")).string();
        Store store;
        Check(OpenCaptured(store, path), "rebuild fixture opens");
        const auto volume = store.GetOrCreateVolume(VolumeKey{});
        if (!volume) continue;
        Check(store.ApplyBatch(*volume, Rows(2, 2000, u"rebuild_name.txt")), "rebuild seed commits");
        store.Close();
        sqlite3* raw = nullptr;
        Check(sqlite3_open(path.c_str(), &raw) == SQLITE_OK, "derived-state preparation opens");
        Check(Sql(raw, migration ?
              "DROP TABLE entry_name_terms; DROP TABLE entry_name_exceptions; DROP TABLE entry_name_index_map; "
              "DROP TABLE entry_name_index_meta; PRAGMA user_version=2;" :
              "UPDATE entry_name_index_meta SET state=0;"), "derived-state repair obligation prepared");
        sqlite3_close(raw);
        Fault::Arm(1, Fault::FileKind::Wal);
        bool corrupt = false;
        Fault::captureNext = true;
        Check(!store.Open(path, &corrupt), "migration/rebuild write failure fails open closed");
        Fault::Disarm();
        ReportFault(migration ? "migration" : "rebuild");
        Check(Fault::fired && !corrupt, "FULL is injected and is not misclassified as corruption");
        store.Close();
        Verify(path, 2000, false);
        Check(OpenCaptured(store, path), "retry rebuild/migration succeeds after restoration");
        store.Close();
        Verify(path, 2000);
    }
}
void TestCandidateRecoveryRegressions(const std::filesystem::path& dir) {
    for (const auto* table : {"entry_name_terms", "entry_name_exceptions", "entry_name_index_map", "entry_name_index_meta"}) {
        const auto path = (dir / (std::string(table) + ".db")).string();
        Store store;
        Check(OpenCaptured(store, path), "missing-table seed opens");
        const auto volume = store.GetOrCreateVolume(VolumeKey{});
        if (!volume) continue;
        Check(store.ApplyBatch(*volume, Rows(2, 1, u"durable_hit.txt")), "missing-table seed commits");
        Check(Sql(Fault::capturedDb, ("DROP TABLE " + std::string(table) + ";").c_str()), "one derived table is dropped without clearing readiness");
        store.Close();
        Store::OpenPhaseTimings timings;
        Check(store.Open(path, nullptr, &timings), "missing-table store recovers");
        Check(timings.candidateIndexRebuilt, "every missing required structure forces rebuild before ready publication");
        auto reader = store.OpenReadSession();
        size_t found = 0;
        Check(reader && reader->ForEachEntryWithAsciiNameUntil(*volume, "durable_hit", [&](const EntryRecord&) { ++found; return true; }),
              "recovered candidate query completes");
        Check(found == 1, "missing FTS table cannot produce successful false-negative search");
    }
    {
        const auto path = (dir / "missing-table-interrupted-repair.db").string();
        Store store;
        Check(OpenCaptured(store, path), "interrupted missing-table fixture opens");
        const auto volume = store.GetOrCreateVolume(VolumeKey{});
        if (!volume) return;
        Check(store.ApplyBatch(*volume, Rows(2, 1, u"durable_hit.txt")), "interrupted repair seed commits");
        Check(Sql(Fault::capturedDb, "DROP TABLE entry_name_terms;"), "interrupted repair drops only FTS table");
        store.Close();
        Fault::onStatement = [](sqlite3*, const char* sql) {
            if (sql && std::strcmp(sql, "DELETE FROM entry_name_index_map;") == 0) Fault::Arm(1, Fault::FileKind::Wal);
        };
        Fault::fired = false;
        Fault::captureNext = true;
        Check(!store.Open(path), "FULL interrupts missing-table clearing transaction");
        Fault::onStatement = {};
        Fault::Disarm();
        Check(Fault::fired, "interrupted missing-table repair reaches actual failed WAL write");
        Check(store.Open(path), "missing-table recovery retries after clearing rollback");
        auto reader = store.OpenReadSession();
        size_t found = 0;
        Check(reader && reader->ForEachEntryWithAsciiNameUntil(*volume, "durable_hit", [&](const EntryRecord&) { ++found; return true; }) && found == 1,
              "interrupted schema repair cannot leave empty terms certified ready");
    }
    {
        const auto path = (dir / "enumeration-interrupt.db").string();
        Store store;
        Check(OpenCaptured(store, path), "enumeration seed opens");
        const auto volume = store.GetOrCreateVolume(VolumeKey{});
        if (!volume) return;
        Check(store.ApplyBatch(*volume, Rows(2, 10, u"enumeration_hit.txt")), "enumeration seed commits");
        Check(Sql(Fault::capturedDb, "PRAGMA user_version=2;"), "migration obligation prepared");
        store.Close();
        bool interrupted = false;
        Fault::onStatement = [&](sqlite3* db, const char* sql) {
            if (sql && std::strcmp(sql, "SELECT id FROM volumes;") == 0) {
                sqlite3_progress_handler(db, 1, [](void* state) -> int {
                    auto& fired = *static_cast<bool*>(state);
                    if (fired) return 0;
                    fired = true;
                    return 1;
                }, &interrupted);
            }
        };
        Fault::captureNext = true;
        const bool opened = store.Open(path);
        Fault::onStatement = {};
        Check(interrupted, "actual volume-enumeration VM is interrupted exactly once");
        Check(!opened, "failed volume enumeration cannot be reported as complete rebuild");
        if (opened) sqlite3_progress_handler(Fault::capturedDb, 0, nullptr, nullptr);
        store.Close();
        Check(OpenCaptured(store, path), "interrupted rebuild retry succeeds");
        store.Close();
        Verify(path, 10);
    }
    {
        const auto path = (dir / "version-read-interrupt.db").string();
        Store store;
        Check(OpenCaptured(store, path), "version-read error fixture opens");
        Check(store.GetOrCreateVolume(VolumeKey{}).has_value(), "version-read error fixture has one volume");
        Check(Sql(Fault::capturedDb, "PRAGMA user_version=4;"), "version-read fixture simulates a newer schema");
        store.Close();
        bool denied = false;
        Fault::onCapture = [&](sqlite3* db) {
            sqlite3_set_authorizer(db, [](void* state, int action, const char* name, const char*, const char*, const char*) -> int {
                auto& fired = *static_cast<bool*>(state);
                if (!fired && action == SQLITE_PRAGMA && name && std::strcmp(name, "user_version") == 0) {
                    fired = true;
                    return SQLITE_DENY;
                }
                return SQLITE_OK;
            }, &denied);
        };
        Fault::captureNext = true;
        const bool opened = store.Open(path);
        Fault::onCapture = {};
        Check(denied, "schema-version lookup actually returns a SQLite prepare error");
        Check(!opened, "unknown schema version after read error must fail closed, not become fresh version zero");
        if (opened) sqlite3_set_authorizer(Fault::capturedDb, nullptr, nullptr);
        store.Close();
        sqlite3* raw = nullptr;
        Check(sqlite3_open(path.c_str(), &raw) == SQLITE_OK, "version-read result probe opens");
        Check(Scalar(raw, "PRAGMA user_version;") == 4, "failed version read must not downgrade newer schema marker");
        sqlite3_close(raw);
    }
    {
        const auto path = (dir / "reader-move.db").string();
        Store store;
        Check(OpenCaptured(store, path), "move-cache seed opens");
        const auto volume = store.GetOrCreateVolume(VolumeKey{});
        if (!volume) return;
        Check(store.ApplyBatch(*volume, Rows(2, 1, u"move_hit.txt")), "move-cache seed commits");
        auto a = store.OpenReadSession();
        auto count = [&](StoreReadSession& session) {
            size_t found = 0;
            Check(session.ForEachEntryWithAsciiNameUntil(*volume, "move_hit", [&](const EntryRecord&) { ++found; return true; }),
                  "moved reader query completes");
            return found;
        };
        if (!a) { Check(false, "move-cache old reader opens"); return; }
        Check(count(*a) == 1, "old reader caches ready verdict");
        Check(Sql(Fault::capturedDb, "BEGIN IMMEDIATE; UPDATE entry_name_index_meta SET state=0; DELETE FROM entry_name_terms; COMMIT;"),
              "new snapshot has incomplete candidate index");
        auto b = store.OpenReadSession();
        if (!b) { Check(false, "move-cache newer reader opens"); return; }
        *a = std::move(*b);
        Check(count(*a) == 1, "value move revalidates readiness for incoming snapshot and falls back");
    }
}

ffprotocol::MftRecordV1 Mft(uint64_t id) {
    ffprotocol::MftRecordV1 row{};
    row.fixed.fileReferenceNumber = id;
    row.fixed.parentFileReferenceNumber = 1;
    row.fixed.fileAttributes = id == 1 ? 0x10U : 0;
    row.fileName = id == 1 ? u"." : u"crash_removal.txt";
    row.fixed.fileNameLengthChars = static_cast<uint16_t>(row.fileName.size());
    return row;
}
void TestChunkTwoFull(const std::filesystem::path& dir) {
    const auto path = (dir / "chunk-two-full.db").string();
    ffengine::IndexPipeline pipeline;
    Fault::captureNext = true;
    Check(pipeline.Open(path), "actual FULL chunk fixture opens");
    const auto volume = pipeline.ResolveVolume(VolumeKey{});
    std::vector<ffprotocol::MftRecordV1> seed{Mft(1)};
    for (uint64_t id = 2; id < 12002; ++id) seed.push_back(Mft(id));
    // Required exception side-set membership must be nonvacuous.
    seed.back().fileName += u"\U0001f600";
    seed.back().fixed.fileNameLengthChars = static_cast<uint16_t>(seed.back().fileName.size());
    Check(pipeline.ApplyMftBatch(volume, seed), "actual FULL chunk seed commits including surrogate name");
    Check(Scalar(Fault::capturedDb, "SELECT count(*) FROM entry_name_exceptions;") == 1,
          "surrogate fixture requires one exception candidate");
    pipeline.BeginReconciliationPass(volume);
    Check(pipeline.ApplyMftBatch(volume, {Mft(1)}), "actual FULL rescan sees root only");
    const auto epoch = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    int commits = 0;
    Fault::onCommit = [&]() { if (++commits == 1) Fault::Arm(1, Fault::FileKind::Wal); };
    pipeline.FinishReconciliationPass(volume);
    Fault::onCommit = {};
    Fault::Disarm();
    Check(Fault::fired, "chunk-two failure comes from real SQLite WAL xWrite after committed prefix");
    ReportFault("pipeline-chunk-two");
    Check(pipeline.IsReconciliationPassActive(volume), "actual FULL retains reconciliation obligation");
    const auto aggregate = pipeline.GetFolderAggregate(volume, {1, 0});
    Check(aggregate && aggregate->itemCount == 7000, "actual FULL projection mirrors exactly 5000 committed removals");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch > epoch, "actual FULL committed prefix advances query epoch");
    Check(Scalar(Fault::capturedDb, "SELECT count(*) FROM entry_name_exceptions;") == 1,
          "unremoved surrogate retains required side-set membership before repair");
    Verify(path, 7001);
    pipeline.FinishReconciliationPass(volume);
    Check(!pipeline.IsReconciliationPassActive(volume), "actual FULL remaining chunks converge on retry");
    pipeline.Close();
    Verify(path, 1);
}

void TestReconciliationReadFailureRetainsObligation(const std::filesystem::path& dir) {
    const auto path = (dir / "reconciliation-read-error.db").string();
    ffengine::IndexPipeline pipeline;
    Fault::captureNext = true;
    Check(pipeline.Open(path), "reconciliation read-error fixture opens");
    const auto volume = pipeline.ResolveVolume(VolumeKey{});
    Check(pipeline.ApplyMftBatch(volume, {Mft(1), Mft(2)}), "reconciliation read-error rows commit");
    pipeline.BeginReconciliationPass(volume);
    Check(pipeline.ApplyMftBatch(volume, {Mft(1)}), "reconciliation read-error pass sees root only");
    bool interrupted = false;
    Fault::onStatement = [&](sqlite3* db, const char* sql) {
        if (sql && std::strcmp(sql, "SELECT frn_low, frn_high FROM entries WHERE volume_id=?;") == 0) {
            sqlite3_progress_handler(db, 1, [](void* state) -> int {
                auto& fired = *static_cast<bool*>(state);
                if (fired) return 0;
                fired = true;
                return 1;
            }, &interrupted);
        }
    };
    pipeline.FinishReconciliationPass(volume);
    Fault::onStatement = {};
    sqlite3_progress_handler(Fault::capturedDb, 0, nullptr, nullptr);
    Check(interrupted, "actual reconciliation identity enumeration returns an error");
    Check(pipeline.IsReconciliationPassActive(volume), "failed identity enumeration is not successful reconciliation exhaustion");
    pipeline.FinishReconciliationPass(volume);
    const auto aggregate = pipeline.GetFolderAggregate(volume, {1, 0});
    Check(!pipeline.IsReconciliationPassActive(volume) && aggregate && aggregate->itemCount == 0,
          "retry after reconciliation read error converges without losing its obligation");
    pipeline.Close();
    Verify(path, 1);
}

int Child(const std::string& path, const std::wstring& eventName, int phase) {
    if (!Fault::Install()) return 11;
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
    if (!event) return 12;
    const auto park = [event]() { SetEvent(event); WaitForSingleObject(GetCurrentProcess(), INFINITE); };
    ffengine::IndexPipeline pipeline;
    Fault::captureNext = true;
    if (!pipeline.Open(path)) return 13;
    const auto volume = pipeline.ResolveVolume(VolumeKey{});
    pipeline.BeginReconciliationPass(volume);
    if (!pipeline.ApplyMftBatch(volume, {Mft(1)})) return 14;
    int commits = 0;
    int walWritesInChunk = 0;
    int checkpointWrites = 0;
    Fault::onCommit = [&]() {
        ++commits;
        walWritesInChunk = 0;
        if ((phase == 2 && commits == 1) || (phase == 5 && commits == 3)) park();
    };
    Fault::onWrite = [&](bool wal, bool database) {
        if (wal) ++walWritesInChunk;
        if (database) ++checkpointWrites;
        // Delegate earlier writes first: these are mid-write-sequence kills,
        // not only deaths before SQLite writes any uncommitted data.
        if ((phase == 1 && wal && commits == 0 && walWritesInChunk == 4) ||
            (phase == 3 && wal && commits == 1 && walWritesInChunk == 4) ||
            (phase == 4 && database && checkpointWrites == 2)) park();
    };
    // Force the production checkpoint threshold to be reached in this small
    // fixture without altering production constants: use long row names in
    // the seed and disable SQLite's own auto checkpoint on this connection.
    Sql(Fault::capturedDb, "PRAGMA cache_size=8; PRAGMA wal_autocheckpoint=0;");
    if (phase == 0) park();
    pipeline.FinishReconciliationPass(volume);
    Fault::onCommit = {};
    Fault::onWrite = {};
    CloseHandle(event);
    return 15; // A missed boundary must fail, not silently turn into a pass.
}
void TestCrash(const std::filesystem::path& dir) {
    const auto seed = dir / "crash-seed.db";
    {
        ffengine::IndexPipeline pipeline;
        Check(pipeline.Open(seed.string()), "crash seed opens");
        const auto volume = pipeline.ResolveVolume(VolumeKey{});
        std::vector<ffprotocol::MftRecordV1> rows{Mft(1)};
        for (uint64_t i = 2; i < 12002; ++i) {
            auto row = Mft(i);
            row.fileName += std::u16string(800, u'x');
            row.fixed.fileNameLengthChars = static_cast<uint16_t>(row.fileName.size());
            rows.push_back(std::move(row));
        }
        Check(pipeline.ApplyMftBatch(volume, rows), "12,000 long-name crash rows commit");
        pipeline.Close();
    }
    std::vector<wchar_t> exe(32768);
    GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    for (int phase = 0; phase < 6; ++phase) {
        const auto path = dir / ("crash-" + std::to_string(phase) + ".db");
        std::filesystem::copy_file(seed, path);
        const auto name = L"Local\\FastFilesStorageCrash-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(phase);
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        std::wstring command = L"\"" + std::wstring(exe.data()) + L"\" --child \"" + path.wstring() + L"\" \"" + name + L"\" " + std::to_wstring(phase);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const bool launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                             nullptr, nullptr, &startup, &process) != FALSE;
        Check(launched, "real crash child launches");
        if (!launched) { CloseHandle(event); continue; }
        const bool reached = WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
        Check(reached, "child reaches exact named crash boundary");
        Check(TerminateProcess(process.hProcess, 0xDEAD) != FALSE, "parent kills child (not graceful ExitProcess rollback)");
        Check(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0, "killed child exits within bound");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(event);
        const int64_t expected = phase <= 1 ? 12001 : (phase == 5 ? 1 : 7001);
        std::printf("CRASH phase=%d expected_entries=%lld boundary_reached=%d\n", phase,
                    static_cast<long long>(expected), reached ? 1 : 0);
        if (!reached) continue;
        Verify(path.string(), expected);
        sqlite3* identity = nullptr;
        Check(sqlite3_open(path.string().c_str(), &identity) == SQLITE_OK, "crash identity probe opens");
        Check(Scalar(identity, "SELECT count(*) FROM entries WHERE frn_low=1 AND frn_high=0 AND parent_frn_low=1 AND name='.' AND attributes=16;") == 1,
              "surviving root has exact identity and metadata");
        const auto unchangedName = "crash_removal.txt" + std::string(800, 'x');
        const auto unchangedSql = "SELECT count(*) FROM entries WHERE frn_low!=1 AND (frn_high!=0 OR parent_frn_low!=1 OR size_bytes!=0 OR attributes!=0 OR name!='" + unchangedName + "');";
        Check(Scalar(identity, unchangedSql.c_str()) == 0, "all unchanged crash rows retain original names and metadata");
        sqlite3_close(identity);
        ffengine::IndexPipeline restarted;
        Check(restarted.Open(path.string()), "killed database reopens through real pipeline");
        const auto volume = restarted.ResolveVolume(VolumeKey{});
        Check(!restarted.IsReconciliationPassActive(volume), "in-memory reconciliation obligation does not survive restart");
        // Production rescan path: a new complete scan reconstructs seen IDs;
        // never assume that the killed process's set was durable.
        restarted.BeginReconciliationPass(volume);
        Check(restarted.ApplyMftBatch(volume, {Mft(1)}), "restart full rescan observes surviving root");
        restarted.FinishReconciliationPass(volume);
        Check(!restarted.IsReconciliationPassActive(volume), "reconstructed pass converges after restart");
        restarted.Close();
        Verify(path.string(), 1);
        std::filesystem::remove(path);
        std::filesystem::remove(path.string() + "-wal");
        std::filesystem::remove(path.string() + "-shm");
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 5 && std::wstring(argv[1]) == L"--child") {
        return Child(std::filesystem::path(argv[2]).string(), argv[3], _wtoi(argv[4]));
    }
    const auto dir = std::filesystem::temp_directory_path() /
        ("ff-owned-storage-pressure-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    if (!std::filesystem::create_directory(dir)) return 2;
    std::printf("OWNED_FIXTURE_DIRECTORY=%s\n", dir.string().c_str());
    Check(Fault::Install(), "test forwarding VFS installs");
    const bool recoveryOnly = argc == 2 && std::wstring(argv[1]) == L"--recovery-only";
    // Explicitly outside ordinary CTest/continuation scope. Historical crash
    // code is retained, but is never dispatched by a default test invocation.
    const bool crashAcceptance = argc == 2 && std::wstring(argv[1]) == L"--crash-acceptance";
    if (!recoveryOnly && !crashAcceptance) {
        TestReaders(dir);
        TestFull(dir);
        TestRebuildFull(dir);
    }
    if (!crashAcceptance) TestCandidateRecoveryRegressions(dir);
    if (!crashAcceptance) TestReconciliationReadFailureRetainsObligation(dir);
    if (!recoveryOnly && !crashAcceptance) TestChunkTwoFull(dir);
    if (crashAcceptance) TestCrash(dir);
    Fault::Uninstall();
    std::filesystem::remove_all(dir); // Only the uniquely created, owned directory.
    std::printf("OWNED_FIXTURE_CLEANED=1 failures=%d\n", FailureCount());
    return FailureCount() == 0 ? 0 : 1;
}
