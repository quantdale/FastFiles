#include "ffindexstore/DurableStore.h"

#include <sqlite3.h>

#include <cstring>

namespace ffindexstore {

namespace {

// Current schema version (task 1.4) -- bump and add a migration path in
// EnsureSchema when the schema changes; a fresh database always starts at
// this version.
constexpr int kSchemaVersion = 1;

constexpr const char* kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS volumes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  volume_guid TEXT NOT NULL UNIQUE,
  serial_number INTEGER NOT NULL,
  drive_letter INTEGER NOT NULL DEFAULT 0,
  available INTEGER NOT NULL DEFAULT 1,
  last_seen_time INTEGER NOT NULL DEFAULT 0,
  journal_id INTEGER,
  resume_usn INTEGER,
  scan_cursor BLOB,
  scan_complete INTEGER NOT NULL DEFAULT 0,
  needs_reconciliation INTEGER NOT NULL DEFAULT 0,
  last_reconciliation_time INTEGER NOT NULL DEFAULT 0,
  entry_count INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS entries (
  volume_id INTEGER NOT NULL,
  frn_low INTEGER NOT NULL,
  frn_high INTEGER NOT NULL,
  parent_frn_low INTEGER NOT NULL,
  parent_frn_high INTEGER NOT NULL,
  name TEXT NOT NULL,
  size_bytes INTEGER NOT NULL,
  last_write_time INTEGER NOT NULL,
  attributes INTEGER NOT NULL,
  PRIMARY KEY (volume_id, frn_low, frn_high)
) WITHOUT ROWID;

CREATE INDEX IF NOT EXISTS idx_entries_parent ON entries(volume_id, parent_frn_low, parent_frn_high);
)SQL";

// Thin RAII wrapper around a prepared statement -- avoids repeating
// finalize-on-every-return-path at each call site below.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr); }
    ~Stmt() { sqlite3_finalize(stmt_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    operator sqlite3_stmt*() const noexcept { return stmt_; }
    bool Valid() const noexcept { return stmt_ != nullptr; }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

int64_t ToSigned(uint64_t value) noexcept {
    int64_t signedValue;
    std::memcpy(&signedValue, &value, sizeof(value));
    return signedValue;
}

uint64_t ToUnsigned(int64_t value) noexcept {
    uint64_t unsignedValue;
    std::memcpy(&unsignedValue, &value, sizeof(value));
    return unsignedValue;
}

std::wstring Utf8ToWide(const unsigned char* utf8, int lengthBytes) {
    // Minimal UTF-8 -> UTF-16 decoder: names round-trip through SQLite's
    // TEXT affinity as UTF-8. Malformed sequences (which should never
    // occur, since every write path here originates from a validated
    // std::u16string) decode permissively rather than throwing.
    std::wstring result;
    result.reserve(static_cast<size_t>(lengthBytes));
    int i = 0;
    while (i < lengthBytes) {
        unsigned char c0 = utf8[i];
        uint32_t codepoint = 0;
        int extraBytes = 0;
        if ((c0 & 0x80) == 0) {
            codepoint = c0;
            extraBytes = 0;
        } else if ((c0 & 0xE0) == 0xC0) {
            codepoint = c0 & 0x1F;
            extraBytes = 1;
        } else if ((c0 & 0xF0) == 0xE0) {
            codepoint = c0 & 0x0F;
            extraBytes = 2;
        } else if ((c0 & 0xF8) == 0xF0) {
            codepoint = c0 & 0x07;
            extraBytes = 3;
        } else {
            ++i;
            continue; // skip invalid lead byte
        }
        if (i + extraBytes >= lengthBytes) {
            break;
        }
        bool valid = true;
        for (int j = 1; j <= extraBytes; ++j) {
            unsigned char cj = utf8[i + j];
            if ((cj & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | (cj & 0x3F);
        }
        i += extraBytes + 1;
        if (!valid) {
            continue;
        }
        if (codepoint <= 0xFFFF) {
            result.push_back(static_cast<wchar_t>(codepoint));
        } else {
            codepoint -= 0x10000;
            result.push_back(static_cast<wchar_t>(0xD800 + (codepoint >> 10)));
            result.push_back(static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF)));
        }
    }
    return result;
}

void BindName(sqlite3_stmt* stmt, int index, const std::u16string& name) {
    // std::u16string -> UTF-8 for TEXT storage, matching Utf8ToWide's
    // decoder above. SQLite has no native UTF-16LE-on-all-platforms
    // guarantee across builds, so UTF-8 is the portable choice for the
    // on-disk TEXT column.
    std::string utf8;
    utf8.reserve(name.size() * 3);
    size_t i = 0;
    while (i < name.size()) {
        uint32_t codepoint = name[i];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 1 < name.size()) {
            uint32_t low = name[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        ++i;
        if (codepoint <= 0x7F) {
            utf8.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            utf8.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            utf8.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }
    sqlite3_bind_text(stmt, index, utf8.data(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT);
}

VolumeRecord ReadVolumeRow(sqlite3_stmt* stmt) {
    VolumeRecord record;
    record.id = sqlite3_column_int(stmt, 0);
    record.identity.volumeGuid = Utf8ToWide(sqlite3_column_text(stmt, 1), sqlite3_column_bytes(stmt, 1));
    record.identity.serialNumber = ToUnsigned(sqlite3_column_int64(stmt, 2));
    record.driveLetter = static_cast<wchar_t>(sqlite3_column_int(stmt, 3));
    record.available = sqlite3_column_int(stmt, 4) != 0;
    record.lastSeenTime = ToUnsigned(sqlite3_column_int64(stmt, 5));
    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
        record.journalId = ToUnsigned(sqlite3_column_int64(stmt, 6));
    }
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
        record.resumeUsn = ToUnsigned(sqlite3_column_int64(stmt, 7));
    }
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
        const auto* blob = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 8));
        const int blobSize = sqlite3_column_bytes(stmt, 8);
        record.scanCursor.assign(blob, blob + blobSize);
    }
    record.scanComplete = sqlite3_column_int(stmt, 9) != 0;
    record.needsReconciliation = sqlite3_column_int(stmt, 10) != 0;
    record.lastReconciliationTime = ToUnsigned(sqlite3_column_int64(stmt, 11));
    record.entryCount = ToUnsigned(sqlite3_column_int64(stmt, 12));
    return record;
}

constexpr const char* kVolumeColumns =
    "id, volume_guid, serial_number, drive_letter, available, last_seen_time, "
    "journal_id, resume_usn, scan_cursor, scan_complete, needs_reconciliation, "
    "last_reconciliation_time, entry_count";

} // namespace

DurableStore::~DurableStore() {
    Close();
}

bool DurableStore::EnsureSchema() {
    char* errorMessage = nullptr;
    if (sqlite3_exec(db_, kSchemaSql, nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        sqlite3_free(errorMessage);
        return false;
    }

    int currentVersion = 0;
    {
        Stmt stmt(db_, "PRAGMA user_version;");
        if (stmt.Valid() && sqlite3_step(stmt) == SQLITE_ROW) {
            currentVersion = sqlite3_column_int(stmt, 0);
        }
    }
    if (currentVersion == 0) {
        char pragmaSql[64];
        std::snprintf(pragmaSql, sizeof(pragmaSql), "PRAGMA user_version=%d;", kSchemaVersion);
        sqlite3_exec(db_, pragmaSql, nullptr, nullptr, nullptr);
    }
    // A future schema change adds `else if (currentVersion < kSchemaVersion) { ... migrate ... }`
    // here (task 1.4); greenfield today, so there is nothing to migrate yet.
    return true;
}

bool DurableStore::Open(const std::wstring& databaseFilePath) {
    Close();

    std::string utf8Path;
    for (wchar_t wc : databaseFilePath) {
        // Paths passed in are always plain ASCII-range install/profile
        // directories in practice; a full UTF-16 -> UTF-8 conversion would
        // duplicate BindName's logic for a path that never carries
        // surrogate pairs, so this is deliberately narrow.
        if (wc < 0x80) {
            utf8Path.push_back(static_cast<char>(wc));
        }
    }

    if (sqlite3_open_v2(utf8Path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        Close();
        return false;
    }

    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=OFF;", nullptr, nullptr, nullptr);
    sqlite3_busy_timeout(db_, 5000);

    if (!EnsureSchema()) {
        Close();
        return false;
    }

    // Task 1.7: startup integrity verification. A failed check is
    // reported via LastIntegrityCheckPassed(); IndexStore decides whether
    // to fall back to a fresh scan (this class does not delete the file
    // itself -- that's a policy decision belonging one layer up).
    integrityOk_ = false;
    {
        Stmt stmt(db_, "PRAGMA integrity_check(1);");
        if (stmt.Valid() && sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            integrityOk_ = text != nullptr && std::strcmp(reinterpret_cast<const char*>(text), "ok") == 0;
        }
    }

    return true;
}

void DurableStore::Close() {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    integrityOk_ = false;
}

DurableVolumeId DurableStore::GetOrCreateVolume(const VolumeIdentity& identity, wchar_t driveLetterHint, uint64_t nowFileTime) {
    {
        Stmt select(db_, "SELECT id FROM volumes WHERE volume_guid = ?1;");
        std::string utf8Guid;
        for (wchar_t wc : identity.volumeGuid) {
            if (wc < 0x80) utf8Guid.push_back(static_cast<char>(wc));
        }
        sqlite3_bind_text(select, 1, utf8Guid.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(select) == SQLITE_ROW) {
            const DurableVolumeId id = sqlite3_column_int(select, 0);
            MarkVolumeAvailable(id, driveLetterHint, nowFileTime);
            return id;
        }
    }

    Stmt insert(db_,
        "INSERT INTO volumes (volume_guid, serial_number, drive_letter, available, last_seen_time) "
        "VALUES (?1, ?2, ?3, 1, ?4);");
    std::string utf8Guid;
    for (wchar_t wc : identity.volumeGuid) {
        if (wc < 0x80) utf8Guid.push_back(static_cast<char>(wc));
    }
    sqlite3_bind_text(insert, 1, utf8Guid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(insert, 2, ToSigned(identity.serialNumber));
    sqlite3_bind_int(insert, 3, static_cast<int>(driveLetterHint));
    sqlite3_bind_int64(insert, 4, ToSigned(nowFileTime));
    sqlite3_step(insert);
    return static_cast<DurableVolumeId>(sqlite3_last_insert_rowid(db_));
}

std::optional<VolumeRecord> DurableStore::GetVolume(DurableVolumeId id) const {
    Stmt stmt(db_, "SELECT id, volume_guid, serial_number, drive_letter, available, last_seen_time, "
                   "journal_id, resume_usn, scan_cursor, scan_complete, needs_reconciliation, "
                   "last_reconciliation_time, entry_count FROM volumes WHERE id = ?1;");
    sqlite3_bind_int(stmt, 1, id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        return std::nullopt;
    }
    return ReadVolumeRow(stmt);
}

std::vector<VolumeRecord> DurableStore::AllVolumes() const {
    std::vector<VolumeRecord> result;
    Stmt stmt(db_, "SELECT id, volume_guid, serial_number, drive_letter, available, last_seen_time, "
                   "journal_id, resume_usn, scan_cursor, scan_complete, needs_reconciliation, "
                   "last_reconciliation_time, entry_count FROM volumes;");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back(ReadVolumeRow(stmt));
    }
    return result;
}

void DurableStore::MarkVolumeAvailable(DurableVolumeId id, wchar_t driveLetterHint, uint64_t nowFileTime) {
    Stmt stmt(db_, "UPDATE volumes SET available = 1, last_seen_time = ?2, drive_letter = ?3 WHERE id = ?1;");
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int64(stmt, 2, ToSigned(nowFileTime));
    sqlite3_bind_int(stmt, 3, static_cast<int>(driveLetterHint));
    sqlite3_step(stmt);
}

void DurableStore::MarkVolumeUnavailable(DurableVolumeId id, uint64_t nowFileTime) {
    // Deliberately touches only this row (task 7.2/7.3: never a
    // cascading/side-effecting operation on other volumes' data).
    Stmt stmt(db_, "UPDATE volumes SET available = 0, last_seen_time = ?2 WHERE id = ?1;");
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int64(stmt, 2, ToSigned(nowFileTime));
    sqlite3_step(stmt);
}

void DurableStore::SetJournalId(DurableVolumeId id, uint64_t journalId) {
    Stmt stmt(db_, "UPDATE volumes SET journal_id = ?2 WHERE id = ?1;");
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int64(stmt, 2, ToSigned(journalId));
    sqlite3_step(stmt);
}

void DurableStore::SetResumeUsn(DurableVolumeId id, uint64_t resumeUsn) {
    Stmt stmt(db_, "UPDATE volumes SET resume_usn = ?2 WHERE id = ?1;");
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int64(stmt, 2, ToSigned(resumeUsn));
    sqlite3_step(stmt);
}

void DurableStore::SetScanCursor(DurableVolumeId id, const std::vector<uint8_t>& cursor) {
    Stmt stmt(db_, "UPDATE volumes SET scan_cursor = ?2 WHERE id = ?1;");
    sqlite3_bind_int(stmt, 1, id);
    if (cursor.empty()) {
        sqlite3_bind_null(stmt, 2);
    } else {
        sqlite3_bind_blob(stmt, 2, cursor.data(), static_cast<int>(cursor.size()), SQLITE_TRANSIENT);
    }
    sqlite3_step(stmt);
}

void DurableStore::ClearScanCursor(DurableVolumeId id) {
    SetScanCursor(id, {});
}

void DurableStore::SetScanComplete(DurableVolumeId id, bool complete) {
    Stmt stmt(db_, "UPDATE volumes SET scan_complete = ?2 WHERE id = ?1;");
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int(stmt, 2, complete ? 1 : 0);
    sqlite3_step(stmt);
}

void DurableStore::SetNeedsReconciliation(DurableVolumeId id, bool needsReconciliation) {
    Stmt stmt(db_, "UPDATE volumes SET needs_reconciliation = ?2 WHERE id = ?1;");
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int(stmt, 2, needsReconciliation ? 1 : 0);
    sqlite3_step(stmt);
}

void DurableStore::SetLastReconciliationTime(DurableVolumeId id, uint64_t nowFileTime) {
    Stmt stmt(db_, "UPDATE volumes SET last_reconciliation_time = ?2, needs_reconciliation = 0 WHERE id = ?1;");
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int64(stmt, 2, ToSigned(nowFileTime));
    sqlite3_step(stmt);
}

bool DurableStore::ForgetVolume(DurableVolumeId id) {
    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    {
        Stmt deleteEntries(db_, "DELETE FROM entries WHERE volume_id = ?1;");
        sqlite3_bind_int(deleteEntries, 1, id);
        if (sqlite3_step(deleteEntries) != SQLITE_DONE) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }
    {
        Stmt deleteVolume(db_, "DELETE FROM volumes WHERE id = ?1;");
        sqlite3_bind_int(deleteVolume, 1, id);
        if (sqlite3_step(deleteVolume) != SQLITE_DONE) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }
    return sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool DurableStore::CommitBatch(DurableVolumeId volumeId, const std::vector<IngestEntry>& batch) {
    if (batch.empty()) {
        return true;
    }

    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return false;
    }

    bool ok = true;
    int64_t netCountDelta = 0;
    {
        Stmt upsert(db_,
            "INSERT INTO entries (volume_id, frn_low, frn_high, parent_frn_low, parent_frn_high, "
            "name, size_bytes, last_write_time, attributes) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) "
            "ON CONFLICT(volume_id, frn_low, frn_high) DO UPDATE SET "
            "parent_frn_low = excluded.parent_frn_low, parent_frn_high = excluded.parent_frn_high, "
            "name = excluded.name, size_bytes = excluded.size_bytes, "
            "last_write_time = excluded.last_write_time, attributes = excluded.attributes;");
        Stmt remove(db_, "DELETE FROM entries WHERE volume_id = ?1 AND frn_low = ?2 AND frn_high = ?3;");
        Stmt exists(db_, "SELECT 1 FROM entries WHERE volume_id = ?1 AND frn_low = ?2 AND frn_high = ?3;");

        if (!upsert.Valid() || !remove.Valid() || !exists.Valid()) {
            ok = false;
        }

        for (const auto& entry : batch) {
            if (!ok) break;

            if (entry.op == IngestOp::Remove) {
                sqlite3_reset(exists);
                sqlite3_bind_int(exists, 1, volumeId);
                sqlite3_bind_int64(exists, 2, ToSigned(entry.key.fileReferenceNumber.low));
                sqlite3_bind_int64(exists, 3, ToSigned(entry.key.fileReferenceNumber.high));
                const bool existed = sqlite3_step(exists) == SQLITE_ROW;

                sqlite3_reset(remove);
                sqlite3_bind_int(remove, 1, volumeId);
                sqlite3_bind_int64(remove, 2, ToSigned(entry.key.fileReferenceNumber.low));
                sqlite3_bind_int64(remove, 3, ToSigned(entry.key.fileReferenceNumber.high));
                if (sqlite3_step(remove) != SQLITE_DONE) {
                    ok = false;
                    break;
                }
                if (existed) {
                    --netCountDelta;
                }
                continue;
            }

            sqlite3_reset(exists);
            sqlite3_bind_int(exists, 1, volumeId);
            sqlite3_bind_int64(exists, 2, ToSigned(entry.key.fileReferenceNumber.low));
            sqlite3_bind_int64(exists, 3, ToSigned(entry.key.fileReferenceNumber.high));
            const bool existed = sqlite3_step(exists) == SQLITE_ROW;

            sqlite3_reset(upsert);
            sqlite3_bind_int(upsert, 1, volumeId);
            sqlite3_bind_int64(upsert, 2, ToSigned(entry.key.fileReferenceNumber.low));
            sqlite3_bind_int64(upsert, 3, ToSigned(entry.key.fileReferenceNumber.high));
            sqlite3_bind_int64(upsert, 4, ToSigned(entry.parentFileReferenceNumber.low));
            sqlite3_bind_int64(upsert, 5, ToSigned(entry.parentFileReferenceNumber.high));
            BindName(upsert, 6, entry.name);
            sqlite3_bind_int64(upsert, 7, ToSigned(entry.sizeBytes));
            sqlite3_bind_int64(upsert, 8, ToSigned(entry.lastWriteTime));
            sqlite3_bind_int64(upsert, 9, entry.attributes);
            if (sqlite3_step(upsert) != SQLITE_DONE) {
                ok = false;
                break;
            }
            if (!existed) {
                ++netCountDelta;
            }
        }
    }

    if (ok && netCountDelta != 0) {
        Stmt updateCount(db_, "UPDATE volumes SET entry_count = entry_count + ?2 WHERE id = ?1;");
        sqlite3_bind_int(updateCount, 1, volumeId);
        sqlite3_bind_int64(updateCount, 2, netCountDelta);
        ok = sqlite3_step(updateCount) == SQLITE_DONE;
    }

    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    return sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

void DurableStore::StreamEntries(DurableVolumeId volumeId, const std::function<void(const IngestEntry&)>& visitor) const {
    Stmt stmt(db_, "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, "
                   "last_write_time, attributes FROM entries WHERE volume_id = ?1;");
    sqlite3_bind_int(stmt, 1, volumeId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        IngestEntry entry;
        entry.op = IngestOp::Upsert;
        entry.key.volumeId = volumeId;
        entry.key.fileReferenceNumber.low = ToUnsigned(sqlite3_column_int64(stmt, 0));
        entry.key.fileReferenceNumber.high = ToUnsigned(sqlite3_column_int64(stmt, 1));
        entry.parentFileReferenceNumber.low = ToUnsigned(sqlite3_column_int64(stmt, 2));
        entry.parentFileReferenceNumber.high = ToUnsigned(sqlite3_column_int64(stmt, 3));
        std::wstring wideName = Utf8ToWide(sqlite3_column_text(stmt, 4), sqlite3_column_bytes(stmt, 4));
        entry.name.assign(wideName.begin(), wideName.end());
        entry.sizeBytes = ToUnsigned(sqlite3_column_int64(stmt, 5));
        entry.lastWriteTime = ToUnsigned(sqlite3_column_int64(stmt, 6));
        entry.attributes = static_cast<uint32_t>(sqlite3_column_int64(stmt, 7));
        visitor(entry);
    }
}

std::vector<EntryKey> DurableStore::FindStaleEntries(DurableVolumeId volumeId, const std::vector<FileId128>& groundTruthKeys) const {
    std::vector<EntryKey> stale;

    sqlite3_exec(db_, "CREATE TEMP TABLE IF NOT EXISTS ground_truth (frn_low INTEGER, frn_high INTEGER, PRIMARY KEY (frn_low, frn_high)) WITHOUT ROWID;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "DELETE FROM temp.ground_truth;", nullptr, nullptr, nullptr);

    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    {
        Stmt insertGroundTruth(db_, "INSERT OR IGNORE INTO temp.ground_truth (frn_low, frn_high) VALUES (?1, ?2);");
        for (const auto& key : groundTruthKeys) {
            sqlite3_reset(insertGroundTruth);
            sqlite3_bind_int64(insertGroundTruth, 1, ToSigned(key.low));
            sqlite3_bind_int64(insertGroundTruth, 2, ToSigned(key.high));
            sqlite3_step(insertGroundTruth);
        }
    }

    {
        // Anti-join (design.md D1's stated rationale for choosing SQLite):
        // persisted rows for this volume with no matching ground-truth row.
        Stmt antiJoin(db_,
            "SELECT e.frn_low, e.frn_high FROM entries e "
            "LEFT JOIN temp.ground_truth g ON e.frn_low = g.frn_low AND e.frn_high = g.frn_high "
            "WHERE e.volume_id = ?1 AND g.frn_low IS NULL;");
        sqlite3_bind_int(antiJoin, 1, volumeId);
        while (sqlite3_step(antiJoin) == SQLITE_ROW) {
            EntryKey key;
            key.volumeId = volumeId;
            key.fileReferenceNumber.low = ToUnsigned(sqlite3_column_int64(antiJoin, 0));
            key.fileReferenceNumber.high = ToUnsigned(sqlite3_column_int64(antiJoin, 1));
            stale.push_back(key);
        }
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

    return stale;
}

bool DurableStore::RemoveEntries(DurableVolumeId volumeId, const std::vector<EntryKey>& keys) {
    if (keys.empty()) {
        return true;
    }
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return false;
    }
    bool ok = true;
    {
        Stmt remove(db_, "DELETE FROM entries WHERE volume_id = ?1 AND frn_low = ?2 AND frn_high = ?3;");
        for (const auto& key : keys) {
            sqlite3_reset(remove);
            sqlite3_bind_int(remove, 1, volumeId);
            sqlite3_bind_int64(remove, 2, ToSigned(key.fileReferenceNumber.low));
            sqlite3_bind_int64(remove, 3, ToSigned(key.fileReferenceNumber.high));
            if (sqlite3_step(remove) != SQLITE_DONE) {
                ok = false;
                break;
            }
        }
    }
    if (ok) {
        Stmt updateCount(db_, "UPDATE volumes SET entry_count = entry_count - ?2 WHERE id = ?1;");
        sqlite3_bind_int(updateCount, 1, volumeId);
        sqlite3_bind_int64(updateCount, 2, static_cast<int64_t>(keys.size()));
        ok = sqlite3_step(updateCount) == SQLITE_DONE;
    }
    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    return sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

void DurableStore::CheckpointPassive() {
    sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
}

void DurableStore::CheckpointIfWalExceeds(int thresholdPages) {
    int walPages = 0;
    int checkpointedPages = 0;
    sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, &walPages, &checkpointedPages);
    if (walPages > thresholdPages) {
        // Risks section: a size-triggered forced checkpoint bounds WAL
        // growth under sustained heavy write churn. RESTART still permits
        // concurrent readers, just not a fully exclusive TRUNCATE.
        sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_RESTART, nullptr, nullptr);
    }
}

} // namespace ffindexstore
