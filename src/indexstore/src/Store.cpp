#include "ffindexstore/Store.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>

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
    std::u16string out;
    out.reserve(static_cast<size_t>(lengthBytes));
    size_t i = 0;
    const auto bytes = reinterpret_cast<const unsigned char*>(data);
    const size_t size = static_cast<size_t>(lengthBytes);
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

} // namespace

Store::~Store() {
    Close();
}

bool Store::RunIntegrityCheck() {
    if (db_ == nullptr) {
        return false;
    }
    Statement stmt(db_, "PRAGMA integrity_check;");
    if (!stmt.Valid()) {
        return false;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        return false;
    }
    const unsigned char* text = sqlite3_column_text(stmt, 0);
    return text != nullptr && std::strcmp(reinterpret_cast<const char*>(text), "ok") == 0;
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
    if (!RunIntegrityCheck()) {
        if (outIntegrityFailed != nullptr) {
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
    // the handle's actual page size; fall back to 4096 only if the query fails.
    int pageSize = 4096;
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "PRAGMA page_size;", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const int p = sqlite3_column_int(stmt, 0);
                if (p > 0) pageSize = p;
            }
            sqlite3_finalize(stmt);
        }
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
        (lastVacuumSizeBytes_ > std::numeric_limits<uint64_t>::max() / sizeGrowthPercent ||
         growth <= (lastVacuumSizeBytes_ * sizeGrowthPercent) / 100)) {
        return false;
    }
    if (!ExecSql(db_, "VACUUM;")) return false;
    lastVacuumTimeSeconds_ = nowEpochSeconds;
    lastVacuumSizeBytes_ = DatabaseFileSize(dbPathUtf8_);
    return true;
}

std::optional<VolumeRowId> Store::GetOrCreateVolume(const VolumeKey& key) {
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

bool Store::SetVolumeAvailable(VolumeRowId volumeRowId, bool available, uint64_t nowTimestamp) {
    if (db_ == nullptr) {
        return false;
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
            sqlite3_reset(upsert);
            sqlite3_bind_int64(upsert, 1, static_cast<sqlite3_int64>(volumeRowId));
            sqlite3_bind_int64(upsert, 2, static_cast<sqlite3_int64>(record.id.low));
            sqlite3_bind_int64(upsert, 3, static_cast<sqlite3_int64>(record.id.high));
            sqlite3_bind_int64(upsert, 4, static_cast<sqlite3_int64>(record.parentId.low));
            sqlite3_bind_int64(upsert, 5, static_cast<sqlite3_int64>(record.parentId.high));
            sqlite3_bind_text(upsert, 6, nameUtf8.data(), static_cast<int>(nameUtf8.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(upsert, 7, static_cast<sqlite3_int64>(record.sizeBytes));
            if (record.allocatedSizeKnown) {
                sqlite3_bind_int64(upsert, 8, static_cast<sqlite3_int64>(record.allocatedSizeBytes));
            } else {
                // Unknown is NULL on disk -- a genuine zero is stored as 0.
                sqlite3_bind_null(upsert, 8);
            }
            sqlite3_bind_int64(upsert, 9, static_cast<sqlite3_int64>(record.creationTime));
            sqlite3_bind_int64(upsert, 10, static_cast<sqlite3_int64>(record.lastModifiedTime));
            sqlite3_bind_int64(upsert, 11, static_cast<sqlite3_int64>(record.lastAccessTime));
            sqlite3_bind_int64(upsert, 12, static_cast<sqlite3_int64>(record.attributes));
            const sqlite3_int64 fields = static_cast<sqlite3_int64>(change.fields);
            for (int parameter = 13; parameter <= 21; ++parameter) {
                sqlite3_bind_int64(upsert, parameter, fields);
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

} // namespace ffindexstore
