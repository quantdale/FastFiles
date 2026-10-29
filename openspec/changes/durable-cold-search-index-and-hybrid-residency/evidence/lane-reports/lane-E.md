Task: READ-ONLY research lane E (FastFiles repo; work ONLY read-only, no shell, no edits) — Migration and crash consistency. Repo: C:/Users/Michael Roy/Documents/FastFiles. Investigate: (1) schema v2 as implemented (src/indexstore/src/Store.cpp — tables, PRAGMA settings, version detection, open/reopen path); (2) schema v3 alternatives for a durable candidate index: additive versioned migration design (new table vs new DB file), transactional coupling to entries mutations (what transaction boundaries exist today — find BeginTransaction/COMMIT usage), consistency markers/epochs, rebuild-on-open invalidation, corruption detection (CHECK constraints? verification queries?), and rollback/downgrade semantics (must an older app fail closed on v3? document the distinction); (3) crash-interruption surface: map every mutation path that would touch both entries and the candidate index (initial creation, large migration, incremental insert, delete, rename, USN batch, reconciliation, bulk replacement, database reopen) and specify the failure-injection test for each (the existing fault-injection seams — find them, e.g. FF_INDEXSTORE_TESTING or failure hooks in tests/indexstore/); (4) migration measurement design at 2M/5M rows: time, disk peak, WAL peak, memory peak, interruption recovery, verification time. Deliver: findings with file:line, a recommended migration architecture (or a ranked pair), and the exact crash-test matrix.
ls: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore
ls: C:/Users/Michael Roy/Documents/FastFiles/tests/indexstore
CMakeLists.txt
include/
src/
bench_projection_memory.cpp
bench_rebuild_isolation.cpp
CMakeLists.txt
test_flat_hash_map.cpp
test_projection.cpp
test_store_read_session.cpp
test_store.cpp
ls: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/src
ls: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
NamePool.cpp
Projection.cpp
Store.cpp
ffindexstore/
ls: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
EntryRecord.h
FlatHashMap.h
Identity.h
NamePool.h
Projection.h
Store.h
Store.cpp:16: // task 1.4: PRAGMA user_version is the stored schema-version marker.
Store.cpp:22: constexpr int kCurrentSchemaVersion = 2;
Store.cpp:24: constexpr char kSchemaSql[] =
Store.cpp:25:     "CREATE TABLE IF NOT EXISTS volumes ("
Store.cpp:38:     "CREATE TABLE IF NOT EXISTS entries ("
Store.cpp:55:     "CREATE INDEX IF NOT EXISTS idx_entries_parent ON entries(volume_id, parent_frn_low, parent_frn_high);";
Store.cpp:314:     Statement stmt(db_, "PRAGMA integrity_check;");
Store.cpp:369:     if (!ExecSql(db_, "PRAGMA journal_mode=WAL;") || !ExecSql(db_, "PRAGMA synchronous=NORMAL;")
Store.cpp:370:         || !ExecSql(db_, "PRAGMA foreign_keys=ON;")) {
Store.cpp:375:     if (!ExecSql(db_, kSchemaSql)) {
Store.cpp:380:     Statement versionStmt(db_, "PRAGMA user_version;");
Store.cpp:386:         char pragma[64];
Store.cpp:387:         std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
Store.cpp:388:         if (!ExecSql(db_, pragma)) {
Store.cpp:402:         char pragma[64];
Store.cpp:403:         std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
Store.cpp:404:         if (!ExecSql(db_, pragma)) {
Store.cpp:408:     } else if (existingVersion != kCurrentSchemaVersion) {
Store.cpp:409:         // A newer-than-understood schema is refused rather than guessed at.
Store.cpp:457:     // (PRAGMA page_size) make the old approximation wrong by up to 8x. Query
Store.cpp:465:         if (sqlite3_prepare_v2(db_, "PRAGMA page_size;", -1, &stmt, nullptr) == SQLITE_OK) {
Store.cpp:1123:         !ExecSql(db_, "PRAGMA query_only=ON;")) {
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
#include "ffindexstore/Store.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>
#include "sqlite3.h"
namespace ffindexstore {
namespace {
// task 1.4: PRAGMA user_version is the stored schema-version marker.
// Version 2 (allocated-size-storage-accuracy D4) adds the nullable
// allocated_size_bytes column (NULL = unknown, never zero) via an in-place
// ALTER TABLE for version-1 databases; a fresh database is created at
// version 2 directly. A mismatched newer version is refused rather than
// guessed at.
constexpr int kCurrentSchemaVersion = 2;
constexpr char kSchemaSql[] =
    "CREATE TABLE IF NOT EXISTS volumes ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  volume_guid BLOB NOT NULL,"
    "  serial_number INTEGER NOT NULL,"
    "  available INTEGER NOT NULL DEFAULT 1,"
    "  last_seen_time INTEGER NOT NULL DEFAULT 0,"
    "  journal_id INTEGER,"
    "  resume_usn INTEGER NOT NULL DEFAULT 0,"
    "  scan_cursor BLOB,"
    "  scan_complete INTEGER NOT NULL DEFAULT 0,"
    "  last_reconciliation_time INTEGER NOT NULL DEFAULT 0,"
    "  UNIQUE(volume_guid, serial_number)"
    ");"
    "CREATE TABLE IF NOT EXISTS entries ("
    "  volume_id INTEGER NOT NULL REFERENCES volumes(id),"
    "  frn_low INTEGER NOT NULL,"
    "  frn_high INTEGER NOT NULL DEFAULT 0,"
    "  parent_frn_low INTEGER NOT NULL,"
    "  parent_frn_high INTEGER NOT NULL DEFAULT 0,"
    "  name TEXT NOT NULL,"
    "  size_bytes INTEGER NOT NULL DEFAULT 0,"
    // allocated-size-storage-accuracy D4: NULL == unknown. Zero is a valid
    // size and is stored as 0, never as NULL.
    "  allocated_size_bytes INTEGER,"
    "  creation_time INTEGER NOT NULL DEFAULT 0,"
    "  last_modified_time INTEGER NOT NULL DEFAULT 0,"
    "  last_access_time INTEGER NOT NULL DEFAULT 0,"
    "  attributes INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY (volume_id, frn_low, frn_high)"
    ") WITHOUT ROWID;"
    "CREATE INDEX IF NOT EXISTS idx_entries_parent ON entries(volume_id, parent_frn_low, parent_frn_high);";
// allocated-size-storage-accuracy D4: version 1 -> 2 in-place migration.
// ALTER TABLE ADD COLUMN is metadata-only in SQLite (no table rewrite, no
// row churn); pre-existing rows read back as NULL == unknown until
// re-observed by a scan or reconciliation sweep. Runs inside the same
// WAL/integrity-checked open path.
constexpr char kMigrateV1ToV2Sql[] =
    "ALTER TABLE entries ADD COLUMN allocated_size_bytes INTEGER;";
uint64_t NowEpochSeconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
uint64_t DatabaseFileSize(const std::string& path) {
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > std::numeric_limits<uint64_t>::max()) return 0;
    return static_cast<uint64_t>(size);
}
// Manual, dependency-free UTF-16 <-> UTF-8 conversion (surrogate-pair
// aware) so this library has no platform-specific text-conversion
// dependency (e.g. Win32's WideCharToMultiByte) and stays independently
// testable off-Windows, consistent with ffprotocol's own portable parsing
// layer (Records.h/SnapshotFormat.h use std::u16string/std::wstring
// directly rather than calling into Win32).
std::string Utf16ToUtf8(const std::u16string& in) {
    // Fast path: pure ASCII (common for file names) avoids per-codepoint
    // branching and surrogate checks; ~80% of typical NTFS names are ASCII.
    bool isAscii = true;
    for (char16_t ch : in) {
        if (ch > 0x7F) { isAscii = false; break; }
    }
    if (isAscii) {
        std::string out;
        out.resize(in.size());
        for (size_t idx = 0; idx < in.size(); ++idx) out[idx] = static_cast<char>(in[idx]);
        return out;
    }
    std::string out;
    out.reserve(in.size() * 3 / 2 + 4);
    size_t i = 0;
    while (i < in.size()) {
        char32_t codepoint = in[i];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 1 < in.size()) {
            const char16_t low = in[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        ++i;
        if (codepoint <= 0x7F) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }
    return out;
}
std::u16string Utf8ToUtf16(const char* data, int lengthBytes) {
    if (data == nullptr || lengthBytes <= 0) {
        return {};
    }
    const size_t size = static_cast<size_t>(lengthBytes);
    const auto bytes = reinterpret_cast<const unsigned char*>(data);
    // Fast path: pure ASCII bytes map 1:1 to UTF-16 code units.
    bool isAscii = true;
    for (size_t a = 0; a < size; ++a) if (bytes[a] >= 0x80) { isAscii = false; break; }
    if (isAscii) {
        std::u16string out;
        out.resize(size);
        for (size_t idx = 0; idx < size; ++idx) out[idx] = static_cast<char16_t>(bytes[idx]);
        return out;
    }
    std::u16string out;
    out.reserve(size);
    size_t i = 0;
    while (i < size) {
        char32_t codepoint = 0;
        size_t extra = 0;
        const unsigned char b0 = bytes[i];
        if (b0 < 0x80) {
            codepoint = b0;
        } else if ((b0 & 0xE0) == 0xC0) {
            codepoint = b0 & 0x1F;
            extra = 1;
        } else if ((b0 & 0xF0) == 0xE0) {
            codepoint = b0 & 0x0F;
            extra = 2;
        } else if ((b0 & 0xF8) == 0xF0) {
            codepoint = b0 & 0x07;
            extra = 3;
        } else {
            ++i;
            continue; // invalid leading byte -- skip defensively
        }
        if (i + extra >= size) {
            break; // truncated sequence
        }
        bool valid = true;
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char cont = bytes[i + k];
            if ((cont & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | (cont & 0x3F);
        }
        i += extra + 1;
        if (!valid) {
            continue;
        }
        if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(codepoint));
        } else {
            codepoint -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (codepoint >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (codepoint & 0x3FF)));
        }
    }
    return out;
}
class Statement {
public:
    Statement(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr); }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    operator sqlite3_stmt*() const noexcept { return stmt_; }
    bool Valid() const noexcept { return stmt_ != nullptr; }
private:
    sqlite3_stmt* stmt_ = nullptr;
};
bool ExecSql(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}
FileId ReadFileId(sqlite3_stmt* stmt, int lowCol, int highCol) {
    FileId id;
    id.low = static_cast<uint64_t>(sqlite3_column_int64(stmt, lowCol));
    id.high = static_cast<uint64_t>(sqlite3_column_int64(stmt, highCol));
    return id;
}
EntryRecord ReadEntryRecord(sqlite3_stmt* stmt) {
    EntryRecord record;
    record.id = ReadFileId(stmt, 0, 1);
    record.parentId = ReadFileId(stmt, 2, 3);
    const unsigned char* nameText = sqlite3_column_text(stmt, 4);
    const int nameBytes = sqlite3_column_bytes(stmt, 4);
    record.name = Utf8ToUtf16(reinterpret_cast<const char*>(nameText), nameBytes);
    record.sizeBytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
    record.allocatedSizeKnown = sqlite3_column_type(stmt, 6) != SQLITE_NULL;
    record.allocatedSizeBytes = record.allocatedSizeKnown
        ? static_cast<uint64_t>(sqlite3_column_int64(stmt, 6)) : 0;
    record.creationTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 7));
    record.lastModifiedTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 8));
    record.lastAccessTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 9));
    record.attributes = static_cast<uint32_t>(sqlite3_column_int64(stmt, 10));
    return record;
}
Store::FolderAggregate QueryFolderAggregate(sqlite3* db, VolumeRowId volumeRowId,
                                             FileId parentFrn, bool* outSuccess) {
    Store::FolderAggregate result{};
    if (outSuccess != nullptr) *outSuccess = false;
    if (db == nullptr) return result;
    Statement exists(db, "SELECT 1 FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
    if (!exists.Valid()) return result;
    sqlite3_bind_int64(exists, 1, static_cast<sqlite3_int64>(volumeRowId));
    sqlite3_bind_int64(exists, 2, static_cast<sqlite3_int64>(parentFrn.low));
    sqlite3_bind_int64(exists, 3, static_cast<sqlite3_int64>(parentFrn.high));
    const int existsResult = sqlite3_step(exists);
    if (existsResult == SQLITE_DONE) {
        // An unknown root is a valid zero aggregate, not a failed read.
        if (outSuccess != nullptr) *outSuccess = true;
        return result;
    }
    if (existsResult != SQLITE_ROW) return result;
    Statement stmt(db,
        "WITH RECURSIVE subtree AS ("
        "  SELECT volume_id, frn_low, frn_high, size_bytes, allocated_size_bytes, 1 AS depth"
        "  FROM entries"
        "  WHERE volume_id=? AND frn_low=? AND frn_high=?"
        "  UNION ALL"
        "  SELECT e.volume_id, e.frn_low, e.frn_high, e.size_bytes, e.allocated_size_bytes, s.depth + 1"
        "  FROM entries e"
        "  JOIN subtree s ON e.volume_id=s.volume_id"
        "    AND e.parent_frn_low=s.frn_low AND e.parent_frn_high=s.frn_high"
        "    AND NOT (e.frn_low = e.parent_frn_low AND e.frn_high = e.parent_frn_high)"
        ")"
        "SELECT COUNT(*), COALESCE(SUM(size_bytes), 0), COALESCE(SUM(allocated_size_bytes), 0), "
        "       COUNT(allocated_size_bytes), COUNT(*)"
        " FROM subtree"
        " WHERE depth > 1;");
    if (!stmt.Valid()) return result;
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(parentFrn.low));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(parentFrn.high));
    const int stepResult = sqlite3_step(stmt);
    if (stepResult == SQLITE_ROW) {
        result.itemCount = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        result.totalSizeBytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
        result.totalAllocatedSizeBytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
        result.allocatedBytesKnownCount = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
        result.allocatedBytesTotalCount = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
        if (outSuccess != nullptr) *outSuccess = true;
    }
    return result;
}
} // namespace
Store::~Store() {
    Close();
}
Store::IntegrityVerdict Store::ClassifyIntegrityStep(int stepCode) noexcept {
    // The primary result code lives in the low 8 bits; extended codes carry
    // sub-reasons (e.g. SQLITE_IOERR_READ == 266) that must classify with
    // their primary family. Only SQLite's own corruption verdicts qualify;
    // everything else -- BUSY, LOCKED, NOMEM, READONLY, INTERRUPT, FULL,
    // IOERR, and any unrecognized code -- means the check produced no
    // corruption verdict, which is grounds for retry, never deletion.
    switch (stepCode & 0xff) {
        case SQLITE_CORRUPT: // 11 -- verified corrupt
        case SQLITE_NOTADB:  // 26 -- not a database file at all
            return IntegrityVerdict::Corrupt;
        default:
            return IntegrityVerdict::Inconclusive;
    }
}
Store::IntegrityVerdict Store::RunIntegrityCheckDetailed() {
    if (db_ == nullptr) {
        return IntegrityVerdict::Inconclusive;
    }
    Statement stmt(db_, "PRAGMA integrity_check;");
    if (!stmt.Valid()) {
        // Preparation itself carries SQLite's verdict: a corrupted page-1
        // b-tree header or a NOTADB file fails the prepare of any statement,
        // so classify the connection's real result code -- losing it here
        // would turn verified corruption into "unproven".
        return ClassifyIntegrityStep(sqlite3_extended_errcode(db_));
    }
    const int step = sqlite3_step(stmt);
    if (step == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text == nullptr) {
            return IntegrityVerdict::Inconclusive; // unusable report row -- unproven, not corrupt
        }
        return std::strcmp(reinterpret_cast<const char*>(text), "ok") == 0 ? IntegrityVerdict::Ok
                                                                           : IntegrityVerdict::Corrupt;
    }
    return ClassifyIntegrityStep(step);
}
bool Store::RunIntegrityCheck() {
    return RunIntegrityCheckDetailed() == IntegrityVerdict::Ok;
}
bool Store::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed) {
    if (outIntegrityFailed != nullptr) {
        *outIntegrityFailed = false;
    }
    Close();
    if (sqlite3_open_v2(dbPathUtf8.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        Close();
        return false;
    }
    // task 1.7: verify before trusting anything already on disk. A brand
    // new, empty file passes trivially, so this is safe to run
    // unconditionally rather than special-casing first-run.
    // index-durability-false-success-closure D6 (PA-006): only a Corrupt
    // verdict licenses the caller's delete-and-rescan fallback; an
    // Inconclusive verdict fails the open with outIntegrityFailed left
    // false so a transient lock/IO condition can never destroy the index.
    const IntegrityVerdict verdict = RunIntegrityCheckDetailed();
    if (verdict != IntegrityVerdict::Ok) {
        if (verdict == IntegrityVerdict::Corrupt && outIntegrityFailed != nullptr) {
            *outIntegrityFailed = true;
        }
        Close();
        return false;
    }
    // design.md D1/"Risks": WAL for concurrent-read-during-write (task
    // 1.5), synchronous=NORMAL under WAL is safe for this workload (a
    // NORMAL-mode WAL commit followed by a crash loses at most the most
    // recent transactions, which resumable scan/journal already tolerate).
    if (!ExecSql(db_, "PRAGMA journal_mode=WAL;") || !ExecSql(db_, "PRAGMA synchronous=NORMAL;")
        || !ExecSql(db_, "PRAGMA foreign_keys=ON;")) {
        Close();
        return false;
    }
    if (!ExecSql(db_, kSchemaSql)) {
        Close();
        return false;
    }
    Statement versionStmt(db_, "PRAGMA user_version;");
    int existingVersion = 0;
    if (versionStmt.Valid() && sqlite3_step(versionStmt) == SQLITE_ROW) {
        existingVersion = sqlite3_column_int(versionStmt, 0);
    }
    if (existingVersion == 0) {
        char pragma[64];
        std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
        if (!ExecSql(db_, pragma)) {
            Close();
            return false;
        }
    } else if (existingVersion == 1) {
        // allocated-size-storage-accuracy D4: in-place migration of an
        // existing version-1 database. Runs before the version marker is
        // bumped; if the ALTER fails the database is left untouched and the
        // open fails closed (the existing integrity-check/rebuild fallback
        // in Main.cpp then applies).
        if (!ExecSql(db_, kMigrateV1ToV2Sql)) {
            Close();
            return false;
        }
        char pragma[64];
        std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
        if (!ExecSql(db_, pragma)) {
            Close();
            return false;
        }
    } else if (existingVersion != kCurrentSchemaVersion) {
        // A newer-than-understood schema is refused rather than guessed at.
        Close();
        return false;
    }
    dbPathUtf8_ = dbPathUtf8;
    lastVacuumTimeSeconds_ = NowEpochSeconds();
    lastVacuumSizeBytes_ = DatabaseFileSize(dbPathUtf8_);
    cachedPageSize_ = 0;
    return true;
}
void Store::Close() {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    dbPathUtf8_.clear();
    lastVacuumTimeSeconds_ = 0;
    lastVacuumSizeBytes_ = 0;
    cachedPageSize_ = 0;
}
std::unique_ptr<StoreReadSession> Store::OpenReadSession(
    const StoreReadSessionOptions& options) const {
    if (db_ == nullptr || dbPathUtf8_.empty()) return nullptr;
    std::unique_ptr<StoreReadSession> session(new StoreReadSession(dbPathUtf8_, options));
    if (!session->Open()) return nullptr;
    return session;
}
bool Store::CheckpointPassive() {
    if (db_ == nullptr) {
        return false;
    }
    return sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr) == SQLITE_OK;
}
bool Store::CheckpointIfWalExceeds(uint64_t thresholdBytes) {
    if (db_ == nullptr) {
        return false;
    }
    int frameCount = 0;
    if (sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, &frameCount, nullptr) != SQLITE_OK) {
        return false;
    }
    // Use the real page size rather than a hardcoded 4096 — custom page sizes
    // (PRAGMA page_size) make the old approximation wrong by up to 8x. Query
    // the handle's actual page size once and cache it; fall back to 4096 only
    // if the query fails. Cache avoids a prepare/step/finalize per maintenance
    // poll (every 500ms during scan bursts).
    int pageSize = cachedPageSize_;
    if (pageSize <= 0) {
        pageSize = 4096;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "PRAGMA page_size;", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const int p = sqlite3_column_int(stmt, 0);
                if (p > 0) pageSize = p;
            }
            sqlite3_finalize(stmt);
        }
        cachedPageSize_ = pageSize;
    }
    const uint64_t approxWalBytes = static_cast<uint64_t>(frameCount) * static_cast<uint64_t>(pageSize);
    if (approxWalBytes > thresholdBytes) {
        return sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_RESTART, nullptr, nullptr) == SQLITE_OK;
    }
    return true;
}
bool Store::Analyze() {
    return db_ != nullptr && ExecSql(db_, "ANALYZE;");
}
bool Store::VacuumIfDue(bool userIdle, uint64_t nowEpochSeconds, uint64_t intervalHours,
                        uint64_t sizeGrowthPercent) {
    if (db_ == nullptr || !userIdle || dbPathUtf8_.empty()) return false;
    constexpr uint64_t kSecondsPerHour = 60 * 60;
    const uint64_t intervalSeconds = intervalHours > std::numeric_limits<uint64_t>::max() / kSecondsPerHour
        ? std::numeric_limits<uint64_t>::max()
        : intervalHours * kSecondsPerHour;
    if (nowEpochSeconds < lastVacuumTimeSeconds_ ||
        nowEpochSeconds - lastVacuumTimeSeconds_ < intervalSeconds) {
        return false;
    }
    const uint64_t currentSize = DatabaseFileSize(dbPathUtf8_);
    if (currentSize <= lastVacuumSizeBytes_) return false;
    const uint64_t growth = currentSize - lastVacuumSizeBytes_;
    if (sizeGrowthPercent != 0 &&
[797 more lines in file. Use offset=501 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore
grep: C:/Users/Michael Roy/Documents/FastFiles
src/NamePool.cpp:52:     arena_.insert(arena_.end(), name.begin(), name.end());
src/Projection.cpp:25:     return std::all_of(name.begin(), name.end(), [](const char16_t ch) { return ch <= 0x7f; });
src/Projection.cpp:107:     std::sort(result.begin(), result.end());
src/Projection.cpp:108:     result.erase(std::unique(result.begin(), result.end()), result.end());
src/Projection.cpp:113:     const auto it = std::lower_bound(values.begin(), values.end(), value);
src/Projection.cpp:118:     const auto it = std::lower_bound(values.begin(), values.end(), value);
src/Projection.cpp:339:         std::fill(lastAccessTimes_.begin(), lastAccessTimes_.end(), 0);
src/Projection.cpp:412:     for (auto it = removalOrder.rbegin(); it != removalOrder.rend(); ++it) {
src/Projection.cpp:654:             std::sort(posting.begin(), posting.end());
src/Projection.cpp:655:             posting.erase(std::unique(posting.begin(), posting.end()), posting.end());
src/Projection.cpp:657:         std::sort(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end());
src/Projection.cpp:659:             std::unique(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end()),
src/Projection.cpp:912:       iterator_(dense_ ? EntryMap::Iterator{} : projection.idToIndex_.begin()),
src/Projection.cpp:943: ProjectionEntryCursor Projection::BeginEntryCursor() const {
src/Projection.cpp:973:             std::sort(postings.begin(), postings.end(), [](const auto* left, const auto* right) {
src/Projection.cpp:983:                 std::set_intersection(result.begin(), result.end(), postings[index]->begin(), postings[index]->end(),
src/Projection.cpp:1020:             std::set_union(result.begin(), result.end(), filenameExceptionalEntries_.begin(),
src/Projection.cpp:1128:     for (auto it = partsReversed.rbegin(); it != partsReversed.rend(); ++it) {
src/Projection.cpp:1137:         result.path.append(name.begin(), name.end());
src/Store.cpp:367:     // NORMAL-mode WAL commit followed by a crash loses at most the most
src/Store.cpp:368:     // recent transactions, which resumable scan/journal already tolerate).
src/Store.cpp:717:     if (!ExecSql(db_, "BEGIN IMMEDIATE;")) {
src/Store.cpp:724:         // same write transaction prevents a reconnect racing the deletion.
src/Store.cpp:733:         ExecSql(db_, "ROLLBACK;");
src/Store.cpp:753:         if (!ExecSql(db_, "COMMIT;")) {
src/Store.cpp:754:             ExecSql(db_, "ROLLBACK;");
src/Store.cpp:758:         ExecSql(db_, "ROLLBACK;");
src/Store.cpp:771:     if (!ExecSql(db_, "BEGIN IMMEDIATE;")) {
src/Store.cpp:792:         ExecSql(db_, "ROLLBACK;");
src/Store.cpp:863:         ExecSql(db_, "ROLLBACK;");
src/Store.cpp:866:     // The commit result is part of the operation's contract. Returning true
src/Store.cpp:867:     // after a failed COMMIT would let IndexPipeline apply changes to the
src/Store.cpp:869:     if (!ExecSql(db_, "COMMIT;")) {
src/Store.cpp:870:         ExecSql(db_, "ROLLBACK;");
src/Store.cpp:1091:       options_(std::move(other.options_)), transactionActive_(other.transactionActive_),
src/Store.cpp:1094:     other.transactionActive_ = false;
src/Store.cpp:1104:     transactionActive_ = other.transactionActive_;
src/Store.cpp:1107:     other.transactionActive_ = false;
src/Store.cpp:1128:     if (ShouldFail(ReadSessionFaultPoint::BeginSnapshot) || !ExecSql(db_, "BEGIN;")) {
src/Store.cpp:1133:     transactionActive_ = true;
src/Store.cpp:1134:     // Force SQLite to establish the read transaction's snapshot before the
src/Store.cpp:1135:     // caller releases the pipeline mutex. A deferred BEGIN otherwise waits
src/Store.cpp:1136:     // until the first real query, allowing a writer commit between reader
src/Store.cpp:1150:         if (transactionActive_) {
src/Store.cpp:1151:             ExecSql(db_, "ROLLBACK;");
src/Store.cpp:1156:     transactionActive_ = false;
include/ffindexstore/Store.h:22:     BeginSnapshot,
include/ffindexstore/Store.h:91:     // SQLite connection with its own WAL read transaction. The returned
include/ffindexstore/Store.h:169:     // Applies the whole batch as one explicit transaction (task 1.5):
include/ffindexstore/Store.h:174:     // back transaction) on any failure, so the caller can retry the same
include/ffindexstore/Store.h:175:     // batch (task 3.4) without risking a partial commit.
include/ffindexstore/Store.h:237: // connection concurrently from a query worker. A session's transaction is
include/ffindexstore/Store.h:249:     bool IsOpen() const noexcept { return db_ != nullptr && transactionActive_; }
include/ffindexstore/Store.h:279:     bool transactionActive_ = false;
include/ffindexstore/Projection.h:84: // consumer of Store::ApplyBatch's committed data, applied strictly after
include/ffindexstore/Projection.h:85: // the corresponding commit -- see the `index-engine` ingestion-ordering
include/ffindexstore/Projection.h:325:     ProjectionEntryCursor BeginEntryCursor() const;
include/ffindexstore/FlatHashMap.h:149:     Iterator begin() const {
include/ffindexstore/FlatHashMap.h:373:                 values_.insert(values_.end(), values_.begin() + off, values_.begin() + off + cnt);
include/ffindexstore/FlatHashMap.h:482:                 values_.insert(values_.end(), old_values.begin() + off, old_values.begin() + off + cnt);
third_party/sqlite/sqlite3.c:22186: SQLITE_PRIVATE void sqlite3BeginTransaction(Parse*, int);
third_party/sqlite/sqlite3.c:22187: SQLITE_PRIVATE void sqlite3EndTransaction(Parse*,int);
third_party/sqlite/sqlite3.c:77565: static void btreeEndTransaction(Btree *p){
third_party/sqlite/sqlite3.c:77651:   btreeEndTransaction(p);
third_party/sqlite/sqlite3.c:77789:   btreeEndTransaction(p);
third_party/sqlite/sqlite3.c:131787: SQLITE_PRIVATE void sqlite3BeginTransaction(Parse *pParse, int type){
third_party/sqlite/sqlite3.c:131823: SQLITE_PRIVATE void sqlite3EndTransaction(Parse *pParse, int eType){
third_party/sqlite/sqlite3.c:183666: {sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy144);}
third_party/sqlite/sqlite3.c:183679: {sqlite3EndTransaction(pParse,yymsp[-1].major);}
third_party/sqlite/sqlite3.c:221734: static int rtreeBeginTransaction(sqlite3_vtab *pVtab){
third_party/sqlite/sqlite3.c:221745: static int rtreeEndTransaction(sqlite3_vtab *pVtab){
third_party/sqlite/sqlite3.c:221752:   return rtreeEndTransaction(pVtab);
third_party/sqlite/sqlite3.c:221869:   rtreeBeginTransaction,      /* xBegin - begin transaction */
third_party/sqlite/sqlite3.c:221870:   rtreeEndTransaction,        /* xSync - sync transaction */
third_party/sqlite/sqlite3.c:221871:   rtreeEndTransaction,        /* xCommit - commit transaction */
third_party/sqlite/sqlite3.c:224575:   rtreeBeginTransaction,      /* xBegin - begin transaction */
third_party/sqlite/sqlite3.c:224576:   rtreeEndTransaction,        /* xSync - sync transaction */
third_party/sqlite/sqlite3.c:224577:   rtreeEndTransaction,        /* xCommit - commit transaction */
third_party/sqlite/sqlite3.c:224578:   rtreeEndTransaction,        /* xRollback - rollback transaction */
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
        (lastVacuumSizeBytes_ > std::numeric_limits<uint64_t>::max() / sizeGrowthPercent ||
         growth <= (lastVacuumSizeBytes_ * sizeGrowthPercent) / 100)) {
        return false;
    }
    if (!ExecSql(db_, "VACUUM;")) return false;
    lastVacuumTimeSeconds_ = nowEpochSeconds;
    lastVacuumSizeBytes_ = DatabaseFileSize(dbPathUtf8_);
    return true;
}
std::optional<VolumeRowId> Store::GetOrCreateVolume(const VolumeKey& key, bool* outCreated) {
    if (outCreated != nullptr) {
        *outCreated = false;
    }
    if (db_ == nullptr) {
        return std::nullopt;
    }
    {
        Statement select(db_, "SELECT id FROM volumes WHERE volume_guid=? AND serial_number=?;");
        if (!select.Valid()) {
            return std::nullopt;
        }
        sqlite3_bind_blob(select, 1, key.volumeGuid.data(), static_cast<int>(key.volumeGuid.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(select, 2, static_cast<sqlite3_int64>(key.serialNumber));
        if (sqlite3_step(select) == SQLITE_ROW) {
            return static_cast<VolumeRowId>(sqlite3_column_int64(select, 0));
        }
    }
    Statement insert(db_, "INSERT INTO volumes(volume_guid, serial_number, available, last_seen_time) VALUES (?,?,1,0);");
    if (!insert.Valid()) {
        return std::nullopt;
    }
    sqlite3_bind_blob(insert, 1, key.volumeGuid.data(), static_cast<int>(key.volumeGuid.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(insert, 2, static_cast<sqlite3_int64>(key.serialNumber));
    if (sqlite3_step(insert) != SQLITE_DONE) {
        return std::nullopt;
    }
    if (outCreated != nullptr) {
        *outCreated = true;
    }
    return static_cast<VolumeRowId>(sqlite3_last_insert_rowid(db_));
}
namespace {
VolumeMetadata ReadVolumeMetadataRow(sqlite3_stmt* stmt) {
    VolumeMetadata meta;
    meta.rowId = static_cast<VolumeRowId>(sqlite3_column_int64(stmt, 0));
    const void* guidBlob = sqlite3_column_blob(stmt, 1);
    const int guidSize = sqlite3_column_bytes(stmt, 1);
    if (guidBlob != nullptr && guidSize == static_cast<int>(meta.key.volumeGuid.size())) {
        std::memcpy(meta.key.volumeGuid.data(), guidBlob, meta.key.volumeGuid.size());
    }
    meta.key.serialNumber = static_cast<uint32_t>(sqlite3_column_int64(stmt, 2));
    meta.available = sqlite3_column_int(stmt, 3) != 0;
    meta.lastSeenTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
        meta.journalId = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
    }
    meta.resumeUsn = static_cast<uint64_t>(sqlite3_column_int64(stmt, 6));
    const void* cursorBlob = sqlite3_column_blob(stmt, 7);
    const int cursorSize = sqlite3_column_bytes(stmt, 7);
    if (cursorBlob != nullptr && cursorSize > 0) {
        const auto* bytes = static_cast<const uint8_t*>(cursorBlob);
        meta.scanCursor.assign(bytes, bytes + cursorSize);
    }
    meta.scanComplete = sqlite3_column_int(stmt, 8) != 0;
    meta.lastReconciliationTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 9));
    return meta;
}
constexpr char kVolumeColumns[] =
    "id, volume_guid, serial_number, available, last_seen_time, journal_id, resume_usn, scan_cursor, "
    "scan_complete, last_reconciliation_time";
} // namespace
std::optional<VolumeMetadata> Store::GetVolumeMetadata(VolumeRowId volumeRowId) {
    if (db_ == nullptr) {
        return std::nullopt;
    }
    std::string sql = std::string("SELECT ") + kVolumeColumns + " FROM volumes WHERE id=?;";
    Statement stmt(db_, sql.c_str());
    if (!stmt.Valid()) {
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        return std::nullopt;
    }
    auto meta = ReadVolumeMetadataRow(stmt);
    meta.entryCount = CountEntries(volumeRowId);
    return meta;
}
std::vector<VolumeMetadata> Store::GetAllVolumes() {
    return GetAllVolumesImpl(true);
}
std::vector<VolumeMetadata> Store::GetAllVolumesWithoutEntryCounts() {
    return GetAllVolumesImpl(false);
}
std::vector<VolumeMetadata> Store::GetAllVolumesImpl(const bool includeEntryCounts) {
    std::vector<VolumeMetadata> result;
    if (db_ == nullptr) {
        return result;
    }
    std::string sql = std::string("SELECT ") + kVolumeColumns + " FROM volumes ORDER BY id;";
    Statement stmt(db_, sql.c_str());
    if (!stmt.Valid()) {
        return result;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto meta = ReadVolumeMetadataRow(stmt);
        if (includeEntryCounts) {
            meta.entryCount = CountEntries(meta.rowId);
        }
        result.push_back(std::move(meta));
    }
    return result;
}
bool Store::SetVolumeAvailable(VolumeRowId volumeRowId, bool available, uint64_t nowTimestamp,
                               bool* outVisibilityChanged) {
    if (outVisibilityChanged != nullptr) {
        *outVisibilityChanged = false;
    }
    if (db_ == nullptr) {
        return false;
    }
    {
        Statement current(db_, "SELECT available FROM volumes WHERE id=?;");
        if (!current.Valid()) {
            return false;
        }
        sqlite3_bind_int64(current, 1, static_cast<sqlite3_int64>(volumeRowId));
        if (sqlite3_step(current) != SQLITE_ROW) {
            return false;
        }
        if (outVisibilityChanged != nullptr) {
            *outVisibilityChanged = (sqlite3_column_int(current, 0) != 0) != available;
        }
    }
    Statement stmt(db_, "UPDATE volumes SET available=?, last_seen_time=? WHERE id=?;");
    if (!stmt.Valid()) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, available ? 1 : 0);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(nowTimestamp));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(volumeRowId));
    return sqlite3_step(stmt) == SQLITE_DONE;
}
bool Store::SetJournalPosition(VolumeRowId volumeRowId, uint64_t journalId, uint64_t resumeUsn) {
    if (db_ == nullptr) {
        return false;
    }
    Statement stmt(db_, "UPDATE volumes SET journal_id=?, resume_usn=? WHERE id=?;");
    if (!stmt.Valid()) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(journalId));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(resumeUsn));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(volumeRowId));
    return sqlite3_step(stmt) == SQLITE_DONE;
}
bool Store::SetScanCursor(VolumeRowId volumeRowId, const std::vector<uint8_t>& cursor) {
    if (db_ == nullptr) {
        return false;
    }
    Statement stmt(db_, "UPDATE volumes SET scan_cursor=? WHERE id=?;");
    if (!stmt.Valid()) {
        return false;
    }
    if (cursor.empty()) {
        sqlite3_bind_null(stmt, 1);
    } else {
        sqlite3_bind_blob(stmt, 1, cursor.data(), static_cast<int>(cursor.size()), SQLITE_TRANSIENT);
    }
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(volumeRowId));
    return sqlite3_step(stmt) == SQLITE_DONE;
}
bool Store::MarkScanComplete(VolumeRowId volumeRowId) {
    if (db_ == nullptr) {
        return false;
    }
    Statement stmt(db_, "UPDATE volumes SET scan_complete=1, scan_cursor=NULL WHERE id=?;");
    if (!stmt.Valid()) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    return sqlite3_step(stmt) == SQLITE_DONE;
}
bool Store::SetLastReconciliationTime(VolumeRowId volumeRowId, uint64_t nowTimestamp) {
    if (db_ == nullptr) {
        return false;
    }
    Statement stmt(db_, "UPDATE volumes SET last_reconciliation_time=? WHERE id=?;");
    if (!stmt.Valid()) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(nowTimestamp));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(volumeRowId));
    return sqlite3_step(stmt) == SQLITE_DONE;
}
bool Store::ForgetVolume(VolumeRowId volumeRowId) {
    if (db_ == nullptr) {
        return false;
    }
    if (!ExecSql(db_, "BEGIN IMMEDIATE;")) {
        return false;
    }
    bool ok = true;
    {
        // A forget is deliberately narrower than a generic delete: only a
        // row already marked unavailable is eligible. Checking inside the
        // same write transaction prevents a reconnect racing the deletion.
        Statement eligibility(db_, "SELECT available FROM volumes WHERE id=?;");
        ok = eligibility.Valid();
        if (ok) {
            sqlite3_bind_int64(eligibility, 1, static_cast<sqlite3_int64>(volumeRowId));
            ok = sqlite3_step(eligibility) == SQLITE_ROW && sqlite3_column_int(eligibility, 0) == 0;
        }
    }
    if (!ok) {
        ExecSql(db_, "ROLLBACK;");
        return false;
    }
    {
        Statement deleteEntries(db_, "DELETE FROM entries WHERE volume_id=?;");
        ok = deleteEntries.Valid();
        if (ok) {
            sqlite3_bind_int64(deleteEntries, 1, static_cast<sqlite3_int64>(volumeRowId));
            ok = sqlite3_step(deleteEntries) == SQLITE_DONE;
        }
    }
    if (ok) {
        Statement deleteVolume(db_, "DELETE FROM volumes WHERE id=?;");
        ok = deleteVolume.Valid();
        if (ok) {
            sqlite3_bind_int64(deleteVolume, 1, static_cast<sqlite3_int64>(volumeRowId));
            ok = sqlite3_step(deleteVolume) == SQLITE_DONE;
        }
    }
    if (ok) {
        if (!ExecSql(db_, "COMMIT;")) {
            ExecSql(db_, "ROLLBACK;");
            ok = false;
        }
    } else {
        ExecSql(db_, "ROLLBACK;");
    }
    return ok;
}
bool Store::ApplyBatch(VolumeRowId volumeRowId, const std::vector<EntryChange>& changes) {
    if (db_ == nullptr) {
        return false;
    }
    if (changes.empty()) {
        return true;
    }
    if (!ExecSql(db_, "BEGIN IMMEDIATE;")) {
        return false;
    }
    Statement upsert(db_,
        "INSERT INTO entries(volume_id, frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, "
        "allocated_size_bytes, creation_time, last_modified_time, last_access_time, attributes) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(volume_id, frn_low, frn_high) DO UPDATE SET "
        "parent_frn_low=CASE WHEN (? & 1) != 0 THEN excluded.parent_frn_low ELSE entries.parent_frn_low END, "
        "parent_frn_high=CASE WHEN (? & 1) != 0 THEN excluded.parent_frn_high ELSE entries.parent_frn_high END, "
        "name=CASE WHEN (? & 2) != 0 THEN excluded.name ELSE entries.name END, "
        "size_bytes=CASE WHEN (? & 4) != 0 THEN excluded.size_bytes ELSE entries.size_bytes END, "
        "allocated_size_bytes=CASE WHEN (? & 128) != 0 THEN excluded.allocated_size_bytes "
        "  ELSE entries.allocated_size_bytes END, "
        "creation_time=CASE WHEN (? & 8) != 0 THEN excluded.creation_time ELSE entries.creation_time END, "
        "last_modified_time=CASE WHEN (? & 16) != 0 THEN excluded.last_modified_time ELSE entries.last_modified_time END, "
        "last_access_time=CASE WHEN (? & 32) != 0 THEN excluded.last_access_time ELSE entries.last_access_time END, "
        "attributes=CASE WHEN (? & 64) != 0 THEN excluded.attributes ELSE entries.attributes END;");
    Statement remove(db_, "DELETE FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
    if (!upsert.Valid() || !remove.Valid()) {
        ExecSql(db_, "ROLLBACK;");
        return false;
    }
    bool ok = true;
    for (const auto& change : changes) {
        if (change.kind == EntryChangeKind::Upsert) {
            const auto& record = change.record;
            const std::string nameUtf8 = Utf16ToUtf8(record.name);
            // large-index-lifecycle-and-search-scalability review-lane C
            // Finding 4: on the INSERT arm the field mask must zero absent
            // fields, matching Projection::UpsertPartial's missing-entry
            // contract ("created with zero/default values only for fields
            // that were not present"). The UPDATE arm already guards via the
            // CASE expressions; before this, a partial upsert of a row that
            // did not yet exist persisted the wire values for absent fields
            // and hot vs cold planes reported different metadata.
            const EntryFieldMask fields = change.fields;
            const bool hasParent = HasEntryField(fields, kEntryFieldParentId);
            const bool hasName = HasEntryField(fields, kEntryFieldName);
            const bool hasSize = HasEntryField(fields, kEntryFieldSizeBytes);
            const bool hasAllocated = HasEntryField(fields, kEntryFieldAllocatedSizeBytes);
            const bool hasCreation = HasEntryField(fields, kEntryFieldCreationTime);
            const bool hasModified = HasEntryField(fields, kEntryFieldLastModifiedTime);
            const bool hasAccess = HasEntryField(fields, kEntryFieldLastAccessTime);
            const bool hasAttributes = HasEntryField(fields, kEntryFieldAttributes);
            sqlite3_reset(upsert);
            sqlite3_bind_int64(upsert, 1, static_cast<sqlite3_int64>(volumeRowId));
            sqlite3_bind_int64(upsert, 2, static_cast<sqlite3_int64>(record.id.low));
            sqlite3_bind_int64(upsert, 3, static_cast<sqlite3_int64>(record.id.high));
            sqlite3_bind_int64(upsert, 4,
                static_cast<sqlite3_int64>(hasParent ? record.parentId.low : 0));
            sqlite3_bind_int64(upsert, 5,
                static_cast<sqlite3_int64>(hasParent ? record.parentId.high : 0));
            sqlite3_bind_text(upsert, 6,
                hasName ? nameUtf8.data() : "", hasName ? static_cast<int>(nameUtf8.size()) : 0,
                SQLITE_TRANSIENT);
            sqlite3_bind_int64(upsert, 7,
                static_cast<sqlite3_int64>(hasSize ? record.sizeBytes : 0));
            if (hasAllocated && record.allocatedSizeKnown) {
                sqlite3_bind_int64(upsert, 8, static_cast<sqlite3_int64>(record.allocatedSizeBytes));
            } else {
                // Unknown is NULL on disk -- a genuine zero is stored as 0.
                sqlite3_bind_null(upsert, 8);
            }
            sqlite3_bind_int64(upsert, 9,
                static_cast<sqlite3_int64>(hasCreation ? record.creationTime : 0));
            sqlite3_bind_int64(upsert, 10,
                static_cast<sqlite3_int64>(hasModified ? record.lastModifiedTime : 0));
            sqlite3_bind_int64(upsert, 11,
                static_cast<sqlite3_int64>(hasAccess ? record.lastAccessTime : 0));
            sqlite3_bind_int64(upsert, 12,
                static_cast<sqlite3_int64>(hasAttributes ? record.attributes : 0));
            const sqlite3_int64 mask = static_cast<sqlite3_int64>(change.fields);
            for (int parameter = 13; parameter <= 21; ++parameter) {
                sqlite3_bind_int64(upsert, parameter, mask);
            }
            ok = sqlite3_step(upsert) == SQLITE_DONE;
        } else {
            sqlite3_reset(remove);
            sqlite3_bind_int64(remove, 1, static_cast<sqlite3_int64>(volumeRowId));
            sqlite3_bind_int64(remove, 2, static_cast<sqlite3_int64>(change.record.id.low));
            sqlite3_bind_int64(remove, 3, static_cast<sqlite3_int64>(change.record.id.high));
            ok = sqlite3_step(remove) == SQLITE_DONE;
        }
        if (!ok) {
            break;
        }
    }
    if (!ok) {
        ExecSql(db_, "ROLLBACK;");
        return false;
    }
    // The commit result is part of the operation's contract. Returning true
    // after a failed COMMIT would let IndexPipeline apply changes to the
    // in-memory projection that never became durable.
    if (!ExecSql(db_, "COMMIT;")) {
        ExecSql(db_, "ROLLBACK;");
        return false;
    }
    return true;
}
bool Store::ForEachEntry(VolumeRowId volumeRowId, const EntryVisitor& visitor) {
    if (db_ == nullptr) {
        return false;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes FROM entries WHERE volume_id=?;");
    if (!stmt.Valid()) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    int stepResult;
    while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW) {
        EntryRecord record;
        record.id = ReadFileId(stmt, 0, 1);
        record.parentId = ReadFileId(stmt, 2, 3);
        const unsigned char* nameText = sqlite3_column_text(stmt, 4);
        const int nameBytes = sqlite3_column_bytes(stmt, 4);
        record.name = Utf8ToUtf16(reinterpret_cast<const char*>(nameText), nameBytes);
        record.sizeBytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
        // allocated-size-storage-accuracy D4: NULL on disk means unknown --
        // read back as the explicit known flag, never as a fabricated zero.
        record.allocatedSizeKnown = sqlite3_column_type(stmt, 6) != SQLITE_NULL;
        record.allocatedSizeBytes =
[397 more lines in file. Use offset=901 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
grep: C:/Users/Michael Roy/Documents/FastFiles
            record.allocatedSizeKnown ? static_cast<uint64_t>(sqlite3_column_int64(stmt, 6)) : 0;
        record.creationTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 7));
        record.lastModifiedTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 8));
        record.lastAccessTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 9));
        record.attributes = static_cast<uint32_t>(sqlite3_column_int64(stmt, 10));
        visitor(record);
    }
    return stepResult == SQLITE_DONE;
}
bool Store::ForEachEntryUntil(VolumeRowId volumeRowId, const EntryVisitorUntil& visitor) {
    if (db_ == nullptr) {
        return false;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes FROM entries WHERE volume_id=?;");
    if (!stmt.Valid()) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    int stepResult;
    while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW) {
        EntryRecord record;
        record.id = ReadFileId(stmt, 0, 1);
        record.parentId = ReadFileId(stmt, 2, 3);
        const unsigned char* nameText = sqlite3_column_text(stmt, 4);
        const int nameBytes = sqlite3_column_bytes(stmt, 4);
        record.name = Utf8ToUtf16(reinterpret_cast<const char*>(nameText), nameBytes);
        record.sizeBytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
        record.allocatedSizeKnown = sqlite3_column_type(stmt, 6) != SQLITE_NULL;
        record.allocatedSizeBytes =
            record.allocatedSizeKnown ? static_cast<uint64_t>(sqlite3_column_int64(stmt, 6)) : 0;
        record.creationTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 7));
        record.lastModifiedTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 8));
        record.lastAccessTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 9));
        record.attributes = static_cast<uint32_t>(sqlite3_column_int64(stmt, 10));
        if (!visitor(record)) {
            return true; // bounded early stop requested by the caller
        }
    }
    return stepResult == SQLITE_DONE;
}
// memory-bounded-indexed-scale 2.1: durable point lookup backing cold
// parent-chain walks. Same column order/semantics as ForEachEntry; a missing
// row is std::nullopt, never a fabricated record.
std::optional<EntryRecord> Store::FindEntry(VolumeRowId volumeRowId, FileId frn) {
    if (db_ == nullptr) {
        return std::nullopt;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes "
        "FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
    if (!stmt.Valid()) {
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(frn.low));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(frn.high));
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        return std::nullopt;
    }
    EntryRecord record;
    record.id = ReadFileId(stmt, 0, 1);
    record.parentId = ReadFileId(stmt, 2, 3);
    const unsigned char* nameText = sqlite3_column_text(stmt, 4);
    const int nameBytes = sqlite3_column_bytes(stmt, 4);
    record.name = Utf8ToUtf16(reinterpret_cast<const char*>(nameText), nameBytes);
    record.sizeBytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
    record.allocatedSizeKnown = sqlite3_column_type(stmt, 6) != SQLITE_NULL;
    record.allocatedSizeBytes =
        record.allocatedSizeKnown ? static_cast<uint64_t>(sqlite3_column_int64(stmt, 6)) : 0;
    record.creationTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 7));
    record.lastModifiedTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 8));
    record.lastAccessTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 9));
    record.attributes = static_cast<uint32_t>(sqlite3_column_int64(stmt, 10));
    return record;
}
std::vector<FileId> Store::ListEntryIds(VolumeRowId volumeRowId) {
    std::vector<FileId> result;
    if (db_ == nullptr) {
        return result;
    }
    Statement stmt(db_, "SELECT frn_low, frn_high FROM entries WHERE volume_id=?;");
    if (!stmt.Valid()) {
        return result;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back(ReadFileId(stmt, 0, 1));
    }
    return result;
}
uint64_t Store::CountEntries(VolumeRowId volumeRowId) {
    if (db_ == nullptr) {
        return 0;
    }
    Statement stmt(db_, "SELECT COUNT(*) FROM entries WHERE volume_id=?;");
    if (!stmt.Valid()) {
        return 0;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        return 0;
    }
    return static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
}
Store::FolderAggregate Store::GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn) {
    FolderAggregate result = {0, 0};
    if (db_ == nullptr) {
        return result;
    }
    // Verify the root exists first so we can return {0,0} for an unknown
    // folder. The caller wraps this in std::optional and treats {0,0}
    // as "not found" only when the root is absent.
    {
        Statement exists(db_, "SELECT 1 FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
        if (!exists.Valid()) {
            return result;
        }
        sqlite3_bind_int64(exists, 1, static_cast<sqlite3_int64>(volumeRowId));
        sqlite3_bind_int64(exists, 2, static_cast<sqlite3_int64>(parentFrn.low));
        sqlite3_bind_int64(exists, 3, static_cast<sqlite3_int64>(parentFrn.high));
        if (sqlite3_step(exists) != SQLITE_ROW) {
            return result;
        }
    }
    // Recursive CTE over the parent->child relationship. The root row is
    // the anchor; every subsequent generation joins on parent_frn == the
    // prior generation's frn. Self-parented rows (the NTFS volume root,
    // whose parent is itself) are excluded from the recursive term --
    // mirroring Projection::Upsert's root-sentinel guard -- so the root
    // never re-enters the walk as its own child and the recursion
    // terminates. We exclude the anchor itself from the aggregate by
    // counting only rows produced by the recursive term (depth > 1).
    // allocated-size-storage-accuracy D7: the allocated subtotal sums only
    // non-NULL rows and the known/total counts are the completeness pair;
    // SUM semantics follow the existing size_bytes handling (SQLite
    // promotes to floating point on 64-bit overflow, which is why the
    // store-side aggregate is the evicted-volume fallback while the
    // projection-side walk applies saturating checked arithmetic).
    Statement stmt(db_,
        "WITH RECURSIVE subtree AS ("
        "  SELECT volume_id, frn_low, frn_high, size_bytes, allocated_size_bytes, 1 AS depth"
        "  FROM entries"
        "  WHERE volume_id=? AND frn_low=? AND frn_high=?"
        "  UNION ALL"
        "  SELECT e.volume_id, e.frn_low, e.frn_high, e.size_bytes, e.allocated_size_bytes, s.depth + 1"
        "  FROM entries e"
        "  JOIN subtree s ON e.volume_id=s.volume_id"
        "    AND e.parent_frn_low=s.frn_low AND e.parent_frn_high=s.frn_high"
        "    AND NOT (e.frn_low = e.parent_frn_low AND e.frn_high = e.parent_frn_high)"
        ")"
        "SELECT COUNT(*), COALESCE(SUM(size_bytes), 0), COALESCE(SUM(allocated_size_bytes), 0), "
        "       COUNT(allocated_size_bytes), COUNT(*)"
        " FROM subtree"
        " WHERE depth > 1;");
    if (!stmt.Valid()) {
        return result;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(parentFrn.low));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(parentFrn.high));
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result.itemCount = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        result.totalSizeBytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
        result.totalAllocatedSizeBytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
        result.allocatedBytesKnownCount = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
        result.allocatedBytesTotalCount = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
    }
    return result;
}
StoreReadSession::StoreReadSession(std::string dbPathUtf8, StoreReadSessionOptions options)
    : dbPathUtf8_(std::move(dbPathUtf8)), options_(std::move(options)) {}
StoreReadSession::~StoreReadSession() {
    Close();
}
StoreReadSession::StoreReadSession(StoreReadSession&& other) noexcept
    : db_(other.db_), dbPathUtf8_(std::move(other.dbPathUtf8_)),
      options_(std::move(other.options_)), transactionActive_(other.transactionActive_),
      failed_(other.failed_) {
    other.db_ = nullptr;
    other.transactionActive_ = false;
    other.failed_ = false;
}
StoreReadSession& StoreReadSession::operator=(StoreReadSession&& other) noexcept {
    if (this == &other) return *this;
    Close();
    db_ = other.db_;
    dbPathUtf8_ = std::move(other.dbPathUtf8_);
    options_ = std::move(other.options_);
    transactionActive_ = other.transactionActive_;
    failed_ = other.failed_;
    other.db_ = nullptr;
    other.transactionActive_ = false;
    other.failed_ = false;
    return *this;
}
bool StoreReadSession::ShouldFail(const ReadSessionFaultPoint point) const {
    return options_.faultInjector && options_.faultInjector(point);
}
bool StoreReadSession::Open() {
    if (ShouldFail(ReadSessionFaultPoint::Open)) return false;
    if (sqlite3_open_v2(dbPathUtf8_.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        Close();
        return false;
    }
    if (sqlite3_busy_timeout(db_, 250) != SQLITE_OK || ShouldFail(ReadSessionFaultPoint::Busy) ||
        !ExecSql(db_, "PRAGMA query_only=ON;")) {
        failed_ = true;
        Close();
        return false;
    }
    if (ShouldFail(ReadSessionFaultPoint::BeginSnapshot) || !ExecSql(db_, "BEGIN;")) {
        failed_ = true;
        Close();
        return false;
    }
    transactionActive_ = true;
    // Force SQLite to establish the read transaction's snapshot before the
    // caller releases the pipeline mutex. A deferred BEGIN otherwise waits
    // until the first real query, allowing a writer commit between reader
    // acquisition and the first streamed row.
    Statement snapshot(db_, "SELECT id FROM volumes LIMIT 1;");
    const int snapshotStep = snapshot.Valid() ? sqlite3_step(snapshot) : SQLITE_ERROR;
    if (!snapshot.Valid() || (snapshotStep != SQLITE_ROW && snapshotStep != SQLITE_DONE)) {
        failed_ = true;
        Close();
        return false;
    }
    return true;
}
void StoreReadSession::Close() noexcept {
    if (db_ != nullptr) {
        if (transactionActive_) {
            ExecSql(db_, "ROLLBACK;");
        }
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    transactionActive_ = false;
    dbPathUtf8_.clear();
}
bool StoreReadSession::ForEachEntryUntil(VolumeRowId volumeRowId,
                                         const Store::EntryVisitorUntil& visitor) {
    if (!IsOpen() || !visitor) {
        if (!IsOpen()) failed_ = true;
        return false;
    }
    if (ShouldFail(ReadSessionFaultPoint::DatabaseClosed)) {
        failed_ = true;
        Close();
        return false;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes FROM entries WHERE volume_id=?;");
    if (!stmt.Valid()) {
        failed_ = true;
        return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    int stepResult;
    while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (ShouldFail(ReadSessionFaultPoint::Stream)) {
            failed_ = true;
            return false;
        }
        if (!visitor(ReadEntryRecord(stmt))) return true;
    }
    if (stepResult != SQLITE_DONE) failed_ = true;
    return stepResult == SQLITE_DONE;
}
namespace {
// large-index-lifecycle-and-search-scalability review-remediation 18.x:
// LIKE pattern for a literal needle: wraps in wildcards and escapes the LIKE
// metacharacters so a needle like "a\b" or "100%" can only ever OVER-match
// (conservative), never lose a row the needle literally appears in.
std::string LikePatternForLiteralNeedle(const std::string_view needle) {
    std::string pattern;
    pattern.reserve(needle.size() + 2 + needle.size() / 4);
    pattern.push_back('%');
    for (const char ch : needle) {
        if (ch == '\\' || ch == '%' || ch == '_') pattern.push_back('\\');
        pattern.push_back(ch);
    }
    pattern.push_back('%');
    return pattern;
}
} // namespace
bool StoreReadSession::ForEachEntryWithAsciiNameUntil(
    const VolumeRowId volumeRowId, const std::string_view asciiFoldedNeedle,
    const Store::EntryVisitorUntil& visitor) {
    // Conservative-superset prefilter; see the header contract. The needle
    // must be ASCII (the caller gates on it): SQLite LIKE folds ASCII case
    // only, which is exactly the fold the superset proof needs, and the GLOB
    // disjunct admits every name carrying non-ASCII bytes (whose canonical
    // match could pass through a non-ASCII unit under any host ordinal table).
    if (!IsOpen() || !visitor) {
        if (!IsOpen()) failed_ = true;
        return false;
    }
    if (ShouldFail(ReadSessionFaultPoint::DatabaseClosed)) {
        failed_ = true;
        Close();
        return false;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes FROM entries WHERE volume_id=? "
        "AND (name LIKE ? ESCAPE '\\' OR name GLOB '*[^ -~]*');");
    if (!stmt.Valid()) {
        failed_ = true;
        return false;
    }
    const std::string pattern = LikePatternForLiteralNeedle(asciiFoldedNeedle);
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    sqlite3_bind_text(stmt, 2, pattern.data(), static_cast<int>(pattern.size()), SQLITE_TRANSIENT);
    int stepResult;
    while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (ShouldFail(ReadSessionFaultPoint::Stream)) {
            failed_ = true;
            return false;
        }
        if (!visitor(ReadEntryRecord(stmt))) return true;
    }
    if (stepResult != SQLITE_DONE) failed_ = true;
    return stepResult == SQLITE_DONE;
}
std::optional<EntryRecord> StoreReadSession::FindEntry(VolumeRowId volumeRowId, FileId frn) {
    if (!IsOpen()) {
        failed_ = true;
        return std::nullopt;
    }
    if (ShouldFail(ReadSessionFaultPoint::FindEntry)) {
        failed_ = true;
        return std::nullopt;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes "
        "FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
    if (!stmt.Valid()) {
        failed_ = true;
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(frn.low));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(frn.high));
    const int stepResult = sqlite3_step(stmt);
    if (stepResult == SQLITE_DONE) return std::nullopt;
    if (stepResult != SQLITE_ROW) {
        failed_ = true;
        return std::nullopt;
    }
    return ReadEntryRecord(stmt);
}
Store::FolderAggregate StoreReadSession::GetFolderAggregate(VolumeRowId volumeRowId,
                                                             FileId parentFrn) {
    if (!IsOpen()) {
        failed_ = true;
        return {};
    }
    if (ShouldFail(ReadSessionFaultPoint::Aggregate)) {
        failed_ = true;
        return {};
    }
    bool success = false;
    const auto result = QueryFolderAggregate(db_, volumeRowId, parentFrn, &success);
    if (!success) failed_ = true;
    return result;
}
} // namespace ffindexstore
.agent/NEXT_CAMPAIGN_12H.md:210: ### P1-04 — fault injector is not actually named-boundary
.agent/NEXT_CAMPAIGN_12H.md:212: `SetSecondaryPruningFaultInjector` currently receives only volume + changes. It cannot explicitly distinguish MFT, USN, RebuildAll, and reconciliation. Existing MFT evidence cannot stand in for every caller.
.agent/NEXT_CAMPAIGN_12H.md:235: The rule-pruning design calls the fault injector test-only, but current `IndexPipeline` exposes it unconditionally. Encapsulate/guard it if feasible without distorting production or test architecture.
.agent/EXECUTION_PROMPT.md:59: 4. The fault injector has no explicit boundary identity for MFT vs USN vs rebuild vs reconciliation.
README.md:91: - **Active-path resilience** — degraded watcher setup/retirement, snapshot publication, engine reconnect, and asynchronous search supersession are covered by deterministic fault-injection and marked-fixture convergence tests; the privileged path remains fail-closed.
.agent/PLANNER_HANDOFF.md:33: - a fault injector without explicit named-boundary identity;
docs/agent/WORKSTATE.md:42: - **P2-A REPAIR (Option A, host-derived exceptional fallback):** `Projection.cpp` now classifies a row fold-exceptional iff it contains a unit that the HOST `CompareStringOrdinal` equates with any ASCII unit (memoized 65,536-slot probe, fail-closed on API error) or any surrogate unit (conservative); `FilenameCandidateIndices` merges ONLY those rows into the posting intersection (restores superset invariant for ANY host table; empty set on this host = zero cost, accelerator speedups preserved). R... [truncated]
docs/agent/WORKSTATE.md:63: - **Differential isolation matrix LOCATED the delta (ffindexstore_bench_rebuild_isolation, arms A-G, FF_INDEXSTORE_TESTING seam, commit `4901d68`):** at 200k — A fresh build 1,207.6 ms; B1 RemoveVolume (production) **12,983.4 ms**; B2 rebuild-after-removal 1,707.1 ms; C clear+build 2,283.9 ms; D build+swap 3,292.4 ms (double-buffer peak 216.5 MB private); **E1 removal with filename maintenance skipped 44.8 ms** (99.7% of removal cost = per-entry `EraseSorted` trigram posting erase); E2 rebuild-o... [truncated]
docs/agent/POST_CERTIFICATION_PRODUCT_AUDIT_2026-09-05.md:92: | PA-108 | Rule-pruning durability regression (2026-08-28 class) | All four secondary boundaries remain fail-closed with active fault-injection regression tests (`test_index_pipeline.cpp:1991/2066`). |
docs/agent/HANDOFF_12H_2026-08-27.md:16: - **Active → Partially Completed:** `rule-pruning-durability-and-reconciliation-failure-closure` → `active` with core D1-D4 implemented and 3 new tests (`TestRulePruningSecondaryFailureIsFailClosed`, `TestReconciliationRetentionOnSecondaryFailure` (relaxed), `TestRepeatedSecondaryFailure`) plus `SecondaryPruningFaultInjector`. Tasks 1.1,1.2,2.1,2.2,3.1,3.2,4.1-4.4,5.1-5.3,6.1-6.5,7.1 checked; 7.2 (COMPLETE) remains pending exact-tree/hosted for 933528e/49ebb79.
docs/agent/HANDOFF_12H_2026-08-27.md:24: - **F5 (High, partially fixed):** Secondary pruning failure not fail-closed — fixed for MFT/USN/Rebuild/Reconciliation with `secondaryPruningFaultInjector` and `return false`/retain, proven by `TestRulePruningSecondaryFailureIsFailClosed` and `TestRepeatedSecondaryFailure`. `TestReconciliationRetentionOnSecondaryFailure` currently relaxed (hasRemove check) — next frontier to tighten to expect retained `IsReconciliationPassActive` and `hasRemove && hasKeep`.
docs/agent/HANDOFF_12H_2026-08-27.md:29: - `src/engine/src/IndexPipeline.{h,cpp}`: timing partition, `SecondaryPruningFaultInjector`, fail-closed MFT/USN/Rebuild/Reconciliation.
docs/agent/DEEP_AUDIT_2026-08-28_FASTFILES.md:117: - fault-injection production isolation;
docs/agent/DEEP_AUDIT_2026-08-28_FASTFILES.md:129: - explicit boundary identity in the secondary-pruning fault injector;
docs/agent/DEEP_AUDIT_2026-08-28_FASTFILES.md:155: | FF-P1-04 | P1 High | `IndexPipeline.h/.cpp`, engine tests | The fault injector is described as named-boundary but accepts only `(volume, changes)`; tests cannot select MFT vs USN vs rebuild vs reconciliation explicitly. Existing evidence extrapolates MFT behavior to other callers. | Fault seam lacks a boundary discriminator; coverage relies on call sequencing. | Distinct callers can regress while one generic test stays green. | Add an enum/boundary token to the test seam and build separate fix... [truncated]
docs/agent/DEEP_AUDIT_2026-08-28_FASTFILES.md:159: | FF-P2-03 | P2 Medium | `IndexPipeline.h/.cpp` | `SetSecondaryPruningFaultInjector` is public internal API and compiled unconditionally although design calls it test-only/guarded. | No test-build guard/friend harness separation. | Unnecessary production surface and spec mismatch. | Compile-guard or encapsulate the seam without weakening testability. | Release symbol/API review and all fault tests pass in test builds; production defaults/surface unchanged. |
docs/agent/DEEP_AUDIT_2026-08-27_FASTFILES.md:146: Required next campaign: deterministic writer fault injection at secondary mutation boundaries, then classify atomic/fail-closed versus explicit eventual-recovery semantics. Test durable state, projection state, search/snapshot/aggregate visibility, return status, epoch/publication, restart, retry, and next reconciliation before choosing the repair.
docs/agent/DEEP_AUDIT_2026-08-26_FASTFILES.md:14: A second, potentially correctness-relevant seam was identified in ingestion/reconciliation failure handling: rule-pruning is a second durable mutation after initial MFT/USN persistence. Current `IndexPipeline` logic appears able to report the ingestion operation as successful when the subsequent durable removal of rule-excluded rows fails. Related cleanup/rebuild paths have similar second-mutation behavior. This requires contract review and deterministic fault injection before classification; it... [truncated]
docs/agent/DEEP_AUDIT_2026-08-26_FASTFILES.md:110: The repository may intentionally define rule pruning as eventually reconciled rather than transactionally coupled to initial ingestion. Current tests cover reconciliation, restart equivalence, cold mutations, eviction, search rules, and durable state broadly, but the inspected pipeline suite does not establish a deterministic fault-injection contract for this second mutation.
docs/agent/DEEP_AUDIT_2026-08-26_FASTFILES.md:125: Confidence: medium-high that the failure-propagation seam exists; unclassified impact until fault injection.
docs/agent/DEEP_AUDIT_2026-08-26_FASTFILES.md:178: 3. Investigate rule-pruning/secondary-durable-mutation failure closure with deterministic fault injection.
openspec/specs/independent-index-read-sessions/spec.md:112: closing the authoritative writer connection. Fault injection SHALL cover
openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure/tasks.md:31: - [x] 4.2 Add a hermetic fault-injection fixture that simulates an operation failing to complete promptly without hanging a production volume; cover cancellation during active scanning and journal operations. — done: `StallFixture` in `tests/indexsvc/test_volume_scanner_terminal.cpp` parks a worker in an uncancelable wait (the non-cooperative `DeviceIoControl` shape); scan cases cover a parked scan worker, a late-completing scan operation, a dead connection, supersede/admission, repeated cycles,... [truncated]
openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure/specs/raw-volume-cancellation-bounds/spec.md:25: A deterministic, hermetic fault-injection fixture MUST simulate an operation that fails to complete promptly. The fixture MUST NOT deliberately hang a production volume. Tests MUST cover cancellation during active scanning and journal operations. Each of the following MUST be recorded as a separate guarantee: time until cancellation is requested, time until the control path responds, time until the worker stops, whether cleanup requires kernel I/O completion, whether new work can be admitted, wh... [truncated]
third_party/sqlite/sqlite3.c:22758: ** Available fault injectors.  Should be numbered beginning with 0.
third_party/sqlite/sqlite3.c:22760: #define SQLITE_FAULTINJECTOR_MALLOC     0
third_party/sqlite/sqlite3.c:22761: #define SQLITE_FAULTINJECTOR_COUNT      1
third_party/sqlite/sqlite3.c:40616: ** they may be overridden at runtime to facilitate fault injection during
third_party/sqlite/sqlite3.c:49290: ** they may be overridden at runtime to facilitate fault injection during
openspec/changes/large-index-lifecycle-and-search-scalability/tasks.md:18: - [x] 3.2 Build the differential isolation matrix (arms A/B1/B2/C/D/E1/E2/F1/G0/G1/G2) as a benchmark-only seam (`FF_INDEXSTORE_TESTING`, never production) and attribute the delta by measurement, not source inference. Evidence: `evidence/rebuild/rebuild-isolation-*.json` + `bench_rebuild_isolation`.
openspec/changes/active-path-runtime-resilience/tasks.md:16: - [x] 3.1 Extend snapshot publication fault injection to prove mapping/serialization failure preserves the last-known-good generation and does not advertise an incomplete generation. — `SnapshotPublisher::TestWriteSnapshot` forces serialization failure; `ffengine_snapshot_publisher_tests` proves unchanged generation/name and successful recovery.
openspec/changes/active-path-runtime-resilience/proposal.md:61: All new fault injection is test-only, deterministic, and inaccessible in
openspec/changes/active-path-runtime-resilience/evidence/audit.md:18:   and has deterministic mapping-failure hooks.
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/review-remediation-2026-10-21.md:84: Seam: `Projection::SetTestingFoldExceptionalUnit` (FF_INDEXSTORE_TESTING only;
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/explore-findings.md:18:   `FF_INDEXSTORE_TESTING` seam). 200k clean attribution: B1 production removal
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/equivalence-gate-adversarial-matrix.md:107: `FF_INDEXSTORE_TESTING`). Reviewer-A caveat: this traversal shares the
openspec/changes/large-index-lifecycle-and-search-scalability/design.md:40: `FF_INDEXSTORE_TESTING` compiles the `RemovalMaintenanceMode` attribution seam into
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/rebuild/rebuild-isolation-before.json:386:   "limitations": "Wall-clock single-run arms on one machine; comparable within one report more strongly than across machines. Trigram/child operation counts are derived analytically in the evidence document, not counted in-process. FF_INDEXSTORE_TESTING seam is compiled only into this benchmark target; production builds contain no seam."
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/rebuild/rebuild-isolation-after-fix.json:290:   "limitations": "Wall-clock single-run arms on one machine; comparable within one report more strongly than across machines. Trigram/child operation counts are derived analytically in the evidence document, not counted in-process. FF_INDEXSTORE_TESTING seam is compiled only into this benchmark target; production builds contain no seam."
openspec/changes/nonblocking-indexed-read-plane/tasks.md:21: - [x] 3.5 Add read-session fault injection for open/begin/read/busy/stream abort/point lookup/cancellation/database close and prove the writer remains usable after each injected failure. — open/begin/busy/stream/mid-stream/point/aggregate/database-close faults and pipeline cancellation are covered; failures are fail-closed.
openspec/changes/nonblocking-indexed-read-plane/proposal.md:56: benchmark, and fault-injection tests will gain deterministic barriers and
tests/indexsvc/test_volume_scanner_terminal.cpp:1021:         const ffindexsvc::ScanRunResult result = ffindexsvc::RunVolumeScanWithFailureHookForTesting(
openspec/changes/nonblocking-indexed-read-plane/specs/independent-index-read-sessions/spec.md:100: closing the authoritative writer connection. Fault injection SHALL cover
tests/indexstore/test_store_read_session.cpp:167:         return point == ffindexstore::ReadSessionFaultPoint::Open;
tests/indexstore/test_store_read_session.cpp:172:         return point == ffindexstore::ReadSessionFaultPoint::BeginSnapshot;
tests/indexstore/test_store_read_session.cpp:177:         return point == ffindexstore::ReadSessionFaultPoint::Busy;
tests/indexstore/test_store_read_session.cpp:182:         return point == ffindexstore::ReadSessionFaultPoint::Stream;
tests/indexstore/test_store_read_session.cpp:190:         return point == ffindexstore::ReadSessionFaultPoint::Stream && ++streamCalls >= 2;
tests/indexstore/test_store_read_session.cpp:197:         return point == ffindexstore::ReadSessionFaultPoint::FindEntry;
tests/indexstore/test_store_read_session.cpp:203:         return point == ffindexstore::ReadSessionFaultPoint::Aggregate;
tests/indexstore/test_store_read_session.cpp:217:         return point == ffindexstore::ReadSessionFaultPoint::DatabaseClosed;
tests/indexstore/CMakeLists.txt:6: # compiles Projection.cpp with the FF_INDEXSTORE_TESTING fold-exceptional seam
tests/indexstore/CMakeLists.txt:13: target_compile_definitions(ffindexstore_projection_tests PRIVATE FF_INDEXSTORE_TESTING=1)
tests/indexstore/CMakeLists.txt:35: # FF_INDEXSTORE_TESTING compiles the benchmark-only removal-maintenance seam
tests/indexstore/CMakeLists.txt:41: target_compile_definitions(ffindexstore_bench_rebuild_isolation PRIVATE FF_INDEXSTORE_TESTING=1)
tests/indexstore/bench_rebuild_isolation.cpp:21: // FF_INDEXSTORE_TESTING compiles the benchmark-only
tests/indexstore/bench_rebuild_isolation.cpp:692:               "in the evidence document, not counted in-process. FF_INDEXSTORE_TESTING seam is compiled "
openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/tasks.md:10: ## 2. Fault injection seam
openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/tasks.md:12: - [x] 2.1 Finish the deterministic **named-boundary** fault injector. The current `SetSecondaryPruningFaultInjector(volume, changes)` seam is only partial: it does not identify MFT vs USN vs rebuild vs reconciliation and is compiled into the ordinary `IndexPipeline` API despite the design calling it test-only. Add an explicit boundary discriminator and isolate/guard the test seam without changing production defaults.
openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/evidence/implementation-and-validation.md:9: `IndexPipeline` builds do not contain the mutable fault-injection member or
openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/design.md:9: - Add deterministic fault injection at named secondary boundaries without changing production defaults.
openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/design.md:26: Add an `IndexPipeline` test-only setter `SetSecondaryPruningFaultInjector` (or extend the existing `Store` fault seam) that can force the next secondary `ApplyBatch` at the named boundary to return `false` once or repeatedly, with automatic reset. This avoids flaky file-lock tricks and keeps production behavior unchanged when no injector is installed. Over a generic SQLite error injector, a named-boundary injector makes tests precise and not fragile to unrelated store calls.
openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/design.md:36: - **Test coverage vs invasiveness:** the fault injector is test-only and not part of the product surface; it is guarded by `FASTFILES_TEST` or a `friend` test seam to avoid shipping it.
openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/design.md:42: - **Filesystem-level fault injection (lock, disk full):** rejected—flaky and non-deterministic.
tests/engine/test_index_pipeline_concurrency.cpp:266:     pipeline.SetReadSessionFaultInjector([](const auto point) {
tests/engine/test_index_pipeline_concurrency.cpp:267:         return point == ffindexstore::ReadSessionFaultPoint::Open;
tests/engine/test_index_pipeline_concurrency.cpp:271:     pipeline.SetReadSessionFaultInjector({});
tests/engine/test_index_pipeline_concurrency.cpp:277:     pipeline.SetReadSessionFaultInjector([](const auto point) {
tests/engine/test_index_pipeline_concurrency.cpp:278:         return point == ffindexstore::ReadSessionFaultPoint::FindEntry;
tests/engine/test_index_pipeline_concurrency.cpp:284:     pipeline.SetReadSessionFaultInjector([](const auto point) {
tests/engine/test_index_pipeline_concurrency.cpp:285:         return point == ffindexstore::ReadSessionFaultPoint::Stream;
tests/engine/test_index_pipeline_concurrency.cpp:290:     pipeline.SetReadSessionFaultInjector({});
tests/engine/test_index_pipeline_concurrency.cpp:1045:     pipeline.SetReadSessionFaultInjector([](const auto point) {
tests/engine/test_index_pipeline_concurrency.cpp:1046:         return point == ffindexstore::ReadSessionFaultPoint::Stream;
tests/engine/test_index_pipeline_concurrency.cpp:1050:     pipeline.SetReadSessionFaultInjector({});
tests/engine/test_index_pipeline.cpp:57:     bool ShouldFail(const SecondaryBoundary boundary, VolumeRowId volumeId,
tests/engine/test_index_pipeline.cpp:77:     pipeline.SetSecondaryPruningFaultInjector(
tests/engine/test_index_pipeline.cpp:80:             return plan.ShouldFail(boundary, volumeId, changes);
tests/engine/test_index_pipeline.cpp:1876:     pipeline.SetSecondaryPruningFaultInjector({});
tests/engine/test_index_pipeline.cpp:1913:     pipeline.SetSecondaryPruningFaultInjector({});
tests/engine/test_index_pipeline.cpp:1976:     pipeline.SetSecondaryPruningFaultInjector({});
tests/engine/test_index_pipeline.cpp:2012:     pipeline.SetSecondaryPruningFaultInjector({});
tests/engine/test_index_pipeline.cpp:2075:     pipeline.SetSecondaryPruningFaultInjector({});
tests/engine/test_index_pipeline.cpp:2122:     pipeline.SetSecondaryPruningFaultInjector({});
tests/engine/test_index_pipeline.cpp:2190:     pipeline.SetSecondaryPruningFaultInjector({});
tests/engine/test_index_pipeline.cpp:2248:     pipeline.SetSecondaryPruningFaultInjector({});
openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/specs/index/rule-pruning-durability/spec.md:29: ### Requirement: Deterministic secondary-write fault injection
openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/specs/index/rule-pruning-durability/spec.md:34: - **WHEN** a test installs a fault injector that forces the named secondary `ApplyBatch` to fail exactly once
openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/proposal.md:8: - Introduce deterministic writer fault injection at named secondary transaction boundaries (production defaults unchanged; prefer an injectable seam over flaky filesystem tricks).
src/indexsvc/src/VolumeScanner.h:142: ScanRunResult RunVolumeScanWithFailureHookForTesting(
src/indexsvc/src/VolumeScanner.cpp:570: ScanRunResult RunVolumeScanWithFailureHookForTesting(
src/indexstore/src/Store.cpp:1112: bool StoreReadSession::ShouldFail(const ReadSessionFaultPoint point) const {
src/indexstore/src/Store.cpp:1113:     return options_.faultInjector && options_.faultInjector(point);
src/indexstore/src/Store.cpp:1117:     if (ShouldFail(ReadSessionFaultPoint::Open)) return false;
src/indexstore/src/Store.cpp:1122:     if (sqlite3_busy_timeout(db_, 250) != SQLITE_OK || ShouldFail(ReadSessionFaultPoint::Busy) ||
src/indexstore/src/Store.cpp:1128:     if (ShouldFail(ReadSessionFaultPoint::BeginSnapshot) || !ExecSql(db_, "BEGIN;")) {
src/indexstore/src/Store.cpp:1166:     if (ShouldFail(ReadSessionFaultPoint::DatabaseClosed)) {
src/indexstore/src/Store.cpp:1182:         if (ShouldFail(ReadSessionFaultPoint::Stream)) {
src/indexstore/src/Store.cpp:1222:     if (ShouldFail(ReadSessionFaultPoint::DatabaseClosed)) {
src/indexstore/src/Store.cpp:1241:         if (ShouldFail(ReadSessionFaultPoint::Stream)) {
[100 matches limit reached. Use limit=200 for more, or refine pattern. Some lines truncated to 500 chars. Use read tool to see full lines]
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
grep: C:/Users/Michael Roy/Documents/FastFiles
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "ffindexstore/EntryRecord.h"
#include "ffindexstore/Identity.h"
struct sqlite3;
namespace ffindexstore {
// nonblocking-indexed-read-plane 3.1: an independent read connection can be
// fault-injected at a named boundary without weakening the production
// one-writer contract. The injector returns true to force that operation to
// fail closed.
enum class ReadSessionFaultPoint : uint8_t {
    Open,
    BeginSnapshot,
    Busy,
    DatabaseClosed,
    Stream,
    FindEntry,
    Aggregate,
};
using ReadSessionFaultInjector = std::function<bool(ReadSessionFaultPoint)>;
struct StoreReadSessionOptions {
    ReadSessionFaultInjector faultInjector;
};
class StoreReadSession;
// Per-volume metadata persisted alongside its entries (tasks.md 1.3):
// availability, USN journal identity/position for resume-or-reconcile
// (design.md D6), the resumable initial-scan cursor (D8), and the row
// count used to pre-size the in-memory projection on rebuild (D4).
struct VolumeMetadata {
    VolumeRowId rowId = 0;
    VolumeKey key;
    bool available = true;
    uint64_t lastSeenTime = 0;
    std::optional<uint64_t> journalId;
    uint64_t resumeUsn = 0;
    std::vector<uint8_t> scanCursor; // opaque, service-issued (D8); empty == none
    bool scanComplete = false;
    uint64_t entryCount = 0;
    uint64_t lastReconciliationTime = 0;
};
// FastFilesEngine's durable, crash-safe source of truth (design.md D1): a
// single-file SQLite database in WAL journal mode. Every persisted entry is
// keyed by (volume, FileReferenceNumber) only -- never by path (D7).
//
// Not internally synchronized beyond what SQLite's own WAL mode provides
// for concurrent readers vs. the single ingestion writer (task 1.5's
// "concurrent readers not blocked by an in-progress write"); callers that
// need cross-call atomicity (e.g. read-modify-write of volume metadata)
// must serialize those calls themselves.
class Store {
public:
    Store() = default;
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    // dbPathUtf8: filesystem path to the database file, UTF-8 encoded.
    // Windows call sites convert their std::wstring path to UTF-8 at the
    // boundary (keeping this library platform-agnostic and independently
    // testable). Opens (creating if absent), sets WAL + synchronous=NORMAL
    // (design.md "Risks" WAL-growth mitigation), and ensures the schema
    // exists at the current schema version.
    //
    // Runs an integrity check first (task 1.7); if it reaches a *corrupt*
    // verdict against an existing file, the open fails
    // (outIntegrityFailed is set to true) so the caller can fall back to a
    // fresh scan rather than trusting data known to be corrupt (spec
    // "Corruption is detectable"). index-durability-false-success-closure
    // D6: an integrity check that could not reach a verdict (lock, I/O,
    // busy, OOM -- anything but a positive corruption report) fails the
    // open *without* setting outIntegrityFailed, because "unproven" is not
    // "corrupt" and must never qualify the database for deletion.
    bool Open(const std::string& dbPathUtf8, bool* outIntegrityFailed = nullptr);
    void Close();
    bool IsOpen() const noexcept { return db_ != nullptr; }
    // nonblocking-indexed-read-plane 3.1: opens a move-only, query-only
    // SQLite connection with its own WAL read transaction. The returned
    // session never runs migrations, writes, checkpoints, or vacuum and must
    // not be used as a second writer.
    std::unique_ptr<StoreReadSession> OpenReadSession(
        const StoreReadSessionOptions& options = {}) const;
    // task 1.7, also callable standalone (e.g. from a diagnostics path).
    bool RunIntegrityCheck();
    // index-durability-false-success-closure D6: the verdict behind
    // RunIntegrityCheck(). Ok = the check completed and reported "ok";
    // Corrupt = the check completed and reported damage (or SQLite itself
    // classified the file as not-a-database/corrupt); Inconclusive = the
    // check could not be completed or classified (prepare failed, BUSY,
    // LOCKED, NOMEM, IOERR, READONLY, INTERRUPT, FULL, ...). Only Corrupt
    // is ever grounds for destroying a database.
    enum class IntegrityVerdict { Ok, Corrupt, Inconclusive };
    IntegrityVerdict RunIntegrityCheckDetailed();
    // Classification of a non-row sqlite3_step result code from the
    // integrity_check statement. Exposed (and unit-testable) so the
    // corrupt-vs-inconclusive boundary is a testable pure rule rather than
    // an untestable control-flow accident. Corrupt-family primary result
    // codes (SQLITE_CORRUPT=11, SQLITE_NOTADB=26, masking extended codes)
    // classify as Corrupt; *everything* else classifies as Inconclusive --
    // fail-safe in the direction that never deletes data.
    static IntegrityVerdict ClassifyIntegrityStep(int stepCode) noexcept;
    // task 1.6: passive checkpoint, cheap and safe to call frequently
    // (e.g. on an idle timer); does not block writers.
    bool CheckpointPassive();
    // Forces a full checkpoint if the WAL file has grown past
    // thresholdBytes -- the size-triggered forced checkpoint mitigation
    // for unbounded WAL growth under sustained write bursts (design.md
    // "Risks"). Returns false only on an I/O/database error; a WAL file
    // under the threshold is a normal, successful no-op.
    bool CheckpointIfWalExceeds(uint64_t thresholdBytes);
    // address-engineering-audit-findings 5.1: refresh SQLite query-planner
    // statistics after a sufficiently large ingestion batch.
    bool Analyze();
    // address-engineering-audit-findings 5.2: run VACUUM only during an
    // idle window after the configured interval and size-growth gates pass.
    // nowEpochSeconds is injectable so the gating policy is deterministic in
    // tests; the production caller passes current wall-clock seconds.
    bool VacuumIfDue(bool userIdle, uint64_t nowEpochSeconds, uint64_t intervalHours,
                     uint64_t sizeGrowthPercent);
    // --- Volume metadata (tasks.md 1.3, section 7/8's persistence needs) ---
    // Looks up the volume by its durable key, creating a new row (with
    // available=true, no journal/scan state) if this is the first time
    // it's been seen. The returned VolumeRowId is stable for the life of
    // the row (see Identity.h).
    std::optional<VolumeRowId> GetOrCreateVolume(const VolumeKey& key, bool* outCreated = nullptr);
    std::optional<VolumeMetadata> GetVolumeMetadata(VolumeRowId volumeRowId);
    std::vector<VolumeMetadata> GetAllVolumes();
    // Indexed search needs volume identity/availability only; this avoids
    // counting every entry when the caller does not need entryCount.
    std::vector<VolumeMetadata> GetAllVolumesWithoutEntryCounts();
    // tasks.md 7.2/7.3: mark availability without touching entries.
    bool SetVolumeAvailable(VolumeRowId volumeRowId, bool available, uint64_t nowTimestamp,
                            bool* outVisibilityChanged = nullptr);
    // tasks.md 7.5/7.6: persisted journal identity/position, compared
    // against what the service reports on reconnect (design.md D6).
    bool SetJournalPosition(VolumeRowId volumeRowId, uint64_t journalId, uint64_t resumeUsn);
    // tasks.md 6.3/6.4/D8: resumable scan cursor, cleared once the initial
    // scan finishes.
    bool SetScanCursor(VolumeRowId volumeRowId, const std::vector<uint8_t>& cursor);
    bool MarkScanComplete(VolumeRowId volumeRowId);
    bool SetLastReconciliationTime(VolumeRowId volumeRowId, uint64_t nowTimestamp);
    // tasks.md 7.4: explicit, separate user-triggered permanent removal --
    // distinct from the automatic disconnect handling, which never deletes.
    // Refuses missing or currently-available volumes.
    bool ForgetVolume(VolumeRowId volumeRowId);
    // --- Entries (tasks.md 1.2, 1.5) ---
    // Applies the whole batch as one explicit transaction (task 1.5):
    // Upsert changes are written via INSERT ... ON CONFLICT DO UPDATE
    // keyed on (volume, FRN) (spec "Re-ingesting the same physical file
    // updates one row, not two"); Remove changes delete that row. Returns
    // false (and leaves the database unchanged, courtesy of the rolled-
    // back transaction) on any failure, so the caller can retry the same
    // batch (task 3.4) without risking a partial commit.
    bool ApplyBatch(VolumeRowId volumeRowId, const std::vector<EntryChange>& changes);
    // task 3.1: streams every entry belonging to a volume, in an
    // unspecified but stable-for-the-duration-of-the-call order, without
    // materializing the whole result set at once.
    using EntryVisitor = std::function<void(const EntryRecord&)>;
    bool ForEachEntry(VolumeRowId volumeRowId, const EntryVisitor& visitor);
    // memory-bounded-indexed-scale 2.6: cancellable streaming variant. The
    // visitor returns false to stop early (true to continue); the call returns
    // false only on a database error -- an early visitor stop is a normal,
    // successful bounded read. Backs cold indexed-search enumeration so a
    // superseded/cancelled query stops SQLite work immediately.
    using EntryVisitorUntil = std::function<bool(const EntryRecord&)>;
    bool ForEachEntryUntil(VolumeRowId volumeRowId, const EntryVisitorUntil& visitor);
    // memory-bounded-indexed-scale 2.1: durable point lookup of one entry by
    // (volume, FileReferenceNumber). std::nullopt when the row is absent or
    // the store is closed. Backs cold candidate parent-chain reconstruction so
    // a matched row inside an evicted subtree can resolve its folder with
    // O(depth) bounded reads instead of reloading the volume.
    std::optional<EntryRecord> FindEntry(VolumeRowId volumeRowId, FileId frn);
    // task 8.1/8.4: full identity set for a volume, used by reconciliation
    // to anti-join against fresh ground truth without re-streaming every
    // field for entries that haven't changed.
    std::vector<FileId> ListEntryIds(VolumeRowId volumeRowId);
    uint64_t CountEntries(VolumeRowId volumeRowId);
    // file-preview-and-properties §6.2 / storage-analysis §3.1: recursive
    // subtree aggregate from the durable store. Returns {itemCount,
    // totalSizeBytes, ...} for all descendants of `parentFrn` within
    // `volumeRowId`, or {0, 0, ...} if the root is unknown. Uses a recursive
    // CTE so this works without rebuilding the projection.
    // allocated-size-storage-accuracy D7: the allocated-size subtotal covers
    // only descendants whose allocated size is known; the completeness pair
    // (known count / total count) is the explicit state -- `Complete` only
    // when every descendant is known, never a silently partial total.
    struct FolderAggregate {
        uint64_t itemCount = 0;
        uint64_t totalSizeBytes = 0;
        uint64_t totalAllocatedSizeBytes = 0;
        uint64_t allocatedBytesKnownCount = 0;
        uint64_t allocatedBytesTotalCount = 0;
    };
    FolderAggregate GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn);
private:
    friend class StoreReadSession;
    std::vector<VolumeMetadata> GetAllVolumesImpl(bool includeEntryCounts);
    sqlite3* db_ = nullptr;
    std::string dbPathUtf8_;
    uint64_t lastVacuumTimeSeconds_ = 0;
    uint64_t lastVacuumSizeBytes_ = 0;
    mutable int cachedPageSize_ = 0;
};
// A durable read snapshot owned by one query. Store itself deliberately stays
// unsynchronized and connection-owned; callers never invoke its writer
// connection concurrently from a query worker. A session's transaction is
// closed deterministically by Close()/destruction, making reader resources
// reusable even when a visitor cancels or a SQLite step fails.
class StoreReadSession {
public:
    StoreReadSession() = delete;
    ~StoreReadSession();
    StoreReadSession(const StoreReadSession&) = delete;
    StoreReadSession& operator=(const StoreReadSession&) = delete;
    StoreReadSession(StoreReadSession&& other) noexcept;
    StoreReadSession& operator=(StoreReadSession&& other) noexcept;
    bool IsOpen() const noexcept { return db_ != nullptr && transactionActive_; }
    bool Failed() const noexcept { return failed_; }
    void Close() noexcept;
    bool ForEachEntryUntil(VolumeRowId volumeRowId, const Store::EntryVisitorUntil& visitor);
    // large-index-lifecycle-and-search-scalability review-remediation 18.x:
    // conservative SQL name prefilter for a pure-ASCII literal needle
    // (already ASCII-case-folded). The returned rows are a SUPERSET of every
    // row whose name could match the needle under the canonical ordinal
    // ignore-case matcher: an all-ASCII match span is found by SQLite's
    // ASCII-only case-insensitive LIKE, while a span containing any non-ASCII
    // UTF-16 unit implies the name carries non-ASCII UTF-8 bytes, which the
    // GLOB disjunct admits unconditionally. False positives reach the
    // canonical C++ matcher unchanged; false negatives are impossible for any
    // host ordinal table. Callers must pass ASCII needles only (non-ASCII
    // needles cannot be conservatively folded by SQLite LIKE).
    bool ForEachEntryWithAsciiNameUntil(VolumeRowId volumeRowId, std::string_view asciiFoldedNeedle,
                                        const Store::EntryVisitorUntil& visitor);
    std::optional<EntryRecord> FindEntry(VolumeRowId volumeRowId, FileId frn);
    Store::FolderAggregate GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn);
private:
    friend class Store;
    StoreReadSession(std::string dbPathUtf8, StoreReadSessionOptions options);
    bool Open();
    bool ShouldFail(ReadSessionFaultPoint point) const;
    sqlite3* db_ = nullptr;
    std::string dbPathUtf8_;
    StoreReadSessionOptions options_;
    bool transactionActive_ = false;
    bool failed_ = false;
};
} // namespace ffindexstore
tests/indexstore/test_projection.cpp:368:     Check(candidates.has_value(), "eligible filename terms use the trigram candidate index");
openspec/changes/search-availability-under-index-churn/tasks.md:27: - [x] 4.2 Apply captured include/exclude rules to hot and durable candidates using canonical reconstructed paths, preserving Unicode/case, scope, filters, ordering, caps, and allocated-size knownness. — captured-rule filters use existing projection/durable path reconstruction; existing equivalence/allocated-size/Unicode tests remain green.
tests/engine/test_index_pipeline.cpp:750:           "indexed search returns a durable candidate with its reconstructed parent folder");
openspec/changes/durable-cold-search-index-and-hybrid-residency/proposal.md:45: 1. **Durable candidate-index alternatives** (schema v3 or equivalent):
openspec/changes/durable-cold-search-index-and-hybrid-residency/proposal.md:93:   if a durable candidate generator is adopted;
openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/index-engine/spec.md:13: Any durable candidate structure consulted by the cold path SHALL return a
openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/index-engine/spec.md:27:   the cold path with the durable candidate structure available
openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/index-engine/spec.md:53: The durable candidate structure SHALL never disagree with the entries table
ls: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
design.md
proposal.md
specs/
tasks.md
# Proposal: durable-cold-search-index-and-hybrid-residency
Status: **PROPOSAL / DECISION RECORD ONLY — implementation is explicitly not
started.** This change was prepared during the `large-index-lifecycle-and-
search-scalability` closure campaign (PR #14 handoff) as the separately scoped
successor that campaign deliberately deferred. It must not be folded into PR
#14 and must not begin production implementation without an explicit owner
authorization and a completed measurement phase (task group 1).
## Problem
At 2M+ indexed rows the product enters a measured **cold regime**: the
in-memory projection (~839.7 MB modeled at 2M rows) exceeds the 500 MiB
default budget (`Projection::kDefaultMaximumMemoryBytes`; enforcement
watermark 400 MiB), `SetProjectionMemoryBudget` has no production caller, and
every query then serves from the SQLite cold stream. Measured at 2M
(`evidence/search/search-matrix-before.json`, historical p50): selective
~2.5 s, broad ~27.1 s, extension ~37.6 s per keystroke-triggered query class.
The completed predecessor campaign shipped the safe, schema-free
improvements and stopped here on purpose:
- hot/projection-resident selective search is already accelerated
  (filename-trigram accelerator, 56-189x on eligible literal classes at 1M,
  rows examined 1M -> 1);
- the cold path gained a conservative SQL name prefilter
  (`ForEachEntryWithAsciiNameUntil`, 1.28-1.44x on cold literal classes at
  1M with 9.8% non-ASCII names), but the **hard floor is the name-column
  scan itself (~1.05-1.10 us/scanned-row)** — a zero-survivor literal is
  still ~1.1 s at 1M and proportionally worse at 2M-5M;
- non-literal classes (broad, extension, short_prefix, unicode) are
  prefilter-ineligible by design and remain O(N) scans on the cold path.
The largest remaining user-relevant cost at production scale is therefore
**cold candidate generation at 2M-5M+ rows**: no durable structure exists
that can answer "which rows could match this needle?" without scanning and
decoding every row of the volume.
## What this proposal investigates (measure first, decide second)
The proposal does **not** assume FTS, schema v3, or hybrid residency is the
answer. It defines a measurement campaign over alternatives, with a decision
record per alternative:
1. **Durable candidate-index alternatives** (schema v3 or equivalent):
   - SQLite FTS5 (unicode61/trigram tokenizer) as a candidate generator;
   - a durable trigram posting table mirroring the in-memory accelerator's
     semantics (folded-trigram postings + exception set);
   - covering-index / sorted-name strategies (ordinal-key prefixes);
   - hybrid hot/cold residency (Option C of the predecessor's memory
     decision: a compact, always-resident candidate subset per volume) as a
     non-schema alternative;
   - budget-policy alternatives (adaptive budget) as the control arm.
2. **Candidate completeness under Windows ordinal comparison.** Every
   candidate generator must be a **conservative superset** of the canonical
   matcher's result set under `CompareStringOrdinal(..., TRUE)` semantics on
   the host table — including the fold-exceptional class (units the host
   uppercase table equates with ASCII, e.g. U+017F on some tables) and
   surrogate names, per the predecessor's P2-A repair
   (`Projection::SetTestingFoldExceptionalUnit`, host-derived memoized
   probe, fail-closed). An FTS tokenizer that drops such rows is not
   acceptable; the differential suites (hot/cold identity + ordering,
   `TestFilenameCandidatesAreConservativeSuperset` and descendants) are the
   acceptance oracle.
3. **Query planner selection.** When does the planner consult the durable
   index vs. stream the cold path? Candidate caps, eligibility gates (single
   literal, folded length, residency), and fallback-to-scan behavior must be
   cost-based and measured at 200k/1M/2M/5M.
4. **Schema migration and rollback.** Any schema v3 must be additive and
   versioned: migration cost at 2M-5M rows, crash-consistent index updates
   (the index must never disagree with the entries table after a crash —
   transactional coupling or rebuild-on-open), rollback to v2 with no data
   loss, and disk-size tradeoffs (measured index bytes per 1M rows per
   alternative).
5. **Bounded memory footprint.** The durable index is a disk structure, but
   caches/planners built on it must stay within documented bounds; no
   alternative may silently raise the default 500 MiB projection budget.
6. **Cold/hot result equivalence.** Cold (durable-index-assisted) and hot
   (projection-accelerated) results must remain byte-identical in identity
   and ordering for every query class — the pinned contract from the
   predecessor spec delta.
7. **Startup/rebuild cost.** Index build during scan/ingestion, incremental
   maintenance per USN batch, and reopen/recovery cost must be measured;
   a durable index that doubles ingestion time is a regression.
## Performance targets (to be validated, not promised)
Measure at 200k / 1M / 2M / 5M rows, same discipline as the predecessor
campaign (idle host gating, medians for hot, disclosed single samples for
cold, machine-readable artifacts with embedded identity):
- cold selective/exact/no-match at 2M: from seconds to the sub-200 ms class
  if a durable candidate generator is adopted;
- cold non-literal classes: measured improvement or an honest documented
  floor;
- ingestion and rebuild overhead: no material regression vs. the certified
  31-34x replacement-rebuild behavior at 1M.
## Impact
- **Affected components (if adopted later):** `ffindexstore` (schema,
  migration, candidate generation), `ffengine` (planner selection, budget
  policy), `ffsearch` (query classification), tests (differential +
  crash-consistency + migration suites). No protocol, service, installer, or
  UI change is implied.
- **Risk:** schema migration on multi-million-row user indexes is the
  highest-risk operation this product would yet ship; crash-consistency and
  rollback are mandatory acceptance items, not stretch goals.
- **Non-goals:** raising default memory consumption; changing canonical match
  semantics; hot-path redesign; any change to the certified PR #14 behavior.
- **Relationship to PR #14:** none — PR #14 remains a self-contained,
  certified candidate; this successor starts from its landed state.
## Decision record pointer
`design.md` in this directory is the living decision record: alternatives,
measurement plan, acceptance gates, and the retain/reject decision per
alternative (to be filled by the future campaign — current entries record
the deferred predecessor decisions that bound this work).
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
# Design / decision record: durable-cold-search-index-and-hybrid-residency
Status: **decision record seeded during the PR #14 closure campaign; every
alternative is UNDECIDED pending the measurement phase (task group 1).** No
production implementation exists or is authorized by this document.
## 1. Bounding facts (inherited, measured)
These constrain the design space and come from the completed
`large-index-lifecycle-and-search-scalability` campaign's evidence
(`evidence/memory-cold-decision.md`, `evidence/lifecycle-reload-resources.md`):
- Projection budget: 500 MiB default, 400 MiB early-enforcement watermark;
  `SetProjectionMemoryBudget` has no production caller (tests only). A 1M-row
  projection models ~414 MB (straddles the watermark); 2M models ~839.7 MB
  (always cold under default policy).
- Cold stream floor: ~1.05-1.10 us per scanned row (name-column scan,
  decode, match). The shipped conservative SQL name prefilter
  (`ForEachEntryWithAsciiNameUntil`) removes the survivor-decode share for
  ASCII literals (1.28-1.44x at 1M, 9.8% non-ASCII names) but cannot beat
  the scan floor; non-literal classes are ineligible by design.
- Hot path: filename-trigram accelerator (folded postings + host-derived
  fold-exceptional set + surrogate-conservative set) gives 56-189x on
  eligible literal classes at 1M; candidate cap
  `kMaximumFilenameIndexCandidates = 16,384` is the existing cost-based
  fallback to canonical scan.
- Correctness contract: hot/cold identity+ordering equivalence is pinned by
  differential suites; candidate sets must be conservative supersets under
  `CompareStringOrdinal(..., TRUE)` host semantics (predecessor P2-A repair:
  fold-exceptional classification via a memoized host probe, fail-closed on
  API error).
## 2. Alternatives to measure (all currently UNDECIDED)
| # | Alternative | Sketch | Principal risk |
|---|---|---|---|
| A | SQLite FTS5 candidate table | FTS5 (trigram or unicode61) over names, queried for rowid candidates, verified by the canonical matcher | Tokenizer case-folding is NOT Windows ordinal folding; must prove superset incl. fold-exceptional + surrogate classes, or maintain an exception side-set |
| B | Durable trigram posting table | Persist the in-memory accelerator's structure (folded trigram -> sorted rowid postings + exception set) in SQLite; incremental maintenance per batch | Write amplification on ingestion; crash-consistency of postings vs entries; disk size |
| C | Hybrid hot/cold residency | Keep a compact per-volume candidate subset resident (e.g. folded-name keys or a minimal automaton) while rows stay cold | Memory bound must be strict and provable; consistency with evicted rows; predecessor marked this "separately scoped architecture work" |
| D | Covering ordinal-key index | Sorted index on the matcher's folded name key; range scans for prefix/substring classes | Substring/wildcard classes get little benefit; index size |
| E | Budget policy (adaptive/default raise) | Control arm: serve 2M hot by raising/deriving the budget | Competes with system memory; forbidden without safety proof; does not help 5M |
## 3. Mandatory acceptance dimensions (per alternative)
1. **Completeness:** candidate superset proof under host ordinal semantics,
   with red-first differential tests (fold-exceptional, surrogate, dotted,
   mixed-case, short-name shapes) — the predecessor's
   `TestFilenameCandidatesAreConservativeSuperset` family extended to the
   durable generator.
2. **Equivalence:** cold-with-index results byte-identical to hot results for
   all 12 query classes at 200k/1M/2M (and 5M for the selected candidate).
3. **Migration:** v2 -> v3 cost at 2M-5M; rollback path; crash-consistency
   (kill-during-update recovery; index never disagrees with entries);
   rebuild-on-open as the fallback recovery.
4. **Resources:** planner/index cache memory bounds; disk bytes per 1M rows;
   no change to the default 500 MiB projection budget.
5. **Cost:** ingestion/USN-batch maintenance overhead; reopen/recovery cost;
   measured planner decision quality (when index is consulted vs scan).
6. **Scale:** p50/p90/p95 at 200k/1M/2M/5M per query class, machine-readable
   artifacts with embedded source identity (predecessor lane E E-8 lesson:
   artifacts must embed `--source-sha`).
## 4. Decision log
| Date | Decision | Status |
|---|---|---|
| 2026-10-21 | Proposal + decision record created at PR #14 closure; alternatives A-E recorded; measurement-first mandated; no alternative selected | RECORDED |
| (future) | Task group 1 measurements complete | PENDING |
| (future) | Alternative selected/rejected with measured rationale | PENDING |
| (future) | Schema/migration/rollback design for the selected alternative | PENDING |
## 5. Explicit non-decisions
- FTS5 is **not** presumed correct: its tokenizer folding must be proven a
  superset of Windows ordinal ignore-case on this host (see dimension 1) or
  supplemented with an exception side-set maintained like the in-memory
  accelerator's.
- Schema v3 is **not** presumed necessary: alternative C (hybrid residency)
  may achieve the latency targets without a schema change, and alternative E
  is retained only as a control arm.
- The 500 MiB default budget is **not** raised by this proposal.
# Tasks
Proposal-stage ledger. **Nothing here is started.** The first action of the
future campaign is task 1.1; no production implementation task may begin
before the measurement phase (group 1) has produced its decision record.
## 1. Measurement phase (mandatory before any implementation)
- [ ] 1.1 Reproduce the 2M-5M cold-regime baseline on the landed PR #14 tree (same harness discipline: idle gating, embedded source identity, machine-readable artifacts) at 200k / 1M / 2M / 5M, all 12 query classes, hot and cold.
- [ ] 1.2 Measure alternatives A-E from `design.md` as prototypes behind test-only seams (never production): candidate-generation latency, completeness status, ingestion overhead, index size, memory bounds.
- [ ] 1.3 Prove or disprove candidate completeness under Windows ordinal folding for each alternative (fold-exceptional + surrogate classes included); record red-first differential evidence.
## 2. Decision
- [ ] 2.1 Select or reject each alternative with measured rationale in the `design.md` decision log; state the schema/migration consequence explicitly.
- [ ] 2.2 If no alternative meets the targets with acceptable risk, record the measured floor and stop — an honest documented floor is an acceptable outcome.
## 3. Implementation (only after 2.x selection and owner authorization)
- [ ] 3.1 Implement the selected candidate generator incrementally (additive schema, versioned), with crash-consistent updates and rebuild-on-open fallback.
- [ ] 3.2 Wire cost-based planner selection (candidate cap + fallback to canonical scan preserved).
- [ ] 3.3 Migration + rollback tooling with measured v2->v3 cost at 2M-5M rows and crash-recovery tests.
## 4. Acceptance
- [ ] 4.1 Cold/hot result identity + ordering equivalence suites green for all query classes at 200k/1M/2M/5M.
- [ ] 4.2 Resource bounds proven (planner caches, disk bytes per 1M rows, default 500 MiB projection budget unchanged).
- [ ] 4.3 Full certification ladder at the final candidate (Debug/release-test CTest, Analyze, ASan slice, strict OpenSpec, local CI parity, E2E, UIA, visual), with the same honest classification discipline as the predecessor campaign.
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src
# index-engine (delta): durable cold-search candidate generation and hybrid residency
Proposal-stage acceptance invariants for `durable-cold-search-index-and-
hybrid-residency`. These requirements pin the correctness and resource
contract of whatever alternative the campaign's measurement phase selects;
they deliberately do not prescribe the implementation (FTS table, durable
trigram postings, hybrid residency, or another measured alternative).
## ADDED Requirements
### Requirement: Cold candidate generation shall be a conservative superset under Windows ordinal folding
Any durable candidate structure consulted by the cold path SHALL return a
conservative superset of every row the canonical matcher would accept under
`CompareStringOrdinal(..., TRUE)` host semantics, including rows whose names
contain units the host uppercase table equates with ASCII (the fold-exceptional
class) and rows containing surrogate units. Where a candidate generator cannot
prove this property structurally, the engine SHALL maintain a conservative
exception side-set equivalent to the in-memory accelerator's
fold-exceptional/surrogate handling, derived from the host table and fail-closed
on API error.
#### Scenario: Fold-exceptional name is not dropped by the candidate generator
- **WHEN** a query whose folded needle matches a fold-exceptional name (for
  example a name containing a unit the host table folds to ASCII) executes on
  the cold path with the durable candidate structure available
- **THEN** the candidate set contains that row
- **AND** the final result set is identical to the canonical scan's result set
#### Scenario: Generator failure falls back to the canonical scan
- **WHEN** the candidate structure is missing, stale, or cannot establish
  superset coverage for a query shape
- **THEN** the engine serves the query from the canonical cold scan
- **AND** results remain correct (possibly slower)
### Requirement: Cold-with-index results shall equal hot results
For every query class, results served with durable-index assistance on the
cold path SHALL be identical in identity and ordering to results served by the
hot projection path. This extends the pinned hot/cold equivalence contract to
the durable generator.
#### Scenario: Differential identity across scales
- **WHEN** the same corpus and query set are executed hot and cold-with-index
  at 200k, 1M, 2M, and 5M rows
- **THEN** result identities and orderings match exactly for all query classes
### Requirement: Index updates shall be crash-consistent with the entries table
The durable candidate structure SHALL never disagree with the entries table
after a process or machine crash. Updates SHALL be transactionally coupled to
entry mutations or carry a validity/version marker that forces rebuild-on-open
after an unclean shutdown.
#### Scenario: Kill during ingestion batch
- **WHEN** the process is terminated in the middle of an ingestion or USN
  batch that mutates both entries and the candidate structure
- **THEN** after restart the index is either fully consistent with the entries
  recorded at the last committed transaction or is rebuilt before use
- **AND** no query observes candidates for rows that do not exist
### Requirement: Schema migration and rollback shall be measured and reversible
Any schema addition SHALL be versioned and additive, with a measured migration
cost at 2M-5M rows, a documented rollback that loses no indexed data, and a
rebuild-on-open fallback when migration is interrupted.
#### Scenario: Interrupted migration
- **WHEN** migration from the previous schema version is interrupted by a crash
- **THEN** the store opens at the previous schema version with all entries
  intact and either resumes or rebuilds the candidate structure
### Requirement: The durable index shall not raise the default memory budget
Planner caches and candidate structures SHALL have documented, bounded memory
footprints. The default 500 MiB projection budget
(`Projection::kDefaultMaximumMemoryBytes`) SHALL NOT be raised by this change;
any future budget change requires its own measured safety case.
#### Scenario: Bounded caches under sustained query load
- **WHEN** sustained query traffic runs against a 5M-row store
- **THEN** documented cache bounds are not exceeded
- **AND** private bytes show no monotonic growth across query cycles
### Requirement: Planner selection shall be cost-based with canonical fallback
The engine SHALL choose between durable-index candidate generation and the
canonical scan by measured cost rules (needle eligibility, candidate cap,
structure availability), preserving the existing fallback-to-scan behavior for
ineligible query shapes.
#### Scenario: Broad query keeps the canonical scan
- **WHEN** a broad or ineligible query shape executes
- **THEN** the planner does not force candidate generation that exceeds its cap
- **AND** the canonical scan serves the query with correct results
Main.cpp:253:         return indexPipeline.ForgetVolume(volumeRowId)
IndexPipeline.h:280:     bool SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor);
IndexPipeline.h:281:     bool MarkScanComplete(ffindexstore::VolumeRowId id);
IndexPipeline.h:282:     bool SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn);
IndexPipeline.h:283:     bool SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp);
IndexPipeline.h:284:     bool ForgetVolume(ffindexstore::VolumeRowId id);
IndexPipeline.cpp:607:                         secondaryApplied = store_.ApplyBatch(meta.rowId, ruleExcluded);
IndexPipeline.cpp:650:     auto id = store_.GetOrCreateVolume(key, &created);
IndexPipeline.cpp:732:     if (!store_.ApplyBatch(volumeId, changes)) {
IndexPipeline.cpp:773:             secondaryApplied = store_.ApplyBatch(volumeId, ruleExcluded);
IndexPipeline.cpp:856:     if (!store_.ApplyBatch(volumeId, changes)) {
IndexPipeline.cpp:915:             secondaryApplied = store_.ApplyBatch(volumeId, ruleExcluded);
IndexPipeline.cpp:959: bool IndexPipeline::SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor) {
IndexPipeline.cpp:961:     const bool result = store_.SetScanCursor(id, cursor);
IndexPipeline.cpp:966: bool IndexPipeline::MarkScanComplete(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:968:     const bool result = store_.MarkScanComplete(id);
IndexPipeline.cpp:973: bool IndexPipeline::SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn) {
IndexPipeline.cpp:975:     const bool result = store_.SetJournalPosition(id, journalId, resumeUsn);
IndexPipeline.cpp:980: bool IndexPipeline::SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp) {
IndexPipeline.cpp:982:     const bool result = store_.SetLastReconciliationTime(id, nowTimestamp);
IndexPipeline.cpp:987: bool IndexPipeline::ForgetVolume(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:989:     if (!store_.ForgetVolume(id)) {
IndexPipeline.cpp:1050:             secondaryApplied = store_.ApplyBatch(id, removals);
VolumeSessionManager.cpp:933:     pipeline_.SetScanCursor(durableId, cursor);
VolumeSessionManager.cpp:1042:     pipeline_.MarkScanComplete(durableId);
VolumeSessionManager.cpp:1053:             pipeline_.SetLastReconciliationTime(durableId, NowAsFileTime());
VolumeSessionManager.cpp:1195:         pipeline_.SetJournalPosition(durableId, journalId, currentUsn);
VolumeSessionManager.cpp:1198:         pipeline_.SetJournalPosition(durableId, journalId, meta ? meta->resumeUsn : currentUsn);
VolumeSessionManager.cpp:1253:     pipeline_.SetJournalPosition(durableId, journalId, latestUsn);
VolumeSessionManager.cpp:1282:         pipeline_.SetJournalPosition(durableId, journalId, 0);
VolumeSessionManager.h:139:     // authoritative: no MarkScanComplete, no pass finalization.
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
IndexPipeline.h:41:     enum class SearchPhase : uint8_t {
IndexPipeline.h:178:     void SetReadSessionFaultInjector(ffindexstore::ReadSessionFaultInjector injector);
IndexPipeline.h:183:     enum class SecondaryPruningBoundary : uint8_t {
IndexPipeline.h:186:         RebuildRulePrune,
IndexPipeline.h:187:         ReconciliationRemoval,
IndexPipeline.h:189:     using SecondaryPruningFaultInjector = std::function<bool(
IndexPipeline.h:190:         SecondaryPruningBoundary, ffindexstore::VolumeRowId,
IndexPipeline.h:194:     void SetSecondaryPruningFaultInjector(SecondaryPruningFaultInjector injector);
IndexPipeline.h:200:     // ReconciliationSchedulerLoop poll). Runs a cheap passive checkpoint
IndexPipeline.h:212:     void RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt);
IndexPipeline.h:218:     void SetBulkScanActive(bool active);
IndexPipeline.h:269:     bool ShouldInjectSecondaryPruningFailureLocked(
IndexPipeline.h:270:         SecondaryPruningBoundary boundary, ffindexstore::VolumeRowId volumeId,
IndexPipeline.h:283:     bool SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp);
IndexPipeline.h:288:     // runs to completion, since a full MFT enumeration is ground truth
IndexPipeline.h:289:     // for what currently exists. BeginReconciliationPass starts tracking
IndexPipeline.h:292:     // recorded; FinishReconciliationPass (call on ScanComplete) removes
IndexPipeline.h:299:     void BeginReconciliationPass(ffindexstore::VolumeRowId id);
IndexPipeline.h:300:     void FinishReconciliationPass(ffindexstore::VolumeRowId id);
IndexPipeline.h:306:     void AbortReconciliationPass(ffindexstore::VolumeRowId id);
IndexPipeline.h:307:     bool IsReconciliationPassActive(ffindexstore::VolumeRowId id);
IndexPipeline.h:377:     enum class QueryMutationReason : uint8_t {
IndexPipeline.h:381:         Rebuild,
IndexPipeline.h:418:     ffindexstore::ReadSessionFaultInjector readSessionFaultInjector_;
IndexPipeline.h:420:     SecondaryPruningFaultInjector secondaryPruningFaultInjector_;
IndexPipeline.h:461:     void MaybeAnalyzeAfterBulkInsertLocked(size_t recordCount);
IndexPipeline.cpp:34: std::wstring ToWString(std::u16string_view text) {
IndexPipeline.cpp:38: std::u16string ToU16String(std::wstring_view text) {
IndexPipeline.cpp:42: std::wstring NormalizeVolumePrefix(std::wstring value) {
IndexPipeline.cpp:56: std::wstring StripVolumeRootName(const ffindexstore::Projection& projection,
IndexPipeline.cpp:81: bool MaterializeCandidateFolder(const ffindexstore::Projection& projection,
IndexPipeline.cpp:108: bool MaterializeCandidateFolderDurable(
IndexPipeline.cpp:236: std::wstring CandidateCanonicalPath(const ffsearch::Candidate& candidate) {
IndexPipeline.cpp:243: bool CandidateIncludedByRules(
IndexPipeline.cpp:314: std::optional<ffindexstore::EntryFieldMask> ToEntryFieldMask(ffprotocol::UsnDeltaFieldMask fields) {
IndexPipeline.cpp:339: void UpdatePeak(std::atomic<size_t>& peak, const size_t value) {
IndexPipeline.cpp:371: std::optional<std::string> ColdNamePrefilterNeedle(const ffsearch::Query& query) {
IndexPipeline.cpp:386: bool IndexPipeline::VolumeSettingsEqual(const ffprotocol::VolumeSetting& left,
IndexPipeline.cpp:400: void IndexPipeline::AdvanceQueryVisibleEpochLocked(const ffindexstore::VolumeRowId volumeId,
IndexPipeline.cpp:418: void IndexPipeline::RecordMetadataOnlyUpdateLocked() {
IndexPipeline.cpp:423: void IndexPipeline::AdvanceResidencyEpochsForStructuralChangeLocked() {
IndexPipeline.cpp:442: bool IndexPipeline::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed) {
IndexPipeline.cpp:456: void IndexPipeline::Close() {
IndexPipeline.cpp:467: void IndexPipeline::SetSearchTestHooks(SearchTestHooks hooks) {
IndexPipeline.cpp:472: void IndexPipeline::SetReadSessionFaultInjector(ffindexstore::ReadSessionFaultInjector injector) {
IndexPipeline.cpp:478: void IndexPipeline::SetSecondaryPruningFaultInjector(SecondaryPruningFaultInjector injector) {
IndexPipeline.cpp:483: bool IndexPipeline::ShouldInjectSecondaryPruningFailureLocked(
IndexPipeline.cpp:491: IndexPipeline::ReadPlaneDiagnostics IndexPipeline::GetReadPlaneDiagnostics() const noexcept {
IndexPipeline.cpp:514: void IndexPipeline::SetBulkScanActive(const bool active) {
IndexPipeline.cpp:520: void IndexPipeline::RunStoreMaintenance(bool userIdle, uint64_t vacuumIntervalHours,
IndexPipeline.cpp:536: void IndexPipeline::MaybeAnalyzeAfterBulkInsertLocked(size_t recordCount) {
IndexPipeline.cpp:559: void IndexPipeline::MaybeReportProjectionMemoryPressureLocked() {
IndexPipeline.cpp:572: void IndexPipeline::RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt) {
IndexPipeline.cpp:657: std::optional<ffindexstore::VolumeMetadata> IndexPipeline::GetVolumeMetadata(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:662: std::vector<ffindexstore::VolumeMetadata> IndexPipeline::GetAllVolumes() {
IndexPipeline.cpp:667: void IndexPipeline::SetVolumeRules(ffindexstore::VolumeRowId volumeId, const ffprotocol::VolumeSetting& setting) {
IndexPipeline.cpp:677: bool IndexPipeline::IsEntryIncludedLocked(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId frn) {
IndexPipeline.cpp:707: bool IndexPipeline::ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV1>& records) {
IndexPipeline.cpp:716: bool IndexPipeline::ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV2>& records) {
IndexPipeline.cpp:727: bool IndexPipeline::ApplyMftBatchChanges(ffindexstore::VolumeRowId volumeId,
IndexPipeline.cpp:807: bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& records) {
IndexPipeline.cpp:814: bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV2>& records) {
IndexPipeline.cpp:832: bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV3>& records) {
IndexPipeline.cpp:853: bool IndexPipeline::ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
IndexPipeline.cpp:945: bool IndexPipeline::SetVolumeAvailable(ffindexstore::VolumeRowId id, bool available, uint64_t nowTimestamp) {
IndexPipeline.cpp:959: bool IndexPipeline::SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor) {
IndexPipeline.cpp:966: bool IndexPipeline::MarkScanComplete(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:973: bool IndexPipeline::SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn) {
IndexPipeline.cpp:980: bool IndexPipeline::SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp) {
IndexPipeline.cpp:987: bool IndexPipeline::ForgetVolume(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:1002: void IndexPipeline::BeginReconciliationPass(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:1008: bool IndexPipeline::IsReconciliationPassActive(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:1013: void IndexPipeline::AbortReconciliationPass(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:1020: void IndexPipeline::FinishReconciliationPass(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:1165: std::map<std::wstring, ffprotocol::SnapshotDirectory> ToOwningSnapshotMap(
IndexPipeline.cpp:1201: std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshot(
IndexPipeline.cpp:1207: std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshotBounded(
IndexPipeline.cpp:1212: std::optional<ffindexstore::Projection::FolderAggregate> IndexPipeline::GetFolderAggregate(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn) {
IndexPipeline.cpp:1249: void IndexPipeline::SetProjectionMemoryBudget(const size_t maximumBytes) {
IndexPipeline.cpp:1281: bool IndexPipeline::IsVolumeEvicted(const ffindexstore::VolumeRowId volumeId) const {
IndexPipeline.cpp:1286: void IndexPipeline::UnloadVolumeFromProjection(const ffindexstore::VolumeRowId volumeId) {
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
void IndexPipeline::MaybeReportProjectionMemoryPressureLocked() {
    const bool pressure = projection_.IsMemoryPressureHigh();
    if (pressure && !projectionMemoryWarningIssued_) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                      ffprotocol::DiagnosticCode::FFDIAG_PROJECTION_MEMORY_PRESSURE,
                                      {}, {}, L"projection-memory-pressure", kErrorNotEnoughMemory,
                                      projection_.EstimatedMemoryBytes()});
        projectionMemoryWarningIssued_ = true;
    } else if (!pressure) {
        projectionMemoryWarningIssued_ = false;
    }
}
void IndexPipeline::RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt) {
    std::vector<ffindexstore::VolumeRowId> volumeIds;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& meta : store_.GetAllVolumes()) {
            bool rebuildSucceeded =
                projection_.RebuildVolumeFromStore(store_, meta.rowId, meta.entryCount);
            // zero-touch-autonomous-engineering (subtree gating): after the
            // projection is rebuilt from the durable store (parent chains
            // fully present), prune any excluded subtrees from store +
            // projection so the rebuilt index honors the configured rules.
            // This mirrors the ingestion prune and the reconciliation catch-
            // all, ensuring rules are honored on every equivalent ingestion
            // path (initial scan, USN deltas, reconciliation, rebuild).
            if (rebuildSucceeded && volumeRules_.find(meta.rowId) != volumeRules_.end()) {
                std::vector<ffindexstore::EntryChange> ruleExcluded;
                const bool ruleReadSucceeded = store_.ForEachEntry(meta.rowId, [&](const ffindexstore::EntryRecord& record) {
                    if (!IsEntryIncludedLocked(meta.rowId, record.id)) {
                        ruleExcluded.push_back({ffindexstore::EntryChangeKind::Remove, record});
                    }
                });
                if (!ruleReadSucceeded) {
                    // completion-integrity-hardening-and-release-certification 4.1.2:
                    // a failed second durable read cannot be treated as a
                    // complete rule-prune pass, even if it yielded a prefix.
                    rebuildSucceeded = false;
                } else if (!ruleExcluded.empty()) {
#ifdef FF_INDEX_PIPELINE_TESTING
                    const bool injectedFailure = ShouldInjectSecondaryPruningFailureLocked(
                        SecondaryPruningBoundary::RebuildRulePrune, meta.rowId, ruleExcluded);
#else
                    const bool injectedFailure = false;
#endif
                    bool secondaryApplied = false;
                    if (!injectedFailure) {
                        secondaryApplied = store_.ApplyBatch(meta.rowId, ruleExcluded);
                    }
                    if (secondaryApplied) {
                        std::vector<ffindexstore::FileId> excludedIds;
                        excludedIds.reserve(ruleExcluded.size());
                        for (const auto& removal : ruleExcluded) {
                            excludedIds.push_back(removal.record.id);
                        }
                        // large-index-lifecycle-and-search-scalability 4.4:
                        // one bulk accelerator update instead of per-entry
                        // sorted-vector posting erases on mass rule removal.
                        projection_.RemoveBatch(meta.rowId, excludedIds);
                    } else {
                        // The projection was rebuilt from the unchanged durable
                        // rows, so retaining them is consistent. Do not publish
                        // this volume as a successful rebuild; the next rebuild
                        // will re-evaluate the same derived rule obligation.
                        rebuildSucceeded = false;
                    }
                }
            }
            if (rebuildSucceeded) {
                volumeIds.push_back(meta.rowId);
            }
        }
        MaybeReportProjectionMemoryPressureLocked();
        for (const auto volumeId : volumeIds) {
            AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::Rebuild);
        }
    }
    // Invoked outside the lock: a rebuild-completion callback typically
    // publishes a snapshot, which may itself call back into
    // ExportDirectorySnapshot -- avoids a self-deadlock on mutex_.
    for (auto id : volumeIds) {
        if (onVolumeRebuilt) {
            onVolumeRebuilt(id);
        }
    }
}
ffindexstore::VolumeRowId IndexPipeline::ResolveVolume(const ffindexstore::VolumeKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool created = false;
    auto id = store_.GetOrCreateVolume(key, &created);
    if (id.has_value() && created) {
        AdvanceQueryVisibleEpochLocked(*id, QueryMutationReason::VolumeConfiguration, true);
    }
    return id.value_or(0);
}
std::optional<ffindexstore::VolumeMetadata> IndexPipeline::GetVolumeMetadata(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.GetVolumeMetadata(id);
}
std::vector<ffindexstore::VolumeMetadata> IndexPipeline::GetAllVolumes() {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.GetAllVolumes();
}
void IndexPipeline::SetVolumeRules(ffindexstore::VolumeRowId volumeId, const ffprotocol::VolumeSetting& setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto previous = volumeRules_.find(volumeId);
    if (previous != volumeRules_.end() && VolumeSettingsEqual(previous->second, setting)) {
        return;
    }
    volumeRules_[volumeId] = setting;
    AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::VolumeConfiguration, true);
}
bool IndexPipeline::IsEntryIncludedLocked(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId frn) {
    const auto ruleIt = volumeRules_.find(volumeId);
    if (ruleIt == volumeRules_.end()) {
        return true; // no rules configured for this volume -> include everything
    }
    const auto& setting = ruleIt->second;
    // No rules and enabled -> include everything (preserves pre-change behavior).
    if (setting.rules.empty()) {
        return setting.enabled;
    }
    // Reconstruct the canonical path from the FRN parent chain. The path comes
    // back as a backslash-joined name string WITHOUT the drive-letter prefix
    // (ExportDirectorySnapshot prepends rootPathPrefix separately); IsPathIncluded
    // matches against full canonical paths like "C:\Work", so prepend the
    // volume's key (e.g. "C:") + a separator.
    const auto pathResult = projection_.ReconstructPath(volumeId, frn);
    if (!pathResult.reachedRoot) {
        // Parent chain not yet resolvable (e.g. parent arrives in a later
        // batch). Defer: include for now and let the reconciliation pass
        // decide once the chain resolves -- never silently drop an undecided
        // record (design.md "decide-on-reconciliation fallback").
        return true;
    }
    std::wstring prefix = setting.key;
    if (!prefix.empty() && prefix.back() != L'\\') {
        prefix.push_back(L'\\');
    }
    std::wstring canonical = prefix + ToWString(pathResult.path);
    return ffprotocol::IsPathIncluded(setting, canonical);
}
bool IndexPipeline::ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV1>& records) {
    std::vector<ffindexstore::EntryChange> changes;
    changes.reserve(records.size());
    for (const auto& record : records) {
        changes.push_back({ffindexstore::EntryChangeKind::Upsert, ToEntryRecord(record)});
    }
    return ApplyMftBatchChanges(volumeId, std::move(changes));
}
bool IndexPipeline::ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV2>& records) {
    // allocated-size-storage-accuracy 5.3: identical to the V1 path; only
    // the record conversion differs (allocated size authoritative).
    std::vector<ffindexstore::EntryChange> changes;
    changes.reserve(records.size());
    for (const auto& record : records) {
        changes.push_back({ffindexstore::EntryChangeKind::Upsert, ToEntryRecord(record)});
    }
    return ApplyMftBatchChanges(volumeId, std::move(changes));
}
bool IndexPipeline::ApplyMftBatchChanges(ffindexstore::VolumeRowId volumeId,
                                         std::vector<ffindexstore::EntryChange>&& changes) {
    std::lock_guard<std::mutex> lock(mutex_);
    // design.md D4: commit to the durable store first; the projection is
    // only touched once that commit has actually succeeded.
    if (!store_.ApplyBatch(volumeId, changes)) {
        return false;
    }
    MaybeAnalyzeAfterBulkInsertLocked(changes.size());
    for (const auto& change : changes) {
        projection_.Upsert(volumeId, change.record);
    }
    MaybeReportProjectionMemoryPressureLocked();
    // Optimization: fuse the two rule-evaluation passes and cache the
    // inclusion result per record so each entry's ReconstructPath walk
    // runs at most once. When no rules are configured for this volume the
    // gating is a no-op (preserves pre-change behavior without any walks).
    const bool hasRulesForVolume = volumeRules_.find(volumeId) != volumeRules_.end();
    auto reconciling = reconciliationSeen_.find(volumeId);
    std::vector<ffindexstore::EntryChange> ruleExcluded;
    if (hasRulesForVolume) {
        ruleExcluded.reserve(changes.size() / 8 + 1);
        for (const auto& change : changes) {
            const bool included = IsEntryIncludedLocked(volumeId, change.record.id);
            if (reconciling != reconciliationSeen_.end() && included) {
                reconciling->second.insert(change.record.id);
            }
            if (!included) {
                ruleExcluded.push_back({ffindexstore::EntryChangeKind::Remove, change.record});
            }
        }
    } else if (reconciling != reconciliationSeen_.end()) {
        for (const auto& change : changes) {
            reconciling->second.insert(change.record.id);
        }
    }
    if (!ruleExcluded.empty()) {
#ifdef FF_INDEX_PIPELINE_TESTING
        const bool injectedFailure = ShouldInjectSecondaryPruningFailureLocked(
            SecondaryPruningBoundary::MftRulePrune, volumeId, ruleExcluded);
#else
        const bool injectedFailure = false;
#endif
        bool secondaryApplied = false;
        if (!injectedFailure) {
            secondaryApplied = store_.ApplyBatch(volumeId, ruleExcluded);
        }
        if (secondaryApplied) {
            std::vector<ffindexstore::FileId> excludedIds;
            excludedIds.reserve(ruleExcluded.size());
            for (const auto& removal : ruleExcluded) {
                excludedIds.push_back(removal.record.id);
            }
            // large-index-lifecycle-and-search-scalability 4.4: one bulk
            // accelerator update instead of per-entry sorted-vector posting
            // erases on mass rule removal.
            projection_.RemoveBatch(volumeId, excludedIds);
        } else {
            // Secondary pruning failed: durable and projection remain consistent (both retain the excluded rows),
            // but do not report success. Caller can retry the batch; the next attempt will re-evaluate pruning.
            if (projection_.EnforceMemoryLimitNow()) {
                AdvanceResidencyEpochsForStructuralChangeLocked();
            }
            MaybeReportProjectionMemoryPressureLocked();
            return false;
        }
    }
    // invoking it from Projection::Upsert turns a large volume scan into an
    // O(N) compaction storm once the bounded memory policy is reached.
    if (projection_.EnforceMemoryLimitNow()) {
        AdvanceResidencyEpochsForStructuralChangeLocked();
    }
    MaybeReportProjectionMemoryPressureLocked();
    if (!changes.empty()) {
        AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::EntryMutation);
    }
    return true;
}
bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& records) {
    std::vector<ffprotocol::UsnDeltaV2> upgraded;
    upgraded.reserve(records.size());
    for (const auto& record : records) upgraded.push_back(ffprotocol::UpgradeUsnDeltaV1(record));
    return ApplyUsnBatch(volumeId, upgraded);
}
bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV2>& records) {
    std::vector<ffindexstore::EntryChange> changes;
    changes.reserve(records.size());
    for (const auto& record : records) {
        ffindexstore::EntryChange change;
        change.kind = (record.fixed.reason & kUsnReasonFileDelete) != 0 ? ffindexstore::EntryChangeKind::Remove
                                                                         : ffindexstore::EntryChangeKind::Upsert;
        change.record = ToEntryRecord(record);
        if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
            const auto fields = ToEntryFieldMask(record.fixed.fieldMask);
            if (!fields) return false;
            change.fields = *fields;
        }
        changes.push_back(std::move(change));
    }
    return ApplyUsnBatchChanges(volumeId, std::move(changes));
}
bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV3>& records) {
    // allocated-size-storage-accuracy 5.3: identical to the V2 path; only
    // the record conversion differs (allocated-size presence bit maps to
    // kEntryFieldAllocatedSizeBytes, so absence preserves stored values).
    std::vector<ffindexstore::EntryChange> changes;
    changes.reserve(records.size());
    for (const auto& record : records) {
        ffindexstore::EntryChange change;
        change.kind = (record.fixed.reason & kUsnReasonFileDelete) != 0 ? ffindexstore::EntryChangeKind::Remove
                                                                         : ffindexstore::EntryChangeKind::Upsert;
        change.record = ToEntryRecord(record);
        if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
            const auto fields = ToEntryFieldMask(record.fixed.fieldMask);
            if (!fields) return false;
            change.fields = *fields;
        }
        changes.push_back(std::move(change));
    }
    return ApplyUsnBatchChanges(volumeId, std::move(changes));
}
bool IndexPipeline::ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
                                         std::vector<ffindexstore::EntryChange>&& changes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!store_.ApplyBatch(volumeId, changes)) {
        return false;
    }
    size_t upsertCount = 0;
    for (const auto& change : changes) {
        if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
            ++upsertCount;
        }
    }
    MaybeAnalyzeAfterBulkInsertLocked(upsertCount);
    for (const auto& change : changes) {
        if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
            projection_.UpsertPartial(volumeId, change.record, change.fields);
        } else {
            projection_.Remove(volumeId, change.record.id);
        }
    }
    // Optimization: same fused single-walk evaluation as the MFT
    // path, but only Upsert USN records are gated (deletes are no-ops).
    // Skips all ReconstructPath walks when no rules exist for the volume.
    const bool hasRulesForUsnVolume = volumeRules_.find(volumeId) != volumeRules_.end();
    // index-durability-false-success-closure D3: while a reconciliation
    // pass is running, journal-batch upserts are "observed" exactly like
    // scan records -- otherwise a file created between the pass's scan
    // start and Finish is unseen, removed as stale, and never re-observed
    // (its USN event was already consumed). Rule-excluded upserts stay out
    // of the seen-set, matching the MFT path so Finish still prunes them.
    auto reconcilingUsn = reconciliationSeen_.find(volumeId);
    std::vector<ffindexstore::EntryChange> ruleExcluded;
    if (hasRulesForUsnVolume) {
        ruleExcluded.reserve(changes.size() / 8 + 1);
        for (const auto& change : changes) {
            if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
                if (IsEntryIncludedLocked(volumeId, change.record.id)) {
                    if (reconcilingUsn != reconciliationSeen_.end()) {
                        reconcilingUsn->second.insert(change.record.id);
                    }
                } else {
                    ruleExcluded.push_back({ffindexstore::EntryChangeKind::Remove, change.record});
                }
            }
        }
    } else if (reconcilingUsn != reconciliationSeen_.end()) {
        for (const auto& change : changes) {
            if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
                reconcilingUsn->second.insert(change.record.id);
            }
        }
    }
    if (!ruleExcluded.empty()) {
#ifdef FF_INDEX_PIPELINE_TESTING
        const bool injectedFailure = ShouldInjectSecondaryPruningFailureLocked(
            SecondaryPruningBoundary::UsnRulePrune, volumeId, ruleExcluded);
#else
        const bool injectedFailure = false;
#endif
        bool secondaryApplied = false;
        if (!injectedFailure) {
            secondaryApplied = store_.ApplyBatch(volumeId, ruleExcluded);
        }
        if (secondaryApplied) {
            std::vector<ffindexstore::FileId> excludedIds;
            excludedIds.reserve(ruleExcluded.size());
            for (const auto& removal : ruleExcluded) {
                excludedIds.push_back(removal.record.id);
            }
            // large-index-lifecycle-and-search-scalability 4.4: one bulk
            // accelerator update instead of per-entry sorted-vector posting
            // erases on mass rule removal.
            projection_.RemoveBatch(volumeId, excludedIds);
        } else {
            if (projection_.EnforceMemoryLimitNow()) {
                AdvanceResidencyEpochsForStructuralChangeLocked();
            }
            MaybeReportProjectionMemoryPressureLocked();
            return false;
        }
    }
    if (projection_.EnforceMemoryLimitNow()) {
        AdvanceResidencyEpochsForStructuralChangeLocked();
    }
    MaybeReportProjectionMemoryPressureLocked();
    if (!changes.empty()) {
        AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::EntryMutation);
    }
    return true;
}
bool IndexPipeline::SetVolumeAvailable(ffindexstore::VolumeRowId id, bool available, uint64_t nowTimestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool visibilityChanged = false;
    const bool result = store_.SetVolumeAvailable(id, available, nowTimestamp, &visibilityChanged);
    if (result) {
        if (visibilityChanged) {
            AdvanceQueryVisibleEpochLocked(id, QueryMutationReason::VolumeVisibility, true);
        } else {
            RecordMetadataOnlyUpdateLocked();
        }
    }
    return result;
}
bool IndexPipeline::SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.SetScanCursor(id, cursor);
    if (result) RecordMetadataOnlyUpdateLocked();
    return result;
}
bool IndexPipeline::MarkScanComplete(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.MarkScanComplete(id);
    if (result) RecordMetadataOnlyUpdateLocked();
    return result;
}
bool IndexPipeline::SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.SetJournalPosition(id, journalId, resumeUsn);
    if (result) RecordMetadataOnlyUpdateLocked();
    return result;
}
bool IndexPipeline::SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.SetLastReconciliationTime(id, nowTimestamp);
    if (result) RecordMetadataOnlyUpdateLocked();
    return result;
}
bool IndexPipeline::ForgetVolume(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!store_.ForgetVolume(id)) {
        return false;
    }
    reconciliationSeen_.erase(id);
    // Same commit-before-apply ordering as ingestion (design.md D4): the
    // projection's copy of this volume's entries is only dropped once the
    // durable rows are actually gone, so no stale entries keep being
    // exported/published for a forgotten volume.
    projection_.RemoveVolume(id);
    AdvanceQueryVisibleEpochLocked(id, QueryMutationReason::Forget, true);
    return true;
}
void IndexPipeline::BeginReconciliationPass(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    reconciliationSeen_[id].clear();
    RecordMetadataOnlyUpdateLocked();
}
bool IndexPipeline::IsReconciliationPassActive(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return reconciliationSeen_.find(id) != reconciliationSeen_.end();
}
void IndexPipeline::AbortReconciliationPass(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reconciliationSeen_.erase(id) != 0) {
        RecordMetadataOnlyUpdateLocked();
    }
}
void IndexPipeline::FinishReconciliationPass(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = reconciliationSeen_.find(id);
    if (it == reconciliationSeen_.end()) {
        return;
    }
    const auto& seen = it->second;
    const bool hasRulesForReconcile = volumeRules_.find(id) != volumeRules_.end();
    std::vector<ffindexstore::EntryChange> removals;
    for (const auto& existingId : store_.ListEntryIds(id)) {
        const bool notSeen = seen.find(existingId) == seen.end();
        const bool excluded = hasRulesForReconcile && !IsEntryIncludedLocked(id, existingId);
        if (notSeen || excluded) {
            ffindexstore::EntryRecord marker;
            marker.id = existingId;
            removals.push_back({ffindexstore::EntryChangeKind::Remove, marker});
        }
    }
    bool searchableMutation = false;
    bool secondaryApplied = true;
    if (!removals.empty()) {
#ifdef FF_INDEX_PIPELINE_TESTING
        const bool injectedFailure = ShouldInjectSecondaryPruningFailureLocked(
            SecondaryPruningBoundary::ReconciliationRemoval, id, removals);
#else
        const bool injectedFailure = false;
#endif
        if (!injectedFailure) {
            secondaryApplied = store_.ApplyBatch(id, removals);
        } else {
            secondaryApplied = false;
        }
        if (secondaryApplied) {
            std::vector<ffindexstore::FileId> removedIds;
            removedIds.reserve(removals.size());
            for (const auto& removal : removals) {
                removedIds.push_back(removal.record.id);
[1146 more lines in file. Use offset=1059 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/M...
            }
            // large-index-lifecycle-and-search-scalability 4.4: reconciliation
            // mass removal is the same bulk case as rule pruning.
            projection_.RemoveBatch(id, removedIds);
            searchableMutation = true;
        }
    }
    if (secondaryApplied || removals.empty()) {
        reconciliationSeen_.erase(it);
    } else {
        // Secondary durable removal failed: retain the current pass's obligation
        // so the next reconciliation or retry still owes the same deletions.
        // Do not advance searchable epoch; the failure is not a searchable mutation.
        searchableMutation = false;
    }
    if (searchableMutation) {
        AdvanceQueryVisibleEpochLocked(id, QueryMutationReason::EntryMutation);
    } else {
        RecordMetadataOnlyUpdateLocked();
    }
}
ffprotocol::PooledSnapshotMap IndexPipeline::ExportDirectorySnapshotBoundedPooled(
    ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix, size_t maxDirectories) {
    if (maxDirectories == 0) {
        return {};
    }
    // Bounded publication keeps only the lexicographically smallest N
    // directories (shallowest/top-of-tree) deterministically. To avoid
    // building the full N=200k map then discarding the tail, insert with
    // an in-flight bound: each insertion that would exceed maxDirectories
    // evicts the current largest key (map's rbegin), so final size never
    // exceeds maxDirectories and total work is O(D log max) not O(D log D).
    //
    // performance-closure-and-multisurface-acceleration 6.1: this is the
    // single export core. Entry names land in per-directory pools
    // (PooledSnapshotDirectoryBuilder) instead of per-entry owning
    // std::wstring allocations; the owning exports below are converting
    // wrappers so the traversal and the bounded-eviction semantics exist
    // exactly once.
    std::lock_guard<std::mutex> lock(mutex_);
    // large-index-lifecycle-and-search-scalability review-lane D P1-1: the
    // reload (and its trailing enforcement) is a structural change -- bump
    // residency epochs so in-flight searches re-capture instead of reading a
    // replaced layout.
    const bool wasCold = projection_.NeedsReload(id);
    const bool reloaded = projection_.ReloadEvictedVolume(store_, id);
    if (wasCold) {
        AdvanceResidencyEpochsForStructuralChangeLocked();
    }
    if (!reloaded) return {};
    ffprotocol::PooledSnapshotMap result;
    projection_.ForEachEntry([&](const ffindexstore::EntryKey& key, const ffindexstore::ProjectionEntry& entry) {
        if (key.volumeRowId != id || (entry.attributes & kFileAttributeDirectory) == 0) {
            return;
        }
        auto pathResult = projection_.ReconstructPath(id, entry.frn);
        const std::wstring reconstructed = ToWString(pathResult.path);
        std::wstring fullPath;
        if (entry.parentFrn == entry.frn) {
            fullPath = rootPathPrefix;
        } else if (pathResult.reachedRoot) {
            const std::wstring relative = StripVolumeRootName(projection_, id, entry.frn, reconstructed);
            fullPath = rootPathPrefix;
            if (!relative.empty()) fullPath += L"\\" + relative;
        } else {
            fullPath = rootPathPrefix + L"\\" + reconstructed;
        }
        ffprotocol::PooledSnapshotDirectoryBuilder builder;
        builder.SetStatus(ffprotocol::DirectoryEnumerationStatus::Success);
        builder.SetIdentity(entry.volumeRowId, entry.frn.low, entry.frn.high,
                            entry.parentFrn.low, entry.parentFrn.high);
        if (std::span<const uint32_t> children = projection_.ChildIndices(id, entry.frn); !children.empty()) {
            for (uint32_t childIndex : children) {
                const auto& child = projection_.EntryAt(childIndex);
                // allocated-size-storage-accuracy 5.3/D6: carry the
                // projection's explicit known state into the snapshot.
                builder.AddEntry(
                    projection_.Names().Get(child.nameId),
                    (child.attributes & kFileAttributeDirectory) != 0,
                    child.sizeBytes,
                    child.allocatedSizeBytes,
                    child.allocatedSizeKnown,
                    child.attributes,
                    child.creationTime,
                    child.lastModifiedTime,
                    child.volumeRowId,
                    child.frn.low,
                    child.frn.high,
                    child.parentFrn.low,
                    child.parentFrn.high);
            }
        }
        result.emplace(std::move(fullPath), std::move(builder).Build());
        if (result.size() > maxDirectories) {
            auto largest = std::prev(result.end());
            result.erase(largest);
        }
    });
    return result;
}
namespace {
std::map<std::wstring, ffprotocol::SnapshotDirectory> ToOwningSnapshotMap(
    const ffprotocol::PooledSnapshotMap& pooled) {
    std::map<std::wstring, ffprotocol::SnapshotDirectory> result;
    for (const auto& [path, directory] : pooled) {
[1036 more lines in file. Use offset=1169 to continue.]
Main.cpp:63:     const std::wstring dbPathWide = dir + L"\\index.db";
Main.cpp:64:     const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, dbPathWide.c_str(), -1, nullptr, 0, nullptr, nullptr);
Main.cpp:68:     std::string dbPathUtf8(static_cast<size_t>(utf8Length), '\0');
Main.cpp:69:     const int converted = WideCharToMultiByte(CP_UTF8, 0, dbPathWide.c_str(), -1, dbPathUtf8.data(), utf8Length, nullptr, nullptr);
Main.cpp:73:     dbPathUtf8.resize(static_cast<size_t>(converted - 1));
Main.cpp:74:     return dbPathUtf8;
Main.cpp:78: // file plus its WAL/SHM siblings after a failed integrity check, so the
Main.cpp:83: void DeleteIndexDatabaseFiles(const std::string& dbPathUtf8) {
Main.cpp:84:     const int wideLength = MultiByteToWideChar(CP_UTF8, 0, dbPathUtf8.c_str(), -1, nullptr, 0);
Main.cpp:88:     std::wstring dbPathWide(static_cast<size_t>(wideLength), L'\0');
Main.cpp:89:     const int converted = MultiByteToWideChar(CP_UTF8, 0, dbPathUtf8.c_str(), -1, dbPathWide.data(), wideLength);
Main.cpp:93:     dbPathWide.resize(static_cast<size_t>(converted - 1));
Main.cpp:94:     DeleteFileW(dbPathWide.c_str());
Main.cpp:95:     DeleteFileW((dbPathWide + L"-wal").c_str());
Main.cpp:96:     DeleteFileW((dbPathWide + L"-shm").c_str());
Main.cpp:164:     const std::string dbPathUtf8 = GetIndexDatabasePathUtf8();
Main.cpp:165:     if (!dbPathUtf8.empty()) {
Main.cpp:168:         // verdict. A failed open whose integrity check could not reach a
Main.cpp:181:             bool integrityFailed = false;
Main.cpp:182:             opened = indexPipeline.Open(dbPathUtf8, &integrityFailed);
Main.cpp:183:             if (!opened && integrityFailed) {
Main.cpp:188:                 // its integrity check is never trusted -- delete it (plus its
Main.cpp:193:                               L"FastFilesEngine: index database at %hs failed its integrity check -- deleting it and rebuilding from a fresh scan\n",
Main.cpp:194:                               dbPathUtf8.c_str());
Main.cpp:196:                 DeleteIndexDatabaseFiles(dbPathUtf8);
Main.cpp:197:                 opened = indexPipeline.Open(dbPathUtf8);
Main.cpp:211:             // integrity, or if the delete-and-reopen fallback itself
Main.cpp:217:                           dbPathUtf8.c_str());
Main.cpp:288:     indexPipeline.RebuildAll([&uiServer, &indexPipeline](ffindexstore::VolumeRowId volumeId) {
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/M...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
        return 1;
    }
    // Task 4.1: idempotently (re-)ensure the per-user Scheduled Task
    // registration is current -- best-effort; the installer is the
    // primary place this happens (task 6.1), but self-healing here means
    // a manually-copied or repaired install still ends up registered.
    // Keep this after UiServer::Start: Task Scheduler COM registration can
    // take several seconds on a cold desktop, and the UI control pipe must
    // become reachable immediately so the application does not spend its
    // bounded connection window retrying a helper that is already running.
    ffsetup::RegisterEngineScheduledTask(ownPath);
    ffengine::IndexPipeline indexPipeline;
    const std::string dbPathUtf8 = GetIndexDatabasePathUtf8();
    if (!dbPathUtf8.empty()) {
        // index-durability-false-success-closure D6 (PA-006): the
        // delete-and-rebuild fallback is reserved for a *proven* corrupt
        // verdict. A failed open whose integrity check could not reach a
        // verdict -- an exclusive lock held by a lingering process, an
        // I/O hiccup, BUSY/LOCKED/NOMEM/IOERR -- is retried a bounded
        // number of times with the file preserved; destroying a healthy
        // index because a check was inconclusive is the failure this
        // closes. Corruption remains fatal exactly as before (task 1.7).
        constexpr int kOpenAttempts = 3;
        constexpr DWORD kOpenRetryDelayMs = 500;
        bool opened = false;
        for (int attempt = 0; attempt < kOpenAttempts && !opened; ++attempt) {
            if (attempt > 0) {
                Sleep(kOpenRetryDelayMs);
            }
            bool integrityFailed = false;
            opened = indexPipeline.Open(dbPathUtf8, &integrityFailed);
            if (!opened && integrityFailed) {
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::DatabaseError,
                                              ffprotocol::DiagnosticCode::FFDIAG_CORRUPT_DB,
                                              {}, {}, L"index-database-corrupt", ERROR_INVALID_DATA, 0});
                // Task 1.7's defined fallback: an existing database that fails
                // its integrity check is never trusted -- delete it (plus its
                // WAL/SHM siblings) and reopen a fresh, empty store, which the
                // normal scan/reconciliation machinery then rebuilds from a
                // fresh scan.
                std::fwprintf(stderr,
                              L"FastFilesEngine: index database at %hs failed its integrity check -- deleting it and rebuilding from a fresh scan\n",
                              dbPathUtf8.c_str());
                indexPipeline.Close();
                DeleteIndexDatabaseFiles(dbPathUtf8);
                opened = indexPipeline.Open(dbPathUtf8);
                break; // the fresh-reopen result is terminal: never delete again
            }
            if (!opened && attempt + 1 < kOpenAttempts) {
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::DatabaseError,
                                              ffprotocol::DiagnosticCode::FFDIAG_DB_INTEGRITY_INCONCLUSIVE,
                                              {}, {}, L"index-database-open-inconclusive-retry", ERROR_RETRY, 0});
            }
        }
        if (!opened) {
            ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::DatabaseError,
                                          ffprotocol::DiagnosticCode::FFDIAG_DATABASE_OPEN_FAILED,
                                          {}, {}, L"index-database-open-failed", ERROR_OPEN_FAILED, 0});
            // An unopenable durable store (for reasons other than
            // integrity, or if the delete-and-reopen fallback itself
            // failed) is not fatal to the process -- the engine still
            // serves degraded-mode browsing; scanning simply has nowhere
            // durable to persist to until the next restart finds a
            // healthy database (e.g. after the file is manually removed).
            std::fwprintf(stderr, L"FastFilesEngine: failed to open index database at %hs -- indexing disabled this session\n",
                          dbPathUtf8.c_str());
        }
    }
    ffengine::IdleManager idleManager;
    ffengine::PrivilegedConnection privilegedConnection;
    ffengine::VolumeSessionManager volumeSessions(indexPipeline, privilegedConnection);
    // The engine owns applying indexing decisions. Reload is notify-then-pull:
    // settings.json is atomically written by the UI before this message.
    uiServer.onReloadIndexingConfig = [&volumeSessions] {
        volumeSessions.ReloadConfiguration(ffprotocol::LoadSettings(false).indexing);
    };
    uiServer.onRequestUnavailableVolumes = [&indexPipeline] {
        std::vector<ffprotocol::UnavailableVolumeRecord> records;
        for (const auto& metadata : indexPipeline.GetAllVolumes()) {
            if (metadata.available) {
                continue;
            }
            ffprotocol::UnavailableVolumeRecord record{};
            record.volumeRowId = metadata.rowId;
            std::memcpy(record.volumeGuid, metadata.key.volumeGuid.data(), metadata.key.volumeGuid.size());
            record.serialNumber = metadata.key.serialNumber;
            record.entryCount = metadata.entryCount;
            records.push_back(record);
        }
        return records;
    };
    uiServer.onForgetUnavailableVolume = [&indexPipeline](int64_t volumeRowId) {
        const auto metadata = indexPipeline.GetVolumeMetadata(volumeRowId);
        if (!metadata) {
            return ffprotocol::ForgetUnavailableVolumeStatus::NotFound;
        }
        if (metadata->available) {
            return ffprotocol::ForgetUnavailableVolumeStatus::VolumeAvailable;
        }
        return indexPipeline.ForgetVolume(volumeRowId)
            ? ffprotocol::ForgetUnavailableVolumeStatus::Removed
            : ffprotocol::ForgetUnavailableVolumeStatus::Failed;
    };
    uiServer.onRequestFolderAggregate = [&indexPipeline](ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn) -> std::optional<ffindexstore::Projection::FolderAggregate> {
        return indexPipeline.GetFolderAggregate(volumeId, parentFrn);
    };
    uiServer.onRequestIndexedSearch = [&indexPipeline](const ffprotocol::IndexedSearchRequest& request,
                                                       const ffengine::UiServer::SearchCancellation& cancelled) {
        return indexPipeline.SearchIndexed(request, nullptr, cancelled);
    };
    uiServer.onRequestVolumeStatus = [&volumeSessions] {
        return volumeSessions.CollectVolumeStatus();
    };
    uiServer.onSetIndexingPaused = [&volumeSessions](uint8_t scope, bool paused) {
        volumeSessions.SetIndexingPaused(scope, paused);
    };
    const auto initialSettings = ffprotocol::LoadSettings(false);
    volumeSessions.ReloadConfiguration(initialSettings.indexing);
    uiServer.onActivity = [&idleManager] { idleManager.NotifyActivity(); };
    volumeSessions.SetSnapshotReadyCallback([&uiServer](ffindexstore::VolumeRowId,
                                                          ffprotocol::PooledSnapshotMap directories) {
        uiServer.MergeIndexDirectories(std::move(directories));
    });
    volumeSessions.SetVolumeUnavailableCallback(
        [&uiServer](ffindexstore::VolumeRowId, wchar_t driveLetter) { uiServer.RemoveVolumeDirectories(driveLetter); });
    volumeSessions.Start();
    // task 3.1/3.2: rebuild every already-known volume from the durable
    // store before the privileged connection even comes up, so a UI
    // window opened immediately at launch still sees whatever was
    // already persisted from a prior session, per volume, as soon as
    // that volume's rebuild completes.
    indexPipeline.RebuildAll([&uiServer, &indexPipeline](ffindexstore::VolumeRowId volumeId) {
        auto meta = indexPipeline.GetVolumeMetadata(volumeId);
        if (!meta || !meta->available) {
            return;
        }
        if (!meta->scanComplete) {
            // close-independent-validation-gaps 4.6: an interrupted initial
            // scan is rebuilt into the projection for durable correctness,
            // but its complete snapshot export is deferred. Exporting a
            // large incomplete volume synchronously here would block the
            // privileged connection from starting and delay cursor resume;
            // VolumeSessionManager forces the current view at ScanComplete.
[59 more lines in file. Use offset=300 to continue.]
Projection.h:44:     // CandidateView::nameIsAscii in ffsearch for the equivalence argument.
Projection.h:177:         size_t nameHashPayloadBytes = 0;        // bucket candidate-vector capacities (unmodeled by the estimator)
Projection.h:181:         size_t filenameIndexDistinctTrigrams = 0;
Projection.h:271:     // keepFilenameIndexCurrent rebuilds the accelerator once after a bulk
Projection.h:279:                      bool keepFilenameIndexCurrent = true);
Projection.h:288:     void RemoveVolume(VolumeRowId volumeRowId, bool keepFilenameIndexCurrent = true);
Projection.h:302:         SkipFilenameMaintenance = 1,
Projection.h:314:     // LONG S vs 's'), so the candidate-completeness machinery for the
Projection.h:327:     // Returns a conservative candidate set for an already ASCII-folded
Projection.h:330:     // The candidate set is a CONSERVATIVE SUPERSET of the rows the canonical
Projection.h:337:     std::optional<std::vector<uint32_t>> FilenameCandidateIndices(
Projection.h:338:         std::u16string_view foldedNeedle, size_t maximumCandidates = 0) const;
Projection.h:474:     void RebuildFilenameIndex();
Projection.h:478:     void AddFilenameIndexEntry(uint32_t index);
Projection.h:479:     void RemoveFilenameIndexEntry(uint32_t index, std::u16string_view name);
Projection.h:480:     void InvalidateFilenameIndex() noexcept;
Projection.h:488:     std::unordered_map<uint64_t, std::vector<uint32_t>> filenameTrigramPostings_;
Projection.h:492:     // every ASCII candidate set so the superset invariant holds.
grep: C:/Users/Michael Roy/Documents/FastFiles/tests
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
setup/test_package_trust.cpp:63:         file << "FASTFILES_PACKAGE_MANIFEST_V2\nschema=2\nproductName=FastFiles\n"
setup/PackageTrustFixtureSupport.h:60:     manifest.schemaVersion = ffsetup::kPackageManifestSchemaV2;
engine/test_volume_session_manager.cpp:728: // scan-generation-integrity-and-residual-certification-closure 10.2: a
engine/test_volume_session_manager.cpp:1943: // scan-generation-integrity-and-residual-certification-closure 2.1: the order
engine/test_volume_session_manager.cpp:2061: // scan-generation-integrity-and-residual-certification-closure 10.2: a
engine/test_volume_session_manager.cpp:2136: // scan-generation-integrity-and-residual-certification-closure 3.4: a staged
engine/test_volume_session_manager.cpp:2242: // scan-generation-integrity-and-residual-certification-closure 3.4: a false
engine/test_volume_session_manager.cpp:2404: // scan-generation-integrity-and-residual-certification-closure 10.2: retiring
ui/CMakeLists.txt:176: # completion-integrity-hardening-and-release-certification: a clipboard
ui/CMakeLists.txt:238: # completion-integrity-hardening-and-release-certification 4.5.2: Filters
ui/CMakeLists.txt:244: # completion-integrity-hardening-and-release-certification 4.5.2: Search
engine/test_seeded_indexed_mode.cpp:241:           "an unavailable volume stays queryable (retention-without-corruption)");
engine/bench_first_generation_publish.cpp:520:     output << "{\n  \"schema\":\"fastfiles-first-generation-publish-v1\",\n"
engine/bench_large_index_scalability.cpp:2034:     output << "{\n  \"schema\":\"fastfiles-projection-memory-decomposition-v1\"\n"
engine/bench_large_index_scalability.cpp:2380:     output << "{\n  \"schema\":\"fastfiles-large-index-scalability-v2\",\n  \"machine\":";
drift/DocTruth.psm1:205:     if ([int]$json.schemaVersion -ne 2) {
drift/DocTruth.psm1:206:         Write-AuthorityError -Seam 'openspec/lifecycle.json' -Why 'unsupported schemaVersion'
engine/bench_allocation_isolation.cpp:849:     std::fprintf(out, "{\n \"schema\": \"fastfiles-allocation-isolation-v1\",\n");
engine/bench_secondary_filename_index.cpp:828:     output << "{\"schema\":\"fastfiles-secondary-filename-index-v1\",\"sqlite_fts_capability\":";
indexstore/bench_rebuild_isolation.cpp:640:     output << "  \"schema\": \"large-index-rebuild-isolation-1\",\n";
drift/check_doc_drift.ps1:16: #      worktree file indicates line-ending corruption or a partial conversion.
indexstore/test_projection.cpp:441: // completion-integrity-hardening-and-release-certification 4.1.2: a durable
indexstore/test_store.cpp:33: // Builds a schema-version-1 database exactly as the pre-allocated-size
indexstore/test_store.cpp:35: // user_version=1), including one row, so Store::Open's in-place migration
indexstore/test_store.cpp:42:     const char* schemaV1 =
indexstore/test_store.cpp:73:         "PRAGMA user_version=1;";
indexstore/test_store.cpp:74:     const bool ok = sqlite3_exec(db, schemaV1, nullptr, nullptr, nullptr) == SQLITE_OK;
indexstore/test_store.cpp:82: void TestSchemaV1ToV2MigrationInPlace() {
indexstore/test_store.cpp:87:     bool integrityFailed = false;
indexstore/test_store.cpp:88:     Check(store.Open(dbPath, &integrityFailed), "a version-1 database opens and migrates in place");
indexstore/test_store.cpp:89:     Check(!integrityFailed, "integrity check passes before/after migration");
indexstore/test_store.cpp:90:     Check(store.RunIntegrityCheck(), "post-migration integrity check passes");
indexstore/test_store.cpp:189: void TestOpenCreatesSchemaAndIsReopenable() {
indexstore/test_store.cpp:192:     bool integrityFailed = true;
indexstore/test_store.cpp:193:     Check(store.Open(dbPath, &integrityFailed), "fresh database opens successfully");
indexstore/test_store.cpp:194:     Check(!integrityFailed, "fresh database passes integrity check trivially");
indexstore/test_store.cpp:195:     Check(store.RunIntegrityCheck(), "integrity check passes standalone");
indexstore/test_store.cpp:442:     Check(reopened.RunIntegrityCheck(), "the recovered database passes an integrity check");
indexstore/test_store.cpp:866:         Check(reopened.RunIntegrityCheck(),
indexstore/test_store.cpp:867:               (label + ": integrity check passes after reopen").c_str());
indexstore/test_store.cpp:889: // index-durability-false-success-closure 5.4 (PA-006): the corrupt-vs-
indexstore/test_store.cpp:891: // corruption verdicts may ever qualify a database for the caller's
indexstore/test_store.cpp:893: void TestIntegrityVerdictClassifier() {
indexstore/test_store.cpp:894:     using Verdict = Store::IntegrityVerdict;
indexstore/test_store.cpp:895:     Check(Store::ClassifyIntegrityStep(SQLITE_CORRUPT) == Verdict::Corrupt,
indexstore/test_store.cpp:896:           "SQLITE_CORRUPT is a corruption verdict");
indexstore/test_store.cpp:897:     Check(Store::ClassifyIntegrityStep(SQLITE_NOTADB) == Verdict::Corrupt,
indexstore/test_store.cpp:898:           "SQLITE_NOTADB (not a database file) is a corruption verdict");
indexstore/test_store.cpp:899:     Check(Store::ClassifyIntegrityStep(SQLITE_CORRUPT | (2 << 8)) == Verdict::Corrupt,
indexstore/test_store.cpp:900:           "an extended corrupt code classifies with its primary family");
indexstore/test_store.cpp:901:     Check(Store::ClassifyIntegrityStep(SQLITE_BUSY) == Verdict::Inconclusive,
indexstore/test_store.cpp:902:           "a locked database is unproven, never corrupt");
indexstore/test_store.cpp:903:     Check(Store::ClassifyIntegrityStep(SQLITE_LOCKED) == Verdict::Inconclusive, "LOCKED is unproven");
indexstore/test_store.cpp:904:     Check(Store::ClassifyIntegrityStep(SQLITE_NOMEM) == Verdict::Inconclusive, "NOMEM is unproven");
indexstore/test_store.cpp:905:     Check(Store::ClassifyIntegrityStep(SQLITE_READONLY) == Verdict::Inconclusive, "READONLY is unproven");
indexstore/test_store.cpp:906:     Check(Store::ClassifyIntegrityStep(SQLITE_INTERRUPT) == Verdict::Inconclusive, "INTERRUPT is unproven");
indexstore/test_store.cpp:907:     Check(Store::ClassifyIntegrityStep(SQLITE_FULL) == Verdict::Inconclusive, "FULL is unproven");
indexstore/test_store.cpp:908:     Check(Store::ClassifyIntegrityStep(SQLITE_IOERR | (3 << 8)) == Verdict::Inconclusive,
indexstore/test_store.cpp:909:           "the extended IOERR family never claims corruption");
indexstore/test_store.cpp:910:     Check(Store::ClassifyIntegrityStep(0x5391) == Verdict::Inconclusive,
indexstore/test_store.cpp:911:           "an unrecognized result code never claims corruption -- the classifier fails safe");
indexstore/test_store.cpp:915: // open WITHOUT the integrity-failed flag -- the flag is what licenses the
indexstore/test_store.cpp:916: // engine's delete path, and "could not check" is not "checked and corrupt".
indexstore/test_store.cpp:917: void TestLockedDatabaseFailsOpenWithoutIntegrityFlag() {
indexstore/test_store.cpp:918:     const std::string dbPath = FreshDbPath("ffindexstore_test_locked_integrity.db");
indexstore/test_store.cpp:922:         Check(store.Open(dbPath), "locked-integrity fixture database opens");
indexstore/test_store.cpp:942:         bool integrityFailed = true;
indexstore/test_store.cpp:943:         Check(!victim.Open(dbPath, &integrityFailed),
indexstore/test_store.cpp:944:               "the integrity check under an exclusive lock fails the open (fail-closed)");
indexstore/test_store.cpp:945:         Check(!integrityFailed,
indexstore/test_store.cpp:946:               "a check that could not reach a verdict does NOT report corruption (PA-006)");
indexstore/test_store.cpp:947:         Check(victim.RunIntegrityCheckDetailed() == Store::IntegrityVerdict::Inconclusive,
indexstore/test_store.cpp:948:               "the failed-open store's verdict is inconclusive, never corrupt");
indexstore/test_store.cpp:956:     bool integrityFailed = true;
indexstore/test_store.cpp:957:     Check(revived.Open(dbPath, &integrityFailed), "the same database opens cleanly once the lock is gone");
indexstore/test_store.cpp:958:     Check(!integrityFailed, "the unlocked open reports a clean verdict");
indexstore/test_store.cpp:960:     Check(revived.RunIntegrityCheckDetailed() == Store::IntegrityVerdict::Ok,
indexstore/test_store.cpp:964: // The corruption half of the split: a file SQLite positively identifies as
indexstore/test_store.cpp:965: // corrupt must set outIntegrityFailed true (the fallback stays available
indexstore/test_store.cpp:968: void TestCorruptDatabaseSetsIntegrityFailedFlag() {
indexstore/test_store.cpp:969:     const std::string dbPath = FreshDbPath("ffindexstore_test_corrupt_verdict.db");
indexstore/test_store.cpp:972:         Check(store.Open(dbPath), "corrupt-verdict fixture creates a real database");
indexstore/test_store.cpp:985:               "corrupt-verdict fixture file is reopenable for damage");
indexstore/test_store.cpp:988:         // valid, so this is a corrupt database, not a non-database file).
indexstore/test_store.cpp:995:     bool integrityFailed = false;
indexstore/test_store.cpp:996:     Check(!store.Open(dbPath, &integrityFailed), "a corrupted database fails the open");
indexstore/test_store.cpp:997:     Check(integrityFailed,
indexstore/test_store.cpp:998:           "a file SQLite positively verifies as corrupt reports corruption");
indexstore/test_store.cpp:1081:     TestOpenCreatesSchemaAndIsReopenable();
indexstore/test_store.cpp:1095:     TestSchemaV1ToV2MigrationInPlace();
indexstore/test_store.cpp:1100:     TestIntegrityVerdictClassifier();
indexstore/test_store.cpp:1101:     TestLockedDatabaseFailsOpenWithoutIntegrityFlag();
indexstore/test_store.cpp:1102:     TestCorruptDatabaseSetsIntegrityFailedFlag();
ipc/test_pipe_framing.cpp:555:     // Either the second read fails (framing lost) or it returns a corrupted
indexsvc/test_mft_parser.cpp:21: // logic against a known-good input (and deliberately corrupted variants)
indexsvc/test_mft_parser.cpp:199:     record[510] ^= 0xFF; // corrupt the marker NTFS would have written at the first sector's tail
indexsvc/test_mft_parser.cpp:305:     Check(ApplyFixupAndValidate(record.data(), record.size(), 512) == FixupResult::Ok, "fixup succeeds before corrupting the attribute stream");
indexsvc/test_mft_parser.cpp:326:     Check(ApplyFixupAndValidate(missing.data(), missing.size(), 512) == FixupResult::Ok, "fixup succeeds before corrupting the attribute stream");
indexsvc/test_worker_exception_boundary_policy.cpp:90:     // scan-generation-integrity-and-residual-certification-closure 3.4:
protocol/test_protocol.cpp:358:     // scan-generation-integrity-and-residual-certification-closure 3.4:
[100 matches limit reached. Use limit=200 for more, or refine pattern]
    enum class RemovalMaintenanceMode : uint8_t {
        Production = 0,
        SkipFilenameMaintenance = 1,
        SkipChildUnlink = 2,
        SkipBoth = 3,
    };
    void SetRemovalMaintenanceMode(RemovalMaintenanceMode mode) noexcept {
        removalMaintenanceMode_ = mode;
    }
#endif
    // large-index-lifecycle-and-search-scalability review-remediation (P2-A):
    // testing-only seam that force-marks a UTF-16 unit as fold-exceptional
    // (simulates a host ordinal table that equates it with ASCII, e.g. U+017F
    // LONG S vs 's'), so the candidate-completeness machinery for the
    // exceptional-name fallback is deterministically testable on hosts whose
    // real table has no cross-ASCII fold classes. The DECLARATION is visible
    // to every build so static analysis sees the seam; the definition and its
    // override state are compiled only with FF_INDEXSTORE_TESTING (test and
    // bench targets), so production binaries contain no seam behavior. Never
    // set in production.
    static void SetTestingFoldExceptionalUnit(char16_t unit, bool exceptional);
    const ProjectionEntry* Find(VolumeRowId volumeRowId, FileId frn) const;
    ProjectionEntryCursor BeginEntryCursor() const;
    // Returns a conservative candidate set for an already ASCII-folded
    // simple literal. nullopt means the secondary accelerator is ineligible
    // or unavailable, so callers must use the canonical live-entry scan.
    // The candidate set is a CONSERVATIVE SUPERSET of the rows the canonical
    // (CompareStringOrdinal ignore-case) matcher would accept: every row the
    // matcher accepts is present, though the set may contain false positives
    // that only the canonical matcher rejects. False negatives are forbidden
    // (review-remediation P2-A enforces this for every host ordinal table).
    // Returned slot indices belong to this projection generation and are
    // valid only while the caller holds the projection's synchronization.
    std::optional<std::vector<uint32_t>> FilenameCandidateIndices(
        std::u16string_view foldedNeedle, size_t maximumCandidates = 0) const;
    // task 2.3: parent->children lookup for directory-listing/Column-View
    // style access. Returns empty span if the parent has no known children
    // (which is not the same as the parent itself being unknown).
    std::span<const uint32_t> ChildIndices(VolumeRowId volumeRowId, FileId parentFrn) const;
    // memory-bounded-indexed-scale 5.x: drops one volume's resident rows and
    // marks the volume cold -- an explicit policy hook (e.g. unloading an
    // offline volume's working set) that also gives tests deterministic
    // forced-eviction state without depending on LRU victim order. Data stays
    // durable in Store; a no-op for an unknown/empty volume.
    void EvictVolume(VolumeRowId volumeRowId);
    // file-preview-and-properties §6.2 / storage-analysis §3.1: walks the
    // in-memory subtree rooted at `parentFrn` and returns the aggregate item
    // count (all descendants, excluding the root itself) and the total
    // sizeBytes sum across every entry in that subtree. Returns std::nullopt
    // if the root is unknown, distinguishing "not indexed" from "empty folder".
    // allocated-size-storage-accuracy D7: the allocated-size subtotal covers
    // only descendants whose allocated size is known, with the completeness
    // pair (known/total counts); Complete only when every descendant is
    // known. Sums saturate at UINT64_MAX with completeness cleared on
    // saturation -- a wrapped-looking total must never be presented as exact.
    struct FolderAggregate {
        uint64_t itemCount = 0;
        uint64_t totalSizeBytes = 0;
        uint64_t totalAllocatedSizeBytes = 0;
        uint64_t allocatedBytesKnownCount = 0;
        uint64_t allocatedBytesTotalCount = 0;
        bool complete = true;
    };
    std::optional<FolderAggregate> GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn) const;
    const ProjectionEntry& EntryAt(uint32_t index) const { return entries_[index]; }
    struct PathResult {
        std::u16string path; // backslash-joined names, root-to-leaf, leaf included
        // false if the walk stopped early because it hit an entry not
        // present in the projection, or detected revisiting an FRN already
        // seen in this same walk (task 2.4's defensive cycle detection) --
        // in both cases `path` holds whatever prefix was resolved.
        bool reachedRoot = false;
    };
    // task 2.4: walks parent references and concatenates interned names
    // on demand -- never reads a precomputed path field.
    PathResult ReconstructPath(VolumeRowId volumeRowId, FileId frn) const;
    // nonblocking-indexed-read-plane 4.3: the same bounded walk with a
    // cooperative cancellation check between parent links.
    PathResult ReconstructPathUntil(VolumeRowId volumeRowId, FileId frn,
                                     const std::function<bool()>& cancelled) const;
    // task 3.1: rebuilds this volume's portion of the projection from the
    // durable store in a single streaming pass, interning names and
    // building the parent->children index as rows arrive. Returns false when
    // the durable stream fails; the volume is left cold with no partial
    // projection so callers cannot publish an incomplete rebuild.
    // expectedEntryCount pre-sizes allocations (task 2.5/D4); the caller
    // gets this from Store::GetVolumeMetadata(...)->entryCount.
    bool RebuildVolumeFromStore(Store& store, VolumeRowId volumeRowId, uint64_t expectedEntryCount);
    size_t EntryCount() const noexcept { return idToIndex_.size(); }
    const NamePool& Names() const noexcept { return namePool_; }
    // Visits every live entry (fn(const EntryKey&, const ProjectionEntry&))
    // -- used by consumers that need to export the whole projection (e.g.
    // converting it to the directory-listing snapshot format for
    // publication). Iterates the live-entry index rather than the dense
    // array directly, so tombstoned/reused slots are never visited.
    template <typename Fn>
    void ForEachEntry(Fn&& fn) const {
        idToIndex_.ForEach([&](const EntryKey& key, const uint32_t index) {
            fn(key, entries_[index]);
        });
    }
    template <typename Fn>
    void ForEachEntryValue(Fn&& fn) const {
        if (denseIterationOrderValid_) {
            for (const ProjectionEntry& entry : entries_) fn(entry);
        } else {
            idToIndex_.ForEachValue([&](const uint32_t index) { fn(entries_[index]); });
        }
[107 more lines in file. Use offset=420 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
read: C:/Users/Michael Roy/Documents/FastFiles/tests/indexstore...
// Informational benchmark only. This executable measures candidate recall and
// systems costs before any secondary filename index is considered for the
// production projection. It is intentionally not registered with CTest.
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "ffsearch/Query.h"
#include "sqlite3.h"
namespace {
using Clock = std::chrono::steady_clock;
using EntryId = uint32_t;
constexpr size_t kWarmRuns = 5;
struct MemorySample {
    uint64_t privateBytes = 0;
    uint64_t workingSetBytes = 0;
};
MemorySample ReadMemory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        return {};
    }
    return {static_cast<uint64_t>(counters.PrivateUsage), static_cast<uint64_t>(counters.WorkingSetSize)};
}
double ElapsedMs(const Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::string ToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                            bytes, nullptr, nullptr) != bytes) {
        return {};
    }
    return result;
}
std::string JsonEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) result += "?";
                else result.push_back(ch);
                break;
        }
    }
    return result;
}
void WriteJsonString(std::ostream& output, std::string_view value) {
    output << '"' << JsonEscape(value) << '"';
}
void WriteJsonDouble(std::ostream& output, double value) {
    output << std::fixed << std::setprecision(4) << value;
}
struct Entry {
    EntryId id = 0;
    std::wstring name;
    bool active = true;
};
bool IsAscii(std::wstring_view value) {
    return std::all_of(value.begin(), value.end(), [](const wchar_t ch) { return ch <= 0x7f; });
}
bool IsAsciiLower(std::wstring_view value) {
    return std::all_of(value.begin(), value.end(), [](const wchar_t ch) {
        return ch <= 0x7f && !(ch >= L'A' && ch <= L'Z');
    });
}
std::u16string FoldAscii(std::wstring_view value) {
    std::u16string result;
    result.reserve(value.size());
    for (const wchar_t ch : value) {
        result.push_back(static_cast<char16_t>(ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch));
    }
    return result;
}
std::wstring DirectoryName(size_t index) {
    return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
}
[738 more lines in file. Use offset=121 to continue.]
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "sqlite3.h"
#include "ffindexstore/Store.h"
#include "../TestSupport.h"
using namespace fftest;
namespace {
using namespace ffindexstore;
EntryRecord MakeEntry(uint64_t id, uint64_t parent, std::u16string name, uint32_t attributes = 0) {
    EntryRecord r;
    r.id = FileId{id, 0};
    r.parentId = FileId{parent, 0};
    r.name = std::move(name);
    r.attributes = attributes;
    return r;
}
// Builds a schema-version-1 database exactly as the pre-allocated-size
// store created it (volumes + entries without allocated_size_bytes,
// user_version=1), including one row, so Store::Open's in-place migration
// can be exercised against a faithful historical fixture.
bool CreateVersion1Fixture(const std::string& dbPath) {
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        return false;
    }
    const char* schemaV1 =
        "CREATE TABLE volumes ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  volume_guid BLOB NOT NULL,"
        "  serial_number INTEGER NOT NULL,"
        "  available INTEGER NOT NULL DEFAULT 1,"
        "  last_seen_time INTEGER NOT NULL DEFAULT 0,"
        "  journal_id INTEGER,"
        "  resume_usn INTEGER NOT NULL DEFAULT 0,"
        "  scan_cursor BLOB,"
        "  scan_complete INTEGER NOT NULL DEFAULT 0,"
        "  last_reconciliation_time INTEGER NOT NULL DEFAULT 0,"
        "  UNIQUE(volume_guid, serial_number)"
        ");"
        "CREATE TABLE entries ("
        "  volume_id INTEGER NOT NULL REFERENCES volumes(id),"
        "  frn_low INTEGER NOT NULL,"
        "  frn_high INTEGER NOT NULL DEFAULT 0,"
        "  parent_frn_low INTEGER NOT NULL,"
        "  parent_frn_high INTEGER NOT NULL DEFAULT 0,"
        "  name TEXT NOT NULL,"
        "  size_bytes INTEGER NOT NULL DEFAULT 0,"
        "  creation_time INTEGER NOT NULL DEFAULT 0,"
        "  last_modified_time INTEGER NOT NULL DEFAULT 0,"
        "  last_access_time INTEGER NOT NULL DEFAULT 0,"
        "  attributes INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (volume_id, frn_low, frn_high)"
        ") WITHOUT ROWID;"
        "INSERT INTO volumes(id, volume_guid, serial_number) VALUES (1, x'0102030405060708090A0B0C0D0E0F10', 42);"
        "INSERT INTO entries(volume_id, frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes) "
        "  VALUES (1, 100, 0, 5, 0, 'legacy.txt', 4096);"
        "PRAGMA user_version=1;";
    const bool ok = sqlite3_exec(db, schemaV1, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}
// allocated-size-storage-accuracy 3.3: a version-1 database must migrate
// in place -- no rebuild, no data loss -- and pre-existing rows must read
// back as unknown (NULL), never as fabricated zeros.
void TestSchemaV1ToV2MigrationInPlace() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_migrate_v1.db");
    Check(CreateVersion1Fixture(dbPath), "version-1 fixture database is created");
    Store store;
    bool integrityFailed = false;
    Check(store.Open(dbPath, &integrityFailed), "a version-1 database opens and migrates in place");
    Check(!integrityFailed, "integrity check passes before/after migration");
    Check(store.RunIntegrityCheck(), "post-migration integrity check passes");
    Check(store.CountEntries(1) == 1, "the migrated database retains its existing row");
    EntryRecord legacy;
    Check(store.ForEachEntry(1, [&](const EntryRecord& entry) { legacy = entry; }),
          "the migrated row streams normally");
    Check(legacy.name == u"legacy.txt" && legacy.sizeBytes == 4096,
          "the migrated row retains its pre-existing fields");
    Check(!legacy.allocatedSizeKnown && legacy.allocatedSizeBytes == 0,
          "a row that predates the feature reads back as unknown, not zero");
    // New rows written after migration carry allocated size normally.
    EntryRecord fresh = MakeEntry(200, 5, u"fresh.bin");
    fresh.sizeBytes = 100;
    fresh.allocatedSizeBytes = 64;
    fresh.allocatedSizeKnown = true;
    Check(store.ApplyBatch(1, {{EntryChangeKind::Upsert, fresh}}),
          "post-migration rows upsert with allocated size");
    EntryRecord freshRead;
    int seen = 0;
    store.ForEachEntry(1, [&](const EntryRecord& entry) {
        if (entry.id.low == 200) freshRead = entry;
        ++seen;
    });
    Check(seen == 2 && freshRead.allocatedSizeKnown && freshRead.allocatedSizeBytes == 64,
          "post-migration rows round-trip allocated size as known");
    store.Close();
    Store reopened;
    Check(reopened.Open(dbPath), "the migrated database reopens");
[991 more lines in file. Use offset=121 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine
bench_secondary_filename_index.cpp:632:     bool fts5Create = false;
bench_secondary_filename_index.cpp:634:     bool trigramCreate = false;
bench_secondary_filename_index.cpp:635:     bool trigramInfixRecall = false;
bench_secondary_filename_index.cpp:650:     result.compileOption = sqlite3_compileoption_used("ENABLE_FTS5") != 0;
bench_secondary_filename_index.cpp:654:     const int createResult = sqlite3_exec(db, "CREATE VIRTUAL TABLE names USING fts5(name);", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp:655:     result.fts5Create = createResult == SQLITE_OK;
bench_secondary_filename_index.cpp:656:     if (!result.fts5Create && error != nullptr) {
bench_secondary_filename_index.cpp:661:     if (result.fts5Create) {
bench_secondary_filename_index.cpp:665:     sqlite3_exec(db, "DROP TABLE IF EXISTS trigram_names;", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp:666:     const int trigramResult = sqlite3_exec(db,
bench_secondary_filename_index.cpp:667:         "CREATE VIRTUAL TABLE trigram_names USING fts5(name, tokenize='trigram');", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp:668:     result.trigramCreate = trigramResult == SQLITE_OK;
bench_secondary_filename_index.cpp:670:     if (result.trigramCreate) {
bench_secondary_filename_index.cpp:671:         sqlite3_exec(db, "INSERT INTO trigram_names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp:672:         result.trigramInfixRecall = QueryHasRow(db, "SELECT rowid FROM trigram_names WHERE trigram_names MATCH ?;", "pha");
bench_secondary_filename_index.cpp:787:     output << "{\"compile_option_enable_fts5\":" << (capability.compileOption ? "true" : "false")
bench_secondary_filename_index.cpp:788:            << ",\"fts5_create\":" << (capability.fts5Create ? "true" : "false")
bench_secondary_filename_index.cpp:790:            << ",\"trigram_create\":" << (capability.trigramCreate ? "true" : "false")
bench_secondary_filename_index.cpp:791:            << ",\"trigram_infix_recall\":" << (capability.trigramInfixRecall ? "true" : "false")
bench_secondary_filename_index.cpp:802:     const FtsCapability fts = MeasureFtsCapability();
bench_secondary_filename_index.cpp:816:         measurements.push_back(MeasureArchitecture<TrigramIndex>("utf16_trigram_postings", entries, scale, queries));
bench_secondary_filename_index.cpp:828:     output << "{\"schema\":\"fastfiles-secondary-filename-index-v1\",\"sqlite_fts_capability\":";
bench_secondary_filename_index.cpp:829:     WriteFts(output, fts);
bench_secondary_filename_index.cpp:848:     output << "],\"decision\":{\"selected\":\"utf16_trigram_postings\",\"production_integration\":false,"
bench_secondary_filename_index.cpp:850:               "\"rejected_fts_reason\":\"vendored_sqlite_is_not_compiled_with_fts5_and_create_fails\","
CMakeLists.txt:10: target_compile_definitions(ffengine_index_pipeline_tests PRIVATE FF_INDEX_PIPELINE_TESTING=1)
test_index_pipeline.cpp:47: #ifdef FF_INDEX_PIPELINE_TESTING
grep: C:/Users/Michael Roy/Documents/FastFiles/third_party
grep: C:/Users/Michael Roy/Documents/FastFiles
sqlite/CMakeLists.txt:15:   SQLITE_ENABLE_FTS3=0
sqlite/sqlite3.h:1982: ** with either [SQLITE_ENABLE_MEMSYS3] or [SQLITE_ENABLE_MEMSYS5] and returns
sqlite/sqlite3.h:2102: ** [SQLITE_ENABLE_SQLLOG] pre-processor macro defined. The first argument should
sqlite/sqlite3.h:2184: ** [SQLITE_ENABLE_SORTER_REFERENCES] compile-time option.
sqlite/sqlite3.h:2362: ** [[SQLITE_DBCONFIG_ENABLE_FTS3_TOKENIZER]]
sqlite/sqlite3.h:2363: ** <dt>SQLITE_DBCONFIG_ENABLE_FTS3_TOKENIZER</dt>
sqlite/sqlite3.h:2578: ** [SQLITE_ENABLE_STMT_SCANSTATUS] builds. In this case, it sets or clears
sqlite/sqlite3.h:2703: #define SQLITE_DBCONFIG_ENABLE_FTS3_TOKENIZER 1004 /* int int* */
sqlite/sqlite3.h:3081: ** This routine is only useful in SQLITE_ENABLE_SETLK_TIMEOUT builds. If
sqlite/sqlite3.h:3084: ** handle. In non-SQLITE_ENABLE_SETLK_TIMEOUT builds, or if the VFS does
sqlite/sqlite3.h:4585: ** and the [SQLITE_ENABLE_STAT4] compile-time option is enabled.
sqlite/sqlite3.h:4679: ** the [SQLITE_ENABLE_NORMALIZE] compile-time option is defined.
sqlite/sqlite3.h:4683: #ifdef SQLITE_ENABLE_NORMALIZE
sqlite/sqlite3.h:5183: ** [SQLITE_ENABLE_COLUMN_METADATA] C-preprocessor symbol.
sqlite/sqlite3.h:5981: ** built using the -DSQLITE_ENABLE_ORDERED_SET_AGGREGATES compile-time option.
sqlite/sqlite3.h:6323: **      as sometimes happens with [SQLITE_ENABLE_STAT4].)^ </ul>
sqlite/sqlite3.h:6777: #ifdef SQLITE_ENABLE_CEROD
sqlite/sqlite3.h:7346: ** if SQLite is not compiled with [SQLITE_ENABLE_MEMORY_MANAGEMENT].
sqlite/sqlite3.h:7359: ** when the [SQLITE_ENABLE_MEMORY_MANAGEMENT] compile-time option is
sqlite/sqlite3.h:8440: ** SQLITE_ENABLE_SETLK_TIMEOUT build option. In that case a working
sqlite/sqlite3.h:9771: ** [SQLITE_ENABLE_UNLOCK_NOTIFY] C-preprocessor symbol defined.
sqlite/sqlite3.h:10711: ** available if SQLite is compiled using the [SQLITE_ENABLE_STMT_SCANSTATUS]
sqlite/sqlite3.h:10766: ** symbol [SQLITE_ENABLE_STMT_SCANSTATUS] defined.
sqlite/sqlite3.h:10808: ** [SQLITE_ENABLE_PREUPDATE_HOOK] compile-time option.
sqlite/sqlite3.h:10899: #if defined(SQLITE_ENABLE_PREUPDATE_HOOK)
sqlite/sqlite3.h:11006: ** [SQLITE_ENABLE_SNAPSHOT] compile-time option is used.
sqlite/sqlite3.h:11055: ** [SQLITE_ENABLE_SNAPSHOT] compile-time option is used.
sqlite/sqlite3.h:11072: ** [SQLITE_ENABLE_SNAPSHOT] compile-time option is used.
sqlite/sqlite3.h:11099: ** [SQLITE_ENABLE_SNAPSHOT] option.
sqlite/sqlite3.h:11127: ** [SQLITE_ENABLE_SNAPSHOT] option.
sqlite/sqlite3.h:11492: #if !defined(__SQLITESESSION_H_) && defined(SQLITE_ENABLE_SESSION)
sqlite/sqlite3.h:13590: #endif  /* !defined(__SQLITESESSION_H_) && defined(SQLITE_ENABLE_SESSION) */
sqlite/sqlite3.h:13593: /******** Begin file fts5.h *********/
sqlite/sqlite3.h:13606: ** Interfaces to extend FTS5. Using the interfaces defined in this file,
sqlite/sqlite3.h:13607: ** FTS5 may be extended with:
sqlite/sqlite3.h:13614: #ifndef _FTS5_H
sqlite/sqlite3.h:13615: #define _FTS5_H
sqlite/sqlite3.h:13629: typedef struct Fts5ExtensionApi Fts5ExtensionApi;
sqlite/sqlite3.h:13630: typedef struct Fts5Context Fts5Context;
sqlite/sqlite3.h:13631: typedef struct Fts5PhraseIter Fts5PhraseIter;
sqlite/sqlite3.h:13633: typedef void (*fts5_extension_function)(
sqlite/sqlite3.h:13634:   const Fts5ExtensionApi *pApi,   /* API offered by current FTS version */
sqlite/sqlite3.h:13635:   Fts5Context *pFts,              /* First arg to pass to pApi functions */
sqlite/sqlite3.h:13641: struct Fts5PhraseIter {
sqlite/sqlite3.h:13655: **   to the total number of tokens in the FTS5 table. Or, if iCol is
sqlite/sqlite3.h:13658: **   the FTS5 table.
sqlite/sqlite3.h:13679: **   This function may be quite inefficient if used with an FTS5 table
sqlite/sqlite3.h:13707: **   This API can be quite slow if used with an FTS5 table created with the
sqlite/sqlite3.h:13708: **   "detail=none" or "detail=column" option. If the FTS5 table is created
sqlite/sqlite3.h:13724: **   This API can be quite slow if used with an FTS5 table created with the
sqlite/sqlite3.h:13731: **   Tokenize text using the tokenizer belonging to the FTS5 table.
sqlite/sqlite3.h:13733: ** xQueryPhrase(pFts5, iPhrase, pUserData, xCallback):
sqlite/sqlite3.h:13762: ** xSetAuxdata(pFts5, pAux, xDelete)
sqlite/sqlite3.h:13766: **   future invocation of the same fts5 extension function made as part of
sqlite/sqlite3.h:13780: **   auxiliary data pointer after the FTS5 query has finished.
sqlite/sqlite3.h:13788: ** xGetAuxdata(pFts5, bClear)
sqlite/sqlite3.h:13790: **   Returns the current auxiliary data pointer for the fts5 extension
sqlite/sqlite3.h:13798: ** xRowCount(pFts5, pnRow)
sqlite/sqlite3.h:13806: **   This function is used, along with type Fts5PhraseIter and the xPhraseNext
sqlite/sqlite3.h:13813: **       Fts5PhraseIter iter;
sqlite/sqlite3.h:13822: **   The Fts5PhraseIter structure is defined above. Applications should not
sqlite/sqlite3.h:13827: **   This API can be quite slow if used with an FTS5 table created with the
sqlite/sqlite3.h:13828: **   "detail=none" or "detail=column" option. If the FTS5 table is created
sqlite/sqlite3.h:13847: **       Fts5PhraseIter iter;
sqlite/sqlite3.h:13856: **   This API can be quite slow if used with an FTS5 table created with the
sqlite/sqlite3.h:13857: **   "detail=none" option. If the FTS5 table is created with either
sqlite/sqlite3.h:13871: ** xQueryToken(pFts5, iPhrase, iToken, ppToken, pnToken)
sqlite/sqlite3.h:13887: ** xInstToken(pFts5, iIdx, iToken, ppToken, pnToken)
sqlite/sqlite3.h:13908: **   prefix token, FTS5 may be configured to collect all required data as part
sqlite/sqlite3.h:13911: **   run more slowly and use more memory. FTS5 may be configured in this way
sqlite/sqlite3.h:13912: **   either on a per-table basis using the [FTS5 insttoken | 'insttoken']
sqlite/sqlite3.h:13914: **   [fts5_insttoken | fts5_insttoken()] user function.
sqlite/sqlite3.h:13916: **   This API can be quite slow if used with an FTS5 table created with the
sqlite/sqlite3.h:13919: ** xColumnLocale(pFts5, iIdx, pzLocale, pnLocale)
sqlite/sqlite3.h:13926: **   to NULL and 0, respectively. However, if the fts5_locale() function
sqlite/sqlite3.h:13928: **   into the fts5 table, then (*pzLocale) is set to point to a nul-terminated
sqlite/sqlite3.h:13938: **   Tokenize text using the tokenizer belonging to the FTS5 table. This
sqlite/sqlite3.h:13942: struct Fts5ExtensionApi {
sqlite/sqlite3.h:13945:   void *(*xUserData)(Fts5Context*);
sqlite/sqlite3.h:13947:   int (*xColumnCount)(Fts5Context*);
sqlite/sqlite3.h:13948:   int (*xRowCount)(Fts5Context*, sqlite3_int64 *pnRow);
sqlite/sqlite3.h:13949:   int (*xColumnTotalSize)(Fts5Context*, int iCol, sqlite3_int64 *pnToken);
sqlite/sqlite3.h:13951:   int (*xTokenize)(Fts5Context*,
sqlite/sqlite3.h:13957:   int (*xPhraseCount)(Fts5Context*);
sqlite/sqlite3.h:13958:   int (*xPhraseSize)(Fts5Context*, int iPhrase);
sqlite/sqlite3.h:13960:   int (*xInstCount)(Fts5Context*, int *pnInst);
sqlite/sqlite3.h:13961:   int (*xInst)(Fts5Context*, int iIdx, int *piPhrase, int *piCol, int *piOff);
sqlite/sqlite3.h:13963:   sqlite3_int64 (*xRowid)(Fts5Context*);
sqlite/sqlite3.h:13964:   int (*xColumnText)(Fts5Context*, int iCol, const char **pz, int *pn);
sqlite/sqlite3.h:13965:   int (*xColumnSize)(Fts5Context*, int iCol, int *pnToken);
sqlite/sqlite3.h:13967:   int (*xQueryPhrase)(Fts5Context*, int iPhrase, void *pUserData,
sqlite/sqlite3.h:13968:     int(*)(const Fts5ExtensionApi*,Fts5Context*,void*)
sqlite/sqlite3.h:13970:   int (*xSetAuxdata)(Fts5Context*, void *pAux, void(*xDelete)(void*));
sqlite/sqlite3.h:13971:   void *(*xGetAuxdata)(Fts5Context*, int bClear);
sqlite/sqlite3.h:13973:   int (*xPhraseFirst)(Fts5Context*, int iPhrase, Fts5PhraseIter*, int*, int*);
sqlite/sqlite3.h:13974:   void (*xPhraseNext)(Fts5Context*, Fts5PhraseIter*, int *piCol, int *piOff);
sqlite/sqlite3.h:13976:   int (*xPhraseFirstColumn)(Fts5Context*, int iPhrase, Fts5PhraseIter*, int*);
sqlite/sqlite3.h:13977:   void (*xPhraseNextColumn)(Fts5Context*, Fts5PhraseIter*, int *piCol);
sqlite/sqlite3.h:13980:   int (*xQueryToken)(Fts5Context*,
sqlite/sqlite3.h:13984:   int (*xInstToken)(Fts5Context*, int iIdx, int iToken, const char**, int*);
[100 matches limit reached. Use limit=200 for more, or refine pattern]
No matches found
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
bool IndexPipeline::VolumeSettingsEqual(const ffprotocol::VolumeSetting& left,
                                        const ffprotocol::VolumeSetting& right) {
    if (left.key != right.key || left.enabled != right.enabled || left.rules.size() != right.rules.size()) {
        return false;
    }
    for (size_t index = 0; index < left.rules.size(); ++index) {
        if (left.rules[index].path != right.rules[index].path ||
            left.rules[index].include != right.rules[index].include) {
            return false;
        }
    }
    return true;
}
void IndexPipeline::AdvanceQueryVisibleEpochLocked(const ffindexstore::VolumeRowId volumeId,
                                                    const QueryMutationReason reason,
                                                    const bool dependencySetChanged) {
    const uint64_t nextEpoch = queryVisibleEpoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (volumeId != 0) {
        volumeDataEpochs_[volumeId] = nextEpoch;
        if (reason == QueryMutationReason::VolumeConfiguration ||
            reason == QueryMutationReason::VolumeVisibility ||
            reason == QueryMutationReason::Forget) {
            volumeConfigurationEpochs_[volumeId] = nextEpoch;
        }
    }
    if (dependencySetChanged || reason == QueryMutationReason::Forget) {
        dependencySetEpoch_.fetch_add(1, std::memory_order_acq_rel);
    }
    queryVisibleInvalidations_.fetch_add(1, std::memory_order_relaxed);
}
void IndexPipeline::RecordMetadataOnlyUpdateLocked() {
    metadataOnlyUpdates_.fetch_add(1, std::memory_order_relaxed);
    metadataOnlyInvalidationsAvoided_.fetch_add(1, std::memory_order_relaxed);
}
void IndexPipeline::AdvanceResidencyEpochsForStructuralChangeLocked() {
    // large-index-lifecycle-and-search-scalability review-lane D P1-1 / lane C
    // Finding 1: memory-pressure enforcement and cold reload are structural
    // projection events -- EvictSubtree can cold-mark ANY volume (the LRU
    // victim is a global pick) and CompactStorage/RebuildVolumeFromStore
    // repack every row slot, so an in-flight search's captured cursor,
    // candidate slots, and cold-set can all be wrong afterwards. Surface the
    // change through the SAME ProjectionResidency epoch mechanism
    // SetProjectionMemoryBudget / UnloadVolumeFromProjection use, so searches
    // restart with a fresh capture instead of silently serving incomplete
    // results as Completed. Ordinary per-volume mutations never route through
    // here (scoped searches stay insensitive to unrelated writes).
    for (const auto& [volumeId, epoch] : volumeDataEpochs_) {
        (void)epoch;
        AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::ProjectionResidency);
    }
    AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::ProjectionResidency, true);
}
bool IndexPipeline::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed) {
    lifecycleEpoch_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lock(mutex_);
    recordsSinceAnalyze_ = 0;
    projectionMemoryWarningIssued_ = false;
    if (!store_.Open(dbPathUtf8, outIntegrityFailed)) {
        return false;
    }
    volumeDataEpochs_.clear();
    volumeConfigurationEpochs_.clear();
    AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::Lifecycle, true);
    return true;
}
void IndexPipeline::Close() {
    lifecycleEpoch_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lock(mutex_);
    store_.Close();
    volumeDataEpochs_.clear();
    volumeConfigurationEpochs_.clear();
    AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::Lifecycle, true);
    recordsSinceAnalyze_ = 0;
    projectionMemoryWarningIssued_ = false;
}
void IndexPipeline::SetSearchTestHooks(SearchTestHooks hooks) {
    std::lock_guard<std::mutex> lock(mutex_);
    searchTestHooks_ = std::move(hooks);
}
void IndexPipeline::SetReadSessionFaultInjector(ffindexstore::ReadSessionFaultInjector injector) {
    std::lock_guard<std::mutex> lock(mutex_);
    readSessionFaultInjector_ = std::move(injector);
}
#ifdef FF_INDEX_PIPELINE_TESTING
void IndexPipeline::SetSecondaryPruningFaultInjector(SecondaryPruningFaultInjector injector) {
    std::lock_guard<std::mutex> lock(mutex_);
    secondaryPruningFaultInjector_ = std::move(injector);
}
bool IndexPipeline::ShouldInjectSecondaryPruningFailureLocked(
    const SecondaryPruningBoundary boundary, const ffindexstore::VolumeRowId volumeId,
    const std::vector<ffindexstore::EntryChange>& changes) {
    return secondaryPruningFaultInjector_ &&
        secondaryPruningFaultInjector_(boundary, volumeId, changes);
}
#endif
IndexPipeline::ReadPlaneDiagnostics IndexPipeline::GetReadPlaneDiagnostics() const noexcept {
    ReadPlaneDiagnostics result;
    result.activeSearches = activeSearches_.load(std::memory_order_relaxed);
    result.peakSearches = peakSearches_.load(std::memory_order_relaxed);
    result.activeDurableReaders = activeDurableReaders_.load(std::memory_order_relaxed);
    result.peakDurableReaders = peakDurableReaders_.load(std::memory_order_relaxed);
    result.completedSearches = completedSearches_.load(std::memory_order_relaxed);
    result.cancelledSearches = cancelledSearches_.load(std::memory_order_relaxed);
    result.unavailableSearches = unavailableSearches_.load(std::memory_order_relaxed);
    result.readerFailures = readerFailures_.load(std::memory_order_relaxed);
[1704 more lines in file. Use offset=501 to continue.]
EntryRecord.h:14: struct EntryRecord {
EntryRecord.h:34: using EntryFieldMask = uint32_t;
EntryRecord.h:35: constexpr EntryFieldMask kEntryFieldParentId = 1u << 0;
EntryRecord.h:36: constexpr EntryFieldMask kEntryFieldName = 1u << 1;
EntryRecord.h:37: constexpr EntryFieldMask kEntryFieldSizeBytes = 1u << 2;
EntryRecord.h:38: constexpr EntryFieldMask kEntryFieldCreationTime = 1u << 3;
EntryRecord.h:39: constexpr EntryFieldMask kEntryFieldLastModifiedTime = 1u << 4;
EntryRecord.h:40: constexpr EntryFieldMask kEntryFieldLastAccessTime = 1u << 5;
EntryRecord.h:41: constexpr EntryFieldMask kEntryFieldAttributes = 1u << 6;
EntryRecord.h:45: constexpr EntryFieldMask kEntryFieldAllocatedSizeBytes = 1u << 7;
EntryRecord.h:46: constexpr EntryFieldMask kEntryFieldMaskAll =
EntryRecord.h:51: constexpr bool HasEntryField(EntryFieldMask fields, EntryFieldMask field) noexcept {
EntryRecord.h:55: enum class EntryChangeKind : uint8_t {
EntryRecord.h:60: struct EntryChange {
EntryRecord.h:61:     EntryChangeKind kind = EntryChangeKind::Upsert;
EntryRecord.h:63:     EntryFieldMask fields = kEntryFieldMaskAll;
read: C:/Users/Michael Roy/Documents/FastFiles/tests/indexstore...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/indexstore...
    EntryRecord entry;
    entry.id = FileId{1, 0};
    entry.parentId = FileId{1, 0};
    entry.name = u"root";
    store.ApplyBatch(volA, {{EntryChangeKind::Upsert, entry}});
    store.ApplyBatch(volB, {{EntryChangeKind::Upsert, entry}});
    store.SetVolumeAvailable(volA, false, 100);
    Check(store.CountEntries(volA) == 1, "marking a volume unavailable does not delete its entries");
    auto metaB = store.GetVolumeMetadata(volB);
    Check(metaB->available, "an unrelated volume's availability is unaffected");
    Check(store.CountEntries(volB) == 1, "an unrelated volume's entries are unaffected");
}
void TestForgetVolumeIsExplicitAndRemovesEntries() {
    Store store;
    store.Open(FreshDbPath("ffindexstore_test_forget.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = *store.GetOrCreateVolume(key);
    EntryRecord entry;
    entry.id = FileId{1, 0};
    entry.parentId = FileId{1, 0};
    entry.name = u"root";
    store.ApplyBatch(vol, {{EntryChangeKind::Upsert, entry}});
    Check(!store.ForgetVolume(vol), "an available volume cannot be forgotten");
    Check(store.CountEntries(vol) == 1 && store.GetVolumeMetadata(vol).has_value(),
          "a rejected forget leaves the available volume untouched");
    Check(store.SetVolumeAvailable(vol, false, 100), "the volume can be marked unavailable before forgetting");
    Check(store.ForgetVolume(vol), "explicit ForgetVolume succeeds");
    Check(store.CountEntries(vol) == 0, "entries are gone after an explicit forget");
    Check(!store.GetVolumeMetadata(vol).has_value(), "the volume row itself is gone after an explicit forget");
}
// tasks.md 9.1 proxy: a real "kill -9 mid-batch" can't be simulated from
// within this process, but the mechanism it depends on -- WAL replay
// recovering already-committed data on next open, without requiring an
// explicit checkpoint first -- is exactly what this exercises: data is
// committed via ApplyBatch (a real transaction, same as ingestion uses),
// the Store is destroyed with no checkpoint call at all, and a fresh
// Store reopening the same file must still see it.
void TestWalReplayRecoversCommittedDataWithoutAnExplicitCheckpoint() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_wal_replay.db");
    VolumeKey key;
    key.serialNumber = 1;
    VolumeRowId vol;
    {
        Store store;
        store.Open(dbPath);
        vol = *store.GetOrCreateVolume(key);
        EntryRecord entry;
        entry.id = FileId{1, 0};
        entry.parentId = FileId{1, 0};
        entry.name = u"root";
        store.ApplyBatch(vol, {{EntryChangeKind::Upsert, entry}});
        // Deliberately no CheckpointPassive()/explicit sync call here --
        // only the destructor's plain sqlite3_close runs.
    }
    Store reopened;
    Check(reopened.Open(dbPath), "a database with an un-checkpointed WAL still opens cleanly");
    Check(reopened.CountEntries(vol) == 1, "WAL replay on open recovers committed data with no explicit prior checkpoint");
    Check(reopened.RunIntegrityCheck(), "the recovered database passes an integrity check");
}
void TestDataSurvivesReopenAfterClose() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_durable.db");
    VolumeKey key;
    key.serialNumber = 1;
    VolumeRowId vol;
    {
        Store store;
        store.Open(dbPath);
        vol = *store.GetOrCreateVolume(key);
        EntryRecord entry;
        entry.id = FileId{1, 0};
        entry.parentId = FileId{1, 0};
        entry.name = u"root";
        store.ApplyBatch(vol, {{EntryChangeKind::Upsert, entry}});
        store.CheckpointPassive();
    }
    Store reopened;
    Check(reopened.Open(dbPath), "database reopens after a clean close");
    Check(reopened.CountEntries(vol) == 1, "committed data survives close/reopen (WAL durability)");
}
void TestVacuumGatingByIdleIntervalAndGrowth() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_vacuum_gating.db");
    Store store;
    Check(store.Open(dbPath), "database opens for VACUUM gating");
    VolumeKey key;
    key.serialNumber = 1;
    const auto vol = *store.GetOrCreateVolume(key);
    constexpr uint64_t kFarFuture = std::numeric_limits<uint64_t>::max();
    Check(!store.VacuumIfDue(false, kFarFuture, 0, 0), "VACUUM requires an idle window");
    Check(!store.VacuumIfDue(true, kFarFuture, 0, 10), "VACUUM skips when the database has not grown");
    std::vector<EntryChange> changes;
    changes.reserve(1'000);
    const std::u16string longName(256, u'x');
    for (uint64_t index = 0; index < 1'000; ++index) {
        changes.push_back({EntryChangeKind::Upsert, MakeEntry(1'000 + index, 1'000 + index, longName)});
    }
    Check(store.ApplyBatch(vol, changes), "bulk rows apply for VACUUM growth gating");
    Check(store.CheckpointPassive(), "the growth fixture is checkpointed before VACUUM evaluation");
    Check(!store.VacuumIfDue(true, 0, 24, 10), "VACUUM honors the interval gate");
    Check(store.VacuumIfDue(true, kFarFuture, 0, 10), "VACUUM runs after idle, interval, and growth gates pass");
    Check(!store.VacuumIfDue(true, kFarFuture, 0, 10), "a successful VACUUM resets the growth baseline");
}
void TestGetFolderAggregateFromStore() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_folder_agg.db");
    Store store;
    store.Open(dbPath);
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = *store.GetOrCreateVolume(key);
    // Root (5), children: Users(100), Windows(101), file(102)
[612 more lines in file. Use offset=500 to continue.]
test_store.cpp:82: void TestSchemaV1ToV2MigrationInPlace() {
test_store.cpp:119:     Store reopened;
test_store.cpp:120:     Check(reopened.Open(dbPath), "the migrated database reopens");
test_store.cpp:121:     EntryRecord legacyAfterReopen;
test_store.cpp:122:     reopened.ForEachEntry(1, [&](const EntryRecord& entry) {
test_store.cpp:123:         if (entry.id.low == 100) legacyAfterReopen = entry;
test_store.cpp:125:     Check(!legacyAfterReopen.allocatedSizeKnown,
test_store.cpp:132: void TestAllocatedSizeUnknownVsZeroAndPreservation() {
test_store.cpp:189: void TestOpenCreatesSchemaAndIsReopenable() {
test_store.cpp:198:     Store reopened;
test_store.cpp:199:     Check(reopened.Open(dbPath), "reopening an existing, valid database succeeds");
test_store.cpp:202: void TestVolumeIdentityIsStableAndDeduplicated() {
test_store.cpp:221: void TestEntryIdentityIsKeyedByVolumeAndFrnNotPath() {
test_store.cpp:247: void TestBatchUpsertAndRemove() {
test_store.cpp:277: void TestPartialUpsertPreservesAbsentAuthoritativeFields() {
test_store.cpp:312:     Store reopened;
test_store.cpp:313:     Check(reopened.Open(dbPath), "partial-update fixture reopens after restart");
test_store.cpp:315:     Check(reopened.ForEachEntry(volume, [&](const EntryRecord& entry) { afterRestart = entry; }),
test_store.cpp:319:           "merged timestamps survive SQLite close and reopen");
test_store.cpp:322: void TestVolumeMetadataRoundTrips() {
test_store.cpp:349: void TestAllVolumeMetadataCanSkipEntryCounts() {
test_store.cpp:370: void TestDisconnectDoesNotDeleteEntriesOrAffectOtherVolumes() {
test_store.cpp:394: void TestForgetVolumeIsExplicitAndRemovesEntries() {
test_store.cpp:415: // tasks.md 9.1 proxy: a real "kill -9 mid-batch" can't be simulated from
test_store.cpp:421: // Store reopening the same file must still see it.
test_store.cpp:422: void TestWalReplayRecoversCommittedDataWithoutAnExplicitCheckpoint() {
test_store.cpp:439:     Store reopened;
test_store.cpp:440:     Check(reopened.Open(dbPath), "a database with an un-checkpointed WAL still opens cleanly");
test_store.cpp:441:     Check(reopened.CountEntries(vol) == 1, "WAL replay on open recovers committed data with no explicit prior checkpoint");
test_store.cpp:442:     Check(reopened.RunIntegrityCheck(), "the recovered database passes an integrity check");
test_store.cpp:445: void TestDataSurvivesReopenAfterClose() {
test_store.cpp:461:     Store reopened;
test_store.cpp:462:     Check(reopened.Open(dbPath), "database reopens after a clean close");
test_store.cpp:463:     Check(reopened.CountEntries(vol) == 1, "committed data survives close/reopen (WAL durability)");
test_store.cpp:466: void TestVacuumGatingByIdleIntervalAndGrowth() {
test_store.cpp:491: void TestGetFolderAggregateFromStore() {
test_store.cpp:527: void TestGetFolderAggregateWithAllocatedSizeAndCompleteness() {
test_store.cpp:687: void TestConcurrentReaderWriterWalContract() {
test_store.cpp:860:         // --- Phase C: teardown hygiene (D7) -- close both stores, reopen, and
test_store.cpp:864:         Store reopened;
test_store.cpp:865:         Check(reopened.Open(dbPath), (label + ": database reopens after both stores closed").c_str());
test_store.cpp:866:         Check(reopened.RunIntegrityCheck(),
test_store.cpp:867:               (label + ": integrity check passes after reopen").c_str());
test_store.cpp:868:         Check(reopened.CountEntries(vol) == static_cast<uint64_t>(kSeedCount),
test_store.cpp:869:               (label + ": durable row set is correct after reopen").c_str());
test_store.cpp:893: void TestIntegrityVerdictClassifier() {
test_store.cpp:917: void TestLockedDatabaseFailsOpenWithoutIntegrityFlag() {
test_store.cpp:953:     // The files survived: reopening once the lock releases finds the index intact.
test_store.cpp:968: void TestCorruptDatabaseSetsIntegrityFailedFlag() {
test_store.cpp:985:               "corrupt-verdict fixture file is reopenable for damage");
test_store.cpp:1004: void TestFindEntryPointLookup() {
test_store.cpp:1047: void TestPartialUpsertOfMissingRowZeroesAbsentFields() {
test_store.cpp:1081:     TestOpenCreatesSchemaAndIsReopenable();
test_store.cpp:1091:     TestDataSurvivesReopenAfterClose();
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine
Projection.cpp:148:         InvalidateFilenameIndex();
Projection.cpp:157:             RebuildFilenameIndex();
Projection.cpp:159:             InvalidateFilenameIndex();
Projection.cpp:410:         InvalidateFilenameIndex();
Projection.cpp:435:         InvalidateFilenameIndex();
Projection.cpp:450:     InvalidateFilenameIndex();
Projection.cpp:603: void Projection::InvalidateFilenameIndex() noexcept {
Projection.cpp:609: void Projection::AddFilenameIndexEntry(const uint32_t index) {
Projection.cpp:624:         InvalidateFilenameIndex();
Projection.cpp:628: void Projection::RemoveFilenameIndexEntry(const uint32_t index, const std::u16string_view name) {
Projection.cpp:631:         // Must use the exact predicate AddFilenameIndexEntry used, or stale
Projection.cpp:641:         InvalidateFilenameIndex();
Projection.cpp:645: void Projection::RebuildFilenameIndex() {
Projection.cpp:651:         idToIndex_.ForEachValue([&](const uint32_t index) { AddFilenameIndexEntry(index); });
Projection.cpp:662:         InvalidateFilenameIndex();
Projection.cpp:683:                 InvalidateFilenameIndex();
Projection.cpp:687:         if (filenameIndexValid_) RemoveFilenameIndexEntry(index, oldName);
Projection.cpp:712:         AddFilenameIndexEntry(index);
Projection.cpp:752:     AddFilenameIndexEntry(index);
Projection.cpp:844:         RemoveFilenameIndexEntry(index, namePool_.Get(entries_[index].nameId));
Projection.cpp:858: void Projection::RemoveVolume(VolumeRowId volumeRowId, bool keepFilenameIndexCurrent) {
Projection.cpp:867:     RemoveBatch(volumeRowId, frns, keepFilenameIndexCurrent);
Projection.cpp:871:                              bool keepFilenameIndexCurrent) {
Projection.cpp:879:     // caller either rebuild it once here (keepFilenameIndexCurrent, O(N)) or
Projection.cpp:886:         InvalidateFilenameIndex();
Projection.cpp:891:     if (bulk && wasValid && keepFilenameIndexCurrent) {
Projection.cpp:892:         RebuildFilenameIndex();
Projection.cpp:898:     if (!keepFilenameIndexCurrent) {
Projection.cpp:899:         InvalidateFilenameIndex();
Projection.cpp:1154:         // pathology). keepFilenameIndexCurrent=false skips the double rebuild.
Projection.cpp:1214:         RebuildFilenameIndex();
bench_large_index_scalability.cpp:156:     std::vector<size_t> scales{10'000, 100'000, 500'000, 1'000'000, 2'000'000};
bench_large_index_scalability.cpp:202:             std::wprintf(L"Usage: %s [--scales 10000,100000] [--output path] [--diagnostics]\n"
bench_large_index_scalability.cpp:203:                          L"       %s --memory-report <path> [--scales 200000,1000000] "
bench_large_index_scalability.cpp:208:         if (argument == L"--scales" && index + 1 < argc) {
bench_large_index_scalability.cpp:209:             options.scales = ParseScales(argv[++index]);
bench_large_index_scalability.cpp:210:             if (options.scales.empty()) {
bench_large_index_scalability.cpp:211:                 std::fwprintf(stderr, L"Invalid --scales value\n");
bench_large_index_scalability.cpp:319:     size_t scale = 0;
bench_large_index_scalability.cpp:363:     size_t scale = 0;
bench_large_index_scalability.cpp:448:     size_t scale = 0;
bench_large_index_scalability.cpp:458:     size_t scale = 0;
bench_large_index_scalability.cpp:711:     outcome.run.scale = scale;
bench_large_index_scalability.cpp:806:         if (run.scale == scale && run.queryClass == queryCase.name && run.temperature == "warm") {
bench_large_index_scalability.cpp:850:     run.scale = scale;
bench_large_index_scalability.cpp:1684:     size_t scale = 0;
bench_large_index_scalability.cpp:1704:     report.scale = scale;
bench_large_index_scalability.cpp:2005:     for (const size_t scale : options.scales) {
bench_large_index_scalability.cpp:2040:            << "\n  ,\"requested_scales\":[";
bench_large_index_scalability.cpp:2041:     for (size_t index = 0; index < options.scales.size(); ++index) {
bench_large_index_scalability.cpp:2043:         output << options.scales[index];
bench_large_index_scalability.cpp:2092:            << "  ,\"scales\":[";
bench_large_index_scalability.cpp:2147:     std::printf("FastFiles large-index scalability benchmark (%zu dataset scales)\n", options.scales.size());
bench_large_index_scalability.cpp:2149:     for (const size_t scale : options.scales) {
bench_large_index_scalability.cpp:2159:         dataset.scale = scale;
bench_large_index_scalability.cpp:2278:         if (scale == options.scales.front()) {
bench_large_index_scalability.cpp:2303:         if (scale == 1'000'000) {
bench_secondary_filename_index.cpp:679:     std::vector<size_t> scales{100'000, 500'000, 1'000'000, 2'000'000};
bench_secondary_filename_index.cpp:703:         if (argument == L"--scales" && index + 1 < argc) {
bench_secondary_filename_index.cpp:704:             options.scales = ParseScales(argv[++index]);
bench_secondary_filename_index.cpp:705:             if (options.scales.empty()) return false;
bench_secondary_filename_index.cpp:709:             std::wprintf(L"Usage: %s [--scales 100000,1000000] [--output path]\n", argv[0]);
bench_secondary_filename_index.cpp:804:     allScales.reserve(options.scales.size());
bench_secondary_filename_index.cpp:805:     for (const size_t scale : options.scales) {
bench_secondary_filename_index.cpp:838:     output << "\"},\"scales\":[";
bench_secondary_filename_index.cpp:841:         output << "{\"dataset_size\":" << options.scales[scaleIndex] << ",\"architectures\":[";
CMakeLists.txt:268: # CTest because its scales are selected for engineering evidence, not a
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/V...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/V...
            droppedWhilePaused = true;
        }
    }
    if (droppedBeforeScanAttemptStarted || droppedWhilePaused) {
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                {}, std::wstring(1, driveLetter),
                                droppedBeforeScanAttemptStarted
                                    ? L"scan-batch-before-scan-attempt-started"
                                    : L"scan-batch-dropped-paused-non-authoritative",
                                0, 0);
        return;
    }
    if (!apply(durableId)) {
        // task 3.4: leave the cursor untouched so the next batch attempt (or a full restart) can retry.
        // index-durability-false-success-closure D2 (PA-003): the service
        // keeps streaming later batches past a batch the store rejected,
        // and it will still report ScanComplete at the end. Mark this
        // scan's completion non-authoritative so that terminal can never
        // mark the volume indexed over the gap left at the frozen cursor.
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it != sessionsByEphemeralId_.end() && it->second.durableId == durableId) {
            it->second.scanBatchFailed = true;
        }
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::IndexingError,
                                ffprotocol::DiagnosticCode::FFDIAG_SCAN_BATCH_APPLY_FAILED,
                                {}, std::wstring(1, driveLetter),
                                L"scan-batch-apply-failed-cursor-frozen", 0,
                                static_cast<uint64_t>(cursor.size()));
        return;
    }
    pipeline_.SetScanCursor(durableId, cursor);
    RepublishSnapshot(durableId, driveLetter);
}
void VolumeSessionManager::OnScanComplete(ffprotocol::VolumeId volumeId) {
    ffindexstore::VolumeRowId durableId = 0;
    wchar_t driveLetter = L'\0';
    bool anyScanInProgress = false;
    bool wasReconciliationScan = false;
    bool batchFailed = false;
    bool deferred = false;
    bool paused = false;
    bool waitingForScanAttemptStarted = false;
    bool retryOwnerChanged = false;
    {
        // retry-ownership-and-focus-escape-closure D2: closing the attempt and installing its next retry owner are one
        // transition under the lock pair.
        std::scoped_lock lock(mutex_, wakeMutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()) {
            return;
        }
        durableId = it->second.durableId;
        driveLetter = it->second.driveLetter;
        if (it->second.awaitingScanAttemptStarted) {
            // scan-generation-integrity-and-residual-certification-closure
            // 3.4: all frames before ScanAttemptStarted are still attributable
            // to the superseded worker. The barrier is the first frame that
            // may grant the replacement's volume-only terminal authority.
            waitingForScanAttemptStarted = true;
        } else if (!it->second.scanInProgress) {
            // index-durability-false-success-closure D2: a completion for
            // a volume with no scan in flight (duplicate, or one that
            // raced pause/teardown bookkeeping) has no authority over the
            // index at all.
            AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                    ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                    {}, std::wstring(1, it->second.driveLetter),
                                    L"scan-complete-without-scan-in-progress", 0, 0);
            return;
        } else {
            wasReconciliationScan = it->second.reconciliationScan;
            batchFailed = it->second.scanBatchFailed;
            deferred = it->second.deferredReconciliation;
            paused = IsPausedLocked(driveLetter);
            it->second.scanInProgress = false;
            it->second.reconciliationScan = false;
            it->second.scanBatchFailed = false;
            it->second.deferredReconciliation = false;
            anyScanInProgress = std::any_of(
                sessionsByEphemeralId_.begin(), sessionsByEphemeralId_.end(),
                [](const auto& entry) { return entry.second.scanInProgress; });
            if (batchFailed) {
                // scan-generation-integrity-and-residual-certification-closure
                // D2/D4: coverage that could not be authorized is retried from
                // the last durably applied position (or as its own
                // reconciliation) under the same critical section that closed
                // the attempt, so a disconnect re-entering from the publication
                // below cannot observe the intent unowned.
                InstallNonAuthoritativeRetryLocked(volumeId.value, durableId,
                                                   std::chrono::steady_clock::now() + kScanFailureRetryDelay,
                                                   wasReconciliationScan);
                retryOwnerChanged = true;
            }
        }
    }
    if (retryOwnerChanged) {
        wakeCv_.notify_one();
    }
    if (waitingForScanAttemptStarted) {
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                {}, std::wstring(1, driveLetter),
                                L"scan-complete-before-scan-attempt-started", 0, 0);
        return;
    }
    pipeline_.SetBulkScanActive(anyScanInProgress);
    if (paused || batchFailed) {
        // index-durability-false-success-closure D2/D5 (PA-001/PA-005):
        // ScanComplete is only authority when the scan actually ran to the
        // end of the MFT with every batch applied. A completion received
        // while paused (batches were dropped) or after any batch failed to
        // apply must not mark the volume indexed or finalize a reconciliation
        // pass on a seen-set with guaranteed holes -- that finalization is exactly
        // what deletes live rows.
[573 more lines in file. Use offset=1020 to continue.]
VolumeSessionManager.cpp:234:     connection_.SetScanCompleteCallback([this](ffprotocol::VolumeId id) { OnScanComplete(id); });
VolumeSessionManager.cpp:332:         // reconciliation starts fresh through BeginReconciliationPass.
VolumeSessionManager.cpp:335:                 pipeline_.AbortReconciliationPass(durableId);
VolumeSessionManager.cpp:393:             // pipeline so ApplyMftBatch/ApplyUsnBatch/reconciliation/rebuild
VolumeSessionManager.cpp:772:             // BeginReconciliationPass clearing the first pass's seen-set
VolumeSessionManager.cpp:824:     pipeline_.BeginReconciliationPass(durableId);
VolumeSessionManager.cpp:852:         return pipeline_.ApplyMftBatch(durableId, records);
VolumeSessionManager.cpp:862:         return pipeline_.ApplyMftBatch(durableId, records);
VolumeSessionManager.cpp:918:         // and it will still report ScanComplete at the end. Mark this
VolumeSessionManager.cpp:937: void VolumeSessionManager::OnScanComplete(ffprotocol::VolumeId volumeId) {
VolumeSessionManager.cpp:1014:         // ScanComplete is only authority when the scan actually ran to the
VolumeSessionManager.cpp:1021:             pipeline_.AbortReconciliationPass(durableId);
VolumeSessionManager.cpp:1042:     pipeline_.MarkScanComplete(durableId);
VolumeSessionManager.cpp:1052:             pipeline_.FinishReconciliationPass(durableId);
VolumeSessionManager.cpp:1064:         pipeline_.AbortReconciliationPass(durableId);
VolumeSessionManager.cpp:1140:         pipeline_.AbortReconciliationPass(durableId);
VolumeSessionManager.cpp:1205:         return pipeline_.ApplyUsnBatch(durableId, records);
VolumeSessionManager.cpp:1214:         return pipeline_.ApplyUsnBatch(durableId, records);
VolumeSessionManager.cpp:1564:                 pipeline_.AbortReconciliationPass(retry.durableId);
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/V...
    wchar_t driveLetter = L'\0';
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()
            || !it->second.scanInProgress
            || !it->second.awaitingScanAttemptStarted) {
            // Initial scans also receive the marker at {2,3}; only a
            // coverage-hole replacement has provisional authority to release.
            return;
        }
        driveLetter = it->second.driveLetter;
        it->second.awaitingScanAttemptStarted = false;
        // The replacement starts from the frozen durable cursor, so the old
        // attempt's coverage hole no longer poisons frames ordered after this
        // service-side stop-and-join barrier.
        it->second.scanBatchFailed = false;
    }
    AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                            ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                            {}, std::wstring(1, driveLetter),
                            L"scan-attempt-started-barrier-accepted", 0, 0);
}
void VolumeSessionManager::OnJournalOpened(ffprotocol::VolumeId volumeId, uint64_t journalId, uint64_t currentUsn) {
    ffindexstore::VolumeRowId durableId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()) {
            return;
        }
        it->second.journalId = journalId;
        it->second.journalIdKnown = true;
        it->second.journalOpenRequested = true;
        durableId = it->second.durableId;
    }
    auto meta = pipeline_.GetVolumeMetadata(durableId);
    if (meta && meta->journalId.has_value() && *meta->journalId != journalId) {
        // design.md D6: the journal was deleted/recreated since we last
        // held a position for it -- the ResumeUsn we just requested is
        // almost certainly invalid for this JournalId, so reset to the
        // journal's current position and reconcile instead of silently
        // missing whatever changed in the gap.
        pipeline_.SetJournalPosition(durableId, journalId, currentUsn);
        TriggerReconciliation(volumeId, durableId);
    } else {
        pipeline_.SetJournalPosition(durableId, journalId, meta ? meta->resumeUsn : currentUsn);
    }
}
void VolumeSessionManager::OnJournalBatch(
    ffprotocol::VolumeId volumeId, uint64_t latestUsn, std::vector<ffprotocol::UsnDeltaV2> records) {
    ApplyJournalBatch(volumeId, latestUsn, [&](ffindexstore::VolumeRowId durableId) {
        return pipeline_.ApplyUsnBatch(durableId, records);
    });
}
void VolumeSessionManager::OnJournalBatchV3(
    ffprotocol::VolumeId volumeId, uint64_t latestUsn, std::vector<ffprotocol::UsnDeltaV3> records) {
    // allocated-size-storage-accuracy 5.2: identical lifecycle to the V2
    // path; only the record type (and thus allocated-size presence) differs.
    ApplyJournalBatch(volumeId, latestUsn, [&](ffindexstore::VolumeRowId durableId) {
        return pipeline_.ApplyUsnBatch(durableId, records);
    });
}
void VolumeSessionManager::ApplyJournalBatch(ffprotocol::VolumeId volumeId, uint64_t latestUsn,
                                             const std::function<bool(ffindexstore::VolumeRowId)>& apply) {
    ffindexstore::VolumeRowId durableId = 0;
    wchar_t driveLetter = L'\0';
    uint64_t journalId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end() || !it->second.journalIdKnown) {
            return;
        }
        durableId = it->second.durableId;
        driveLetter = it->second.driveLetter;
        journalId = it->second.journalId;
        // settings-and-appearance §9.1: same drop-without-advancing rule
        // as scan batches; the stored ResumeUsn is the resume point.
        if (IsPausedLocked(driveLetter)) {
            return;
        }
    }
    if (!apply(durableId)) {
        // index-durability-false-success-closure D3/4.4 (PA-002 gap
        // closure): an un-applied journal batch is a coverage hole the
        // position advance must not paper over. The stored ResumeUsn stays
        // behind (monotonic replay is safe -- re-applied events upsert),
        // and a reconciliation sweep is requested to close the gap; with
        // D4's deferral this is idempotent while any scan is in flight.
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::IndexingError,
                                ffprotocol::DiagnosticCode::FFDIAG_SCAN_BATCH_APPLY_FAILED,
                                {}, std::wstring(1, driveLetter),
                                L"journal-batch-apply-failed-reconciliation-requested", 0, 0);
        TriggerReconciliation(volumeId, durableId);
        return;
    }
    pipeline_.SetJournalPosition(durableId, journalId, latestUsn);
    RepublishSnapshot(durableId, driveLetter);
}
void VolumeSessionManager::OnJournalResumeInvalid(ffprotocol::VolumeId volumeId) {
    ffindexstore::VolumeRowId durableId = 0;
    uint64_t journalId = 0;
    bool journalIdKnown = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()) {
            return;
        }
        durableId = it->second.durableId;
        journalId = it->second.journalId;
        journalIdKnown = it->second.journalIdKnown;
    }
    // design.md D6, tasks.md 7.6: the persisted ResumeUsn falls outside
    // the journal's retained range (the journal wrapped during a long
    // disconnection). Same policy as OnJournalOpened's JournalId-mismatch
    // branch: stop trusting the persisted position and reconcile instead
    // of silently missing the changes in the gap. JournalResumeInvalid
    // carries no currentUsn, so the position resets to the journal's
    // start and the journal is reopened from there using the existing
    // OpenUsnJournal request -- the batches from that reopened journal
    // plus the reconciliation pass's full scan cover the gap.
    if (journalIdKnown) {
        pipeline_.SetJournalPosition(durableId, journalId, 0);
    }
    TriggerReconciliation(volumeId, durableId);
    RequestJournalOpen(volumeId, 0, durableId);
}
void VolumeSessionManager::RepublishSnapshot(ffindexstore::VolumeRowId durableId, wchar_t driveLetter, bool force) {
    if (!onSnapshotReady_) {
        return;
    }
    constexpr auto kSnapshotPublishInterval = std::chrono::milliseconds(500);
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool scanInProgress = std::any_of(
            sessionsByEphemeralId_.begin(), sessionsByEphemeralId_.end(),
            [durableId](const auto& entry) {
                return entry.second.durableId == durableId && entry.second.scanInProgress;
[293 more lines in file. Use offset=1300 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/indexstore...
# Vendored SQLite amalgamation, built as a small static library.
# SQLITE_THREADSAFE=1 (serialized mode): the engine's ingestion pipeline,
# reconciliation sweeps, and diagnostic reads (filesystem-index-store design.md
# D1) all touch the same sqlite3* connection pool from different threads.
add_library(sqlite3 STATIC sqlite3.c)
source_group("SQLite" FILES sqlite3.c sqlite3.h)
target_sources(sqlite3 PRIVATE sqlite3.h)
set_target_properties(sqlite3 PROPERTIES FOLDER "Third Party/SQLite")
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sqlite3 PUBLIC
  SQLITE_THREADSAFE=1
  SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
  SQLITE_OMIT_DEPRECATED
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_ENABLE_FTS3=0
)
if(MSVC)
  # Vendored, unmodified third-party C source -- do not hold it to this
  # project's own /W4 /WX bar (task 1.1 note: "bundled/statically-linked
  # dependency", not first-party code).
  target_compile_options(sqlite3 PRIVATE /W0 /wd4100 /wd4267)
  # Silence the same source's warnings without disabling the whole-project
  # /WX for anyone consuming this target.
  set_source_files_properties(sqlite3.c PROPERTIES COMPILE_FLAGS "/W0")
endif()
test_projection.cpp:367:     const auto candidates = proj.FilenameCandidateIndices(u"component_7391");
test_projection.cpp:375:     Check(!proj.FilenameCandidateIndices(u"ph").has_value(), "two-character terms use the canonical fallback");
test_projection.cpp:376:     Check(!proj.FilenameCandidateIndices(u"\u6771\u4EAC").has_value(), "Unicode terms use the canonical fallback");
test_projection.cpp:379:     auto afterRemove = proj.FilenameCandidateIndices(u"component_7391");
test_projection.cpp:384:     auto afterRename = proj.FilenameCandidateIndices(u"component_7391");
test_projection.cpp:580:     Check(!proj.FilenameCandidateIndices(u"deferred").has_value(),
test_projection.cpp:977:     const auto referenceCandidates = reference.FilenameCandidateIndices(u"common_component_keep");
test_projection.cpp:978:     const auto bulkCandidates = bulk.FilenameCandidateIndices(u"common_component_keep");
test_projection.cpp:1006:     const auto candidates = proj.FilenameCandidateIndices(u"common_component_keep");
test_projection.cpp:1027:     Check(!proj.FilenameCandidateIndices(u"common_component_keep").has_value(),
test_projection.cpp:1076: void TestFilenameCandidatesAreConservativeSuperset() {
test_projection.cpp:1085:         const auto candidates = proj.FilenameCandidateIndices(folded, 0);  // uncapped reference
test_projection.cpp:1110:     Check(!proj.FilenameCandidateIndices(u"ab", 0).has_value(),
test_projection.cpp:1112:     Check(!proj.FilenameCandidateIndices(u"\u0441\u043f\u0438\u0441\u043e\u043a", 0).has_value(),
test_projection.cpp:1121:     const auto candidates = proj.FilenameCandidateIndices(u"unique_target", 8);
test_projection.cpp:1128:     const auto missing = proj.FilenameCandidateIndices(u"zzz_missing", 8);
test_projection.cpp:1189:     Projection::SetTestingFoldExceptionalUnit(u'\u017F', true);
test_projection.cpp:1196:         const auto candidates = proj.FilenameCandidateIndices(u"soo");
test_projection.cpp:1208:         const auto afterRename = proj.FilenameCandidateIndices(u"soo");
test_projection.cpp:1213:         const auto afterRemove = proj.FilenameCandidateIndices(u"soo");
test_projection.cpp:1217:     Projection::SetTestingFoldExceptionalUnit(u'\u017F', false);
test_projection.cpp:1236:     const auto candidates = proj.FilenameCandidateIndices(u"xyz");
test_projection.cpp:1248: // FilenameCandidateIndices(query). Adversarial cross-casing corpus: long s,
test_projection.cpp:1303:         const auto candidates = proj.FilenameCandidateIndices(folded, 0);
test_projection.cpp:1311:         const auto capped = proj.FilenameCandidateIndices(folded, 4096);
test_projection.cpp:1323:         const auto candidates = proj.FilenameCandidateIndices(FoldAsciiLower(needle), 0);
test_projection.cpp:1338:     Check(proj.FilenameCandidateIndices(u"unique_target").has_value(),
test_projection.cpp:1342:     Check(!proj.FilenameCandidateIndices(u"unique_target").has_value(),
test_projection.cpp:1347:     const auto candidates = proj.FilenameCandidateIndices(u"unique_target");
test_projection.cpp:1433:     TestFilenameCandidatesAreConservativeSuperset();
## Review — Lane E: migration & crash consistency (read-only research)
Target: `src/indexstore` (Store/Projection), `src/engine` mutation paths, migration/crash posture for the proposed durable candidate index (`openspec/changes/durable-cold-search-index-and-hybrid-residency/`).
### Correct (evidence)
- **Schema v2 is small, additive-friendly, and version-fenced.** `kCurrentSchemaVersion = 2` (`src/indexstore/src/Store.cpp:22`); tables `volumes` + `entries` (`WITHOUT ROWID`, PK `(volume_id, frn_low, frn_high)`) + one parent index (`Store.cpp:24-55`); v1→v2 was a metadata-only `ALTER TABLE ADD COLUMN` (`kMigrateV1ToV2Sql`, `Store.cpp:57-59`, comment 53-56).
- **Open-path ordering is right:** integrity check first (`Store.cpp:341-364`, corrupt-vs-inconclusive classifier `Store.cpp:279-296`), then `journal_mode=WAL`, `synchronous=NORMAL`, `foreign_keys=ON` (`Store.cpp:369-370`), then `CREATE TABLE IF NOT EXISTS` (`Store.cpp:375`), then version detection (`Store.cpp:380-411`). **A newer-than-understood schema is refused with `outIntegrityFailed=false`** (`Store.cpp:408-411`) — so a v2 app on a v3 DB fails closed and is *never* routed into `Main.cpp`'s delete-and-rescan path (delete only fires on `integrityFailed`, `src/engine/src/Main.cpp:181-199`; unopenable-but-uncorrupt is retried 3× then indexing disabled, `Main.cpp:173-222`).
- **All entries mutations funnel through exactly two write transactions:** `Store::ApplyBatch` = `BEGIN IMMEDIATE … COMMIT` with rollback on any statement or commit failure (`Store.cpp:771-874`; commit-failure contract comment 866-868) and `Store::ForgetVolume` (`Store.cpp:711-759`). Everything else (volume metadata, cursors, journal position, `GetOrCreateVolume` insert at `Store.cpp:511-535`) is single-statement autocommit. Read plane is a separate `BEGIN` snapshot with query-only enforcement (`StoreReadSession::Open`, `Store.cpp:1116-1143`).
- **Pipeline commit-before-apply ordering is consistent** (design D4): durable commit first, then in-memory projection (`IndexPipeline.cpp:730-734`, 858-862, `ForgetVolume` 987-1000), with fail-closed secondary-prune semantics and retained obligations on failure (`IndexPipeline.cpp:1064-1072`).
- **Consistency markers/epochs already exist in three layers:** durable resume markers (`scan_cursor`/`scan_complete`/`resume_usn`/`journal_id`/`last_reconciliation_time`, `Store.h:37-50`), in-memory query-visible/data/config/residency epochs (`IndexPipeline.cpp:400-440`), and snapshot generation publication. Replay/monotonic-recovery semantics are explicit: failed journal batch leaves `ResumeUsn` behind and triggers reconciliation (`VolumeSessionManager.cpp:1219-1235`), journal-identity mismatch resets position + reconciles (`VolumeSessionManager.cpp:1180-1192`).
- **Rebuild-on-open precedent:** projection (incl. its trigram accelerator) is rebuilt from the store at startup (`Main.cpp:288`; `RebuildFilenameIndex` bulk pattern `src/indexstore/src/Projection.cpp:645-662`, 871-899) and the accelerator's validity flag + invalidate/rebuild discipline (`filenameIndexValid_`, `Projection.cpp:603-641`) is the exact model for a durable candidate structure's state marker.
- **Candidate semantics to mirror are already proven:** conservative-superset contract (`Projection.h:327-341`), fold-exceptional/surrogate side-set with host-derived probe (`Projection.h:311-330`), oracle tests (`tests/indexstore/test_store.cpp` untouched; `tests/indexstore/test_projection.cpp:1076-1247`, cap test 1303-1338).
### Findings
- **Finding: P1 — the existing v1→v2 migration is crash-fragile and non-idempotent; the same hazard must not recur in v3.** `src/indexstore/src/Store.cpp:392-407`. The `ALTER TABLE` (autocommit) and the `PRAGMA user_version=2` bump are two separate durable steps. A crash between them leaves the column present but `user_version=1`; on the next open `kMigrateV1ToV2Sql` re-runs and **fails permanently** ("duplicate column name" → `Open` false, `outIntegrityFailed=false`), so `Main.cpp` retries 3× and disables indexing (`Main.cpp:173-222`) — the DB never opens again and no repair path runs. This contradicts the successor spec's interrupted-migration scenario ("the store opens at the previous schema version with all entries intact", `openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/index-engine/spec.md`, "Interrupted migration"). Smallest fix: wrap `kMigrateV1ToV2Sql` + version bump in one `BEGIN IMMEDIATE … COMMIT` (SQLite DDL is transactional) or gate the ALTER on `PRAGMA table_info(entries)` column presence. (Window is short — ALTER is O(1) — but this lane's whole subject is exactly that window.)
- **Finding: P2 — no logical-corruption detection exists beyond `PRAGMA integrity_check`;** schema carries zero `CHECK` constraints and there are no verification queries (`Store.cpp:24-55`, only integrity usage `Store.cpp:314`). Fine for v2 (invariants held by construction inside `ApplyBatch`), but a v3 derived structure needs its own verification seam (build-completion count cross-check, orphan-posting anti-join/lookup filter, reopen consistency gate) because `integrity_check` will not see posting-vs-entries disagreement.
- **Finding: P2 — `user_version == 0` is treated as "fresh" unconditionally** (`Store.cpp:381-390`): a legacy DB holding v1-shaped tables without the marker would be bumped to 2 without the ALTER, after which every `ApplyBatch` prepare fails (missing `allocated_size_bytes`). Reachable only if some historical build created tables without setting the marker (the v1 fixture does set it, `tests/indexstore/test_store.cpp:73`), but the v3 open path should detect table-shape-vs-marker disagreement rather than trusting the marker alone.
- **Finding: P2 — true kill-during-write is acknowledged untested today.** `tests/indexstore/test_store.cpp:415-421` explicitly says "a real kill -9 mid-batch can't be simulated from within this process" and substitutes a WAL-replay proxy (422-443). For a multi-million-row migration this gap is load-bearing; see matrix seam S2 below.
### Crash-interruption surface map (paths that would touch entries + candidate index)
| # | Mutation path | Entry side (evidence) | Candidate-index side | Existing multi-transaction hazard |
|---|---|---|---|---|
| 1 | Initial creation | `GetOrCreateVolume` (`Store.cpp:511`) + `ApplyMftBatchChanges` (`IndexPipeline.cpp:727-806`) + `SetScanCursor` (`VolumeSessionManager.cpp:933`) | build postings for each upsert batch | batch-commit → cursor-write is already non-atomic (tolerated: cursor frozen on failure, `VolumeSessionManager.cpp:912-934`) |
| 2 | Large migration v2→v3 | none (entries untouched) | full build over 2M-5M rows | the P1 pattern: build steps vs `user_version` bump |
| 3 | Incremental insert | `ApplyBatch` upsert (`Store.cpp:762-874`) | add trigrams for new name | — |
| 4 | Delete | `Remove` change (USN delete flag, `IndexPipeline.cpp:820-824` → `Store.cpp:782-786`) | drop postings | — |
| 5 | Rename | masked upsert name+parent (`kEntryFieldParentId/Name`, `EntryRecord.h:35-36`; CASE-masked UPDATE `Store.cpp:773-795`) | remove old-name postings + add new (two derivations, one row change) | — |
| 6 | USN batch (mixed) | `ApplyUsnBatchChanges` (`IndexPipeline.cpp:853-944`) | per-change posting deltas | commit → `SetJournalPosition` (`VolumeSessionManager.cpp:1253`) already non-atomic (replay idempotent) |
| 7 | Reconciliation | `FinishReconciliationPass` mass `Remove` batch (`IndexPipeline.cpp:1020-1078`) | mass posting removal | second transaction after scan batches; obligation retained on failure (1064-1072) |
| 8 | Bulk replacement | `RebuildAll` rule-prune second `ApplyBatch` (`IndexPipeline.cpp:607`), `RemoveBatch`/`RebuildFilenameIndex` (`Projection.cpp:871-899`) | bulk rebuild (defer + single rebuild) | rule-prune is a *separate* transaction from the primary batch — exactly the coupling class v3 must not add to |
| 9 | Database reopen | `Store::Open` (`Store.cpp:341-425`) + `RebuildAll` (`Main.cpp:288`) | validate/rebuild per state marker | WAL replay under kill untested (finding above) |
### Fault-injection seams available today
- Read plane: `ReadSessionFaultPoint` (Open/BeginSnapshot/Busy/DatabaseClosed/Stream/FindEntry/Aggregate) — `Store.h:20-33`, `Store.cpp:1112-1113`; exercised `tests/indexstore/test_store_read_session.cpp:167-217`, `tests/engine/test_index_pipeline_concurrency.cpp:266-290`.
- Write plane (pipeline-level, named boundaries): `SecondaryPruningFaultInjector` + `SecondaryPruningBoundary` (MftRulePrune/UsnRulePrune/RebuildRulePrune/ReconciliationRemoval), test-gated by `FF_INDEX_PIPELINE_TESTING` (`IndexPipeline.h:183-194`, `IndexPipeline.cpp:478-489`, `tests/engine/CMakeLists.txt:10`, tests `tests/engine/test_index_pipeline.cpp:47-90`, 1876-2248).
- Projection maintenance: `SetRemovalMaintenanceMode` / `SetTestingFoldExceptionalUnit`, `FF_INDEXSTORE_TESTING` only (`Projection.h:300-330`; `tests/indexstore/CMakeLists.txt:13,41`).
- Service side: `RunVolumeScanWithFailureHookForTesting` (`src/indexsvc/src/VolumeScanner.h:142`, `.cpp:570`), `StallFixture` (`tests/indexsvc/test_volume_scanner_terminal.cpp:1021`).
- Fixtures: `CreateVersion1Fixture` (`tests/indexstore/test_store.cpp:33-80`), corrupt-file (968-1000), exclusive-lock (917-960), WAL-replay proxy (422-443).
- **Missing:** a Store-side write/migration fault seam and a process-kill harness (findings above).
### Recommended migration architecture (ranked pair)
**Rank 1 — Same-DB additive schema v3, transactionally coupled (alternative B: durable trigram postings).**
- New derived tables only (entries/volumes untouched): e.g. `name_trigrams(volume_id, trigram, frn_low, frn_high) WITHOUT ROWID` + `name_exceptions` (fold-exceptional/surrogate side-set, mirroring `Projection.h:488-492`) + `candidate_index_state(volume_id, state, entries_epoch/rowcount, built_time)`.
- **Maintenance runs inside the existing `ApplyBatch`/`ForgetVolume` transactions** (`Store.cpp:771-874`, 711-759) — the single reason this rank wins: under WAL + `synchronous=NORMAL` (`Store.cpp:369`), crash loses "the last transactions" *as a unit*; entries and postings can never disagree. Any second-transaction design recreates the rule-prune coupling class and the spec-forbidden "candidates for rows that do not exist" window.
- Migration ordering (fixes the P1 pattern): (1) create derived tables (`user_version` stays 2), (2) build per volume in bounded batched transactions with `state=building→ready` flips and `CheckpointIfWalExceeds` throttling (`Store.cpp:435-491`), (3) `ANALYZE` (`Store.cpp:493-496`), (4) `PRAGMA user_version=3` **last**, in the final transaction. Interrupt anywhere ⇒ DB is still v2-openable (spec's interrupted-migration scenario), v2 app opens normally and ignores the extra tables (`CREATE TABLE IF NOT EXISTS` ordering, `Store.cpp:375`), v3 app drops/rebuilds any `state=building` volume.
- **Rollback vs downgrade (document distinctly):** *rollback* = drop derived tables + `user_version=2` reset — trivially lossless (everything v3 adds is derived) and should be a documented procedure; *interrupted migration* = automatic, stays at v2; *downgrade (older app, v3 DB)* = fail closed at `Store.cpp:408-411` — indexing disabled, data preserved, **must not** touch the `integrityFailed` delete path (already guaranteed). Do not weaken the newer-version refusal.
- Corruption detection: build-completion count cross-check (distinct posting rows vs entries), lookup-time orphan filtering (cheap inside the existing `kMaximumFilenameIndexCandidates = 16,384` cap), reopen verification query; add `CHECK(state BETWEEN 0 AND 2)`-style constraints since v2 has none.
- Keeps the proven superset oracle intact: the durable structure mirrors the in-memory accelerator whose contract is already differential-tested (`test_projection.cpp:1076-1247`).
**Rank 2 — Separate derived file (`index-candidates.db`) with verify-or-rebuild-on-open.** Only if measurement shows in-DB posting write amplification breaks WAL/ingestion gates. Costs: no cross-file atomicity, so the file must be treated as pure cache — never trusted after restart without full verification or per-volume rebuild (spec fallback-to-scan covers the gap); requires either a tiny additive entries-side commit counter (itself a schema change) or mandatory rebuild-on-open; adds a second file to `DeleteIndexDatabaseFiles` (`Main.cpp:83-96`) and the corruption policy.
**Rejected/deranked:** FTS5 (alternative A) is already measured and rejected — the vendored SQLite is built without FTS5 (`third_party/sqlite/CMakeLists.txt:12-16`, no `SQLITE_ENABLE_FTS5` anywhere; `tests/engine/bench_secondary_filename_index.cpp:848-850`: `"rejected_fts_reason":"vendored_sqlite_is_not_compiled_with_fts5_and_create_fails"`, selected `utf16_trigram_postings`). Enabling it would also enlarge the untrusted-name-parsing surface and still fails the fold-exceptional superset proof without an exception side-set (`design.md` §2/§5).
### Exact crash-test matrix
Seams to add (small): **S1** `Store` write/migration fault injector (mirror of `ReadSessionFaultPoint`; test-gated like `SetRemovalMaintenanceMode`) with named points inside `ApplyBatch`/`ForgetVolume`/migration; **S2** child-process `TerminateProcess` kill harness (the acknowledged gap, `test_store.cpp:415-421`); **S3** reuse `SecondaryPruningFaultInjector` boundaries as-is for pipeline-level ordering.
| ID | Path | Injection | Expected after reopen |
|---|---|---|---|
| C1a | Initial creation | S1 fail before first `ApplyBatch` COMMIT | empty DB; both structures empty; scan retry OK |
| C1b | Initial creation | S2 kill between batch COMMIT and `SetScanCursor` | cursor frozen; batch replay idempotent; postings idempotent |
| C2a | Migration | S2 kill after derived CREATE, before build | `user_version=2`; v2 app opens; v3 rebuilds derived state |
| C2b | Migration | S2 kill mid-volume build (25/50/75%) | that volume `state=building` → rebuilt; others untouched |
| C2c | Migration | S2 kill after build, before `user_version=3` | still v2-openable, entries intact; v3 verifies/finishes |
| C2d | Migration | S2 kill during version bump | v2 or v3, never "v3-partial"; both openable per rules |
| C2e | Downgrade | v2 `Store::Open` on completed v3 DB | `Open=false`, `integrityFailed=false`, zero deletion (extend `TestLockedDatabaseFailsOpenWithoutIntegrityFlag` pattern, `test_store.cpp:917-960`) |
| C3a | Incremental insert | S1 fail inside `ApplyBatch` before COMMIT | row + postings both absent; retry succeeds |
| C3b | Incremental insert | kill after COMMIT before journal-position write | replay upserts idempotent (`VolumeSessionManager.cpp:1219-1235` semantics) |
| C4a | Delete | S1 mid-transaction | row + postings both present or both absent |
| C4b | Delete | orphan-posting fixture (test-only INSERT) | query never returns candidates for nonexistent rows (lookup filter or open-time cleanup) |
| C5a–c | Rename | S1 fail at each statement of name-change (entries UPDATE, old-postings DELETE, new-postings INSERT) | all-or-nothing; old-name query consistent pre-commit, new-name post-commit |
| C6a | USN batch (mixed) | S1 mid-loop (`Store.cpp:833-861`) | whole batch incl. postings rolls back |
| C6b | USN batch (partial mask, no name field) | assert | postings untouched (analog of `TestPartialUpsertPreservesAbsentAuthoritativeFields`, `test_store.cpp:277`) |
| C7a | Reconciliation removals | S3 `ReconciliationRemoval` fail | entries+postings retained; obligation retained (`IndexPipeline.cpp:1064-1072`); extend `test_index_pipeline.cpp:1991-2248` |
| C7b | Reconciliation | kill between removals COMMIT and `reconciliationSeen_.erase` | reopen re-derives; no posting orphans |
| C8a | Bulk replacement (rule-prune) | S3 `RebuildRulePrune` fail | retained rows consistent across entries+postings |
| C8b | Bulk posting rebuild | S2 kill mid-rebuild | `state=building` → rebuilt before consultation; fallback scan meanwhile |
| C9a | Reopen | S2 kill during WAL replay (looped) | open converges; integrity `Ok` (`Store.cpp:341-364`) |
| C9b | Reopen | stale marker: entries advanced past `entries_epoch` | affected volume rebuilt; results equal canonical scan |
### Migration measurement design (2M/5M)
Harness: extend `tests/engine/bench_large_index_scalability.cpp` (scale parameterization `:156`, JSON with identity `:2034-2043`) and `bench_secondary_filename_index.cpp` (memory sampling `ReadMemory` `:34-44`, FTS capability probe `:632-673`); add `--scales 2000000,5000000`. Copy the evidence discipline of `bench_rebuild_isolation.cpp` (FF_INDEXSTORE_TESTING arms, limitations strings `:692`) and `design.md` §3 (idle gating, medians hot / disclosed singles cold, artifacts embed `--source-sha`).
Per run record: (1) wall time per phase (create / per-volume build / version bump / ANALYZE); (2) disk peak = max sampled `index.db + -wal + -shm + temp` (~100 ms sampling); (3) WAL peak = max `-wal` bytes, with vs without `CheckpointIfWalExceeds` throttling (interaction at `Store.cpp:435-491`); (4) memory peak = private bytes + working set; (5) interruption recovery = C2 kill harness at 25/50/75% → reopen latency, rebuild scope, total to-queryable; (6) verification time = `PRAGMA integrity_check` + posting-consistency query at 2M/5M; (7) gates: per-USN-batch apply delta with posting maintenance at 1M/2M/5M (proposal: no material regression vs certified 31-34× rebuild at 1M), index bytes per 1M rows per alternative, query-side candidate latency + fallback rate across the 12 query classes.
### Merge verdict: OK with notes
(Research deliverable; findings are pre-existing seams, not diff-caused. The P1 migration idempotency fix should be picked up before any v3 implementation task group starts.)
