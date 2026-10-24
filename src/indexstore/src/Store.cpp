#include "ffindexstore/Store.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>
#include <vector>

#include "FoldClassification.h"
#include "sqlite3.h"

namespace ffindexstore {

namespace {

// task 1.4: PRAGMA user_version is the stored schema-version marker.
// Version 2 (allocated-size-storage-accuracy D4) adds the nullable
// allocated_size_bytes column (NULL = unknown, never zero) via an in-place
// ALTER TABLE for version-1 databases; a fresh database is created at
// version 2 directly. A mismatched newer version is refused rather than
// guessed at.
//
// durable-cold-search-index-and-hybrid-residency 3.1: version 3 adds the
// ADDITIVE durable cold candidate index: entry_name_terms (FTS5 trigram over
// the ASCII-folded name), entry_name_index_map (stable row identity:
// (volume_id, frn) -> fts rowid), entry_name_exceptions (sharp side-set:
// host fold-exceptional + surrogate names), and entry_name_index_meta
// (rebuild state markers). The candidate structures are DERIVED STATE: the
// entries table remains authoritative and any unproven index state falls
// back to the canonical stream (never silent false negatives). A v2
// application opening a v3 database is refused by the existing
// newer-version guard below (fail closed; no downgrade is attempted).
constexpr int kCurrentSchemaVersion = 3;

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

// durable-cold-search-index-and-hybrid-residency 3.1: additive v3 tables.
// CREATE IF NOT EXISTS makes this idempotent across interrupted migrations;
// content is (re)built by BuildCandidateIndexFromEntries before the
// user_version marker is bumped.
//   - entry_name_terms: FTS5 trigram index over the ASCII-folded name with
//     NO folding inside FTS (case_sensitive 1). Trigram substring semantics
//     match the product's infix literal search; SQLite's own case folding is
//     deliberately bypassed because it is neither the ASCII fold nor the
//     Windows ordinal fold.
//   - entry_name_index_map: (volume_id, frn_low, frn_high) -> term_rowid.
//     The fts5 table has only an implicit rowid, and entries is WITHOUT
//     ROWID, so this map is the stable join identity (lane-B P1: dense slot
//     indices may repack; FRN identity does not). Rowid-keyed fts
//     UPDATE/DELETE keeps maintenance O(log n) per row -- no per-row fts
//     scans (the predecessor's quadratic-erasure trap, avoided).
//   - entry_name_exceptions: sharp side-set of term_rowids whose names must
//     always be candidates (host fold-exceptional + surrogate units).
//   - entry_name_index_meta: candidate-index readiness marker (review lane
//     A-4: despite the table name it is a SINGLE global sentinel row,
//     volume_id=1: state=0 -> rebuild required before the index may be
//     consulted, state=1 -> ready; the row also pins the host fold
//     signature).
constexpr char kSchemaV3Sql[] =
    "CREATE TABLE IF NOT EXISTS entry_name_index_map ("
    "  volume_id INTEGER NOT NULL,"
    "  frn_low INTEGER NOT NULL,"
    "  frn_high INTEGER NOT NULL DEFAULT 0,"
    "  term_rowid INTEGER PRIMARY KEY,"
    "  UNIQUE(volume_id, frn_low, frn_high)"
    ");"
    "CREATE VIRTUAL TABLE IF NOT EXISTS entry_name_terms USING fts5("
    "  name_folded, tokenize=\"trigram case_sensitive 1\");"
    "CREATE TABLE IF NOT EXISTS entry_name_exceptions ("
    "  term_rowid INTEGER PRIMARY KEY"
    ");"
    "CREATE TABLE IF NOT EXISTS entry_name_index_meta ("
    "  volume_id INTEGER PRIMARY KEY,"
    "  state INTEGER NOT NULL DEFAULT 0,"
    "  fold_signature INTEGER NOT NULL DEFAULT 0"
    ");"

;

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
        "  FROM entries e INDEXED BY idx_entries_parent"
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

namespace {

// --- durable cold candidate index maintenance (v3) -----------------------

// 5.1: open-phase timing helper (startup profiling; see
// Store::OpenPhaseTimings).
inline double ElapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

// Testing seam state (see Store.h). Production never arms these.
std::atomic<int> gCandidateFaultRemaining{0};
std::atomic<bool> gCandidateFaultHardExit{false};
std::atomic<int> gCandidateCapOverride{0};
std::atomic<bool> gCandidateMaintenanceDisabled{false};
std::atomic<bool> gTestingGlobalAdmission{false};
std::atomic<bool> gTestingStagedDelivery{true};

// Cost-based cap (task 3.2; reviewer-C P1-class finding). The 3-scale matrix
// (evidence/artifacts/) shows the candidate route regressing broad needles
// at 2M (candidate fetch+join exceeds sequential-scan cost once the
// candidate set is large: unicode 0.6x, high_limit_broad 0.7x) while
// selective/exact/no-match stay 100-1500x. The cap is the hot path's
// kMaximumFilenameIndexCandidates value (16,384): a candidate workload that
// would EXCEED the cap takes the canonical scan (review lane B P3: admitted
// exactly at the cap, rejected above it -- the same `> cap` contract as
// Projection::FilenameCandidateIndices), which makes regression impossible
// -- the fallback IS the pre-candidate code path.
constexpr size_t kCandidateFallbackCap = 16384;

size_t CandidateFallbackCap() {
    const int override = gCandidateCapOverride.load(std::memory_order_acquire);
    return override > 0 ? static_cast<size_t>(override) : kCandidateFallbackCap;
}

bool CandidateFaultShouldFire() {
    int remaining = gCandidateFaultRemaining.load(std::memory_order_acquire);
    while (remaining > 0) {
        if (gCandidateFaultRemaining.compare_exchange_weak(remaining, remaining - 1,
                                                          std::memory_order_acq_rel)) {
            return remaining == 1;
        }
    }
    return false;
}

// True exactly at the armed operation (fault). At that point the seam either
// reports failure (graceful mode) or hard-kills the process (kill-during-
// commit mode).
bool CandidateFaultFire() {
    if (!CandidateFaultShouldFire()) return false;
    if (gCandidateFaultHardExit.load(std::memory_order_acquire)) {
        ExitProcess(0xDEAD);
    }
    return true;
}
//
// All helpers operate INSIDE the caller's transaction (ApplyBatch's
// BEGIN IMMEDIATE or the open-time rebuild transaction) so the candidate
// structures can never disagree with entries after a crash: either both
// changed or neither did. The map row is maintained for EVERY entry
// (including empty names) so the invariant
//     COUNT(entry_name_index_map) == COUNT(entries)
// is a cheap integrity cross-check at open time.

constexpr char kCandidateUpsertMapSql[] =
    "INSERT INTO entry_name_index_map(volume_id, frn_low, frn_high) VALUES(?,?,?) "
    "ON CONFLICT(volume_id, frn_low, frn_high) DO NOTHING;";
constexpr char kCandidateLookupRowidSql[] =
    "SELECT term_rowid FROM entry_name_index_map WHERE volume_id=? AND frn_low=? AND frn_high=?;";

// Cached prepared statements for the maintenance path. Preparing once per
// batch (not once per row) keeps the per-row cost at bind+step; a per-row
// prepare was measured at ~3x fixture-ingest cost at 200k rows.
struct CandidateStatements {
    Statement mapUpsert;
    Statement lookup;
    Statement termsUpdate;
    Statement termsInsert;
    Statement markException;
    Statement clearException;
    Statement clearTerm;
    Statement clearMap;

    explicit CandidateStatements(sqlite3* db,
                                 ffindexstore::Store::OpenPhaseTimings* phaseTimings = nullptr)
        : mapUpsert(db, kCandidateUpsertMapSql),
          lookup(db, kCandidateLookupRowidSql),
          termsUpdate(db, "UPDATE entry_name_terms SET name_folded=? WHERE rowid=?;"),
          termsInsert(db, "INSERT INTO entry_name_terms(rowid, name_folded) VALUES(?,?);"),
          markException(db, "INSERT OR IGNORE INTO entry_name_exceptions(term_rowid) VALUES(?);"),
          clearException(db, "DELETE FROM entry_name_exceptions WHERE term_rowid=?;"),
          clearTerm(db, "DELETE FROM entry_name_terms WHERE rowid=?;"),
          clearMap(db, "DELETE FROM entry_name_index_map WHERE term_rowid=?;"),
          timings(phaseTimings) {}

    // 5.2 (task 3.3): optional per-sub-phase accumulators (rebuild/
    // migration decomposition only; nullptr everywhere in production).
    ffindexstore::Store::OpenPhaseTimings* timings;

    bool Valid() const {
        return mapUpsert.Valid() && lookup.Valid() && termsUpdate.Valid() && termsInsert.Valid()
            && markException.Valid() && clearException.Valid() && clearTerm.Valid() && clearMap.Valid();
    }
};

int64_t CandidateTermRowid(CandidateStatements& stmts, int64_t volumeRowId, uint64_t frnLow, uint64_t frnHigh) {
    sqlite3_reset(stmts.lookup);
    sqlite3_bind_int64(stmts.lookup, 1, volumeRowId);
    sqlite3_bind_int64(stmts.lookup, 2, static_cast<sqlite3_int64>(frnLow));
    sqlite3_bind_int64(stmts.lookup, 3, static_cast<sqlite3_int64>(frnHigh));
    if (sqlite3_step(stmts.lookup) != SQLITE_ROW) return -1;
    return sqlite3_column_int64(stmts.lookup, 0);
}

// ASCII-folded UTF-8 copy of the name (FoldAscii semantics: only ASCII A-Z
// folds; UTF-8 multibyte sequences pass through byte-for-byte, which keeps
// the folded column's trigram windows identical to the UTF-16-unit windows
// of the folded name).
std::string FoldAsciiUtf8(const std::string& utf8) {
    std::string folded = utf8;
    for (char& ch : folded) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 0x20);
    }
    return folded;
}

// Upsert the candidate-index row for one entry. When updateName is false the
// map row is only ensured to exist (partial upserts without a name field
// leave the indexed name untouched, matching the entries upsert contract).
bool CandidateIndexUpsert(sqlite3* db, CandidateStatements& stmts, int64_t volumeRowId,
                          uint64_t frnLow, uint64_t frnHigh,
                          const std::string& nameUtf8, const std::u16string& nameUtf16, bool updateName) {
    if (gCandidateMaintenanceDisabled.load(std::memory_order_acquire)) return true;
    if (CandidateFaultFire()) return false;
    auto* const timings = stmts.timings;
    const auto mapStart = std::chrono::steady_clock::now();
    sqlite3_reset(stmts.mapUpsert);
    sqlite3_bind_int64(stmts.mapUpsert, 1, volumeRowId);
    sqlite3_bind_int64(stmts.mapUpsert, 2, static_cast<sqlite3_int64>(frnLow));
    sqlite3_bind_int64(stmts.mapUpsert, 3, static_cast<sqlite3_int64>(frnHigh));
    if (sqlite3_step(stmts.mapUpsert) != SQLITE_DONE) return false;
    const int64_t termRowid = CandidateTermRowid(stmts, volumeRowId, frnLow, frnHigh);
    if (termRowid < 0) return false;
    if (timings != nullptr) {
        timings->candidateMapMs += ElapsedMs(mapStart);
        ++timings->candidateRowsProcessed;
    }
    if (!updateName) return true;

    const auto termsStart = std::chrono::steady_clock::now();
    const std::string folded = FoldAsciiUtf8(nameUtf8);
    // fts5 virtual tables do not support the UPSERT conflict clause; the
    // rowid-keyed UPDATE-then-INSERT pair is the supported idiom and stays
    // O(log n) through the map identity.
    sqlite3_reset(stmts.termsUpdate);
    sqlite3_bind_text(stmts.termsUpdate, 1, folded.data(), static_cast<int>(folded.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmts.termsUpdate, 2, termRowid);
    if (sqlite3_step(stmts.termsUpdate) != SQLITE_DONE) return false;
    if (sqlite3_changes(db) == 0) {
        sqlite3_reset(stmts.termsInsert);
        sqlite3_bind_int64(stmts.termsInsert, 1, termRowid);
        sqlite3_bind_text(stmts.termsInsert, 2, folded.data(), static_cast<int>(folded.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(stmts.termsInsert) != SQLITE_DONE) return false;
    }
    if (timings != nullptr) timings->candidateTermsMs += ElapsedMs(termsStart);

    // Sharp side-set: host fold-exceptional + surrogate names are always
    // candidates (the P2-A superset contract). Classification fails closed.
    const auto exceptionStart = std::chrono::steady_clock::now();
    const bool exceptional = ffindexstore::foldclassification::NameRequiresCandidateSideSet(
        nameUtf16.c_str(), nameUtf16.size());
    sqlite3_reset(stmts.markException);
    sqlite3_bind_int64(stmts.markException, 1, termRowid);
    if (sqlite3_step(stmts.markException) != SQLITE_DONE) return false;
    if (!exceptional) {
        sqlite3_reset(stmts.clearException);
        sqlite3_bind_int64(stmts.clearException, 1, termRowid);
        if (sqlite3_step(stmts.clearException) != SQLITE_DONE) return false;
    }
    if (timings != nullptr) timings->candidateExceptionMs += ElapsedMs(exceptionStart);
    return true;
}

bool CandidateIndexRemove(CandidateStatements& stmts, int64_t volumeRowId, uint64_t frnLow, uint64_t frnHigh) {
    if (gCandidateMaintenanceDisabled.load(std::memory_order_acquire)) return true;
    if (CandidateFaultFire()) return false;
    const int64_t termRowid = CandidateTermRowid(stmts, volumeRowId, frnLow, frnHigh);
    if (termRowid < 0) return true;  // never indexed (or already removed)
    sqlite3_reset(stmts.clearException);
    sqlite3_bind_int64(stmts.clearException, 1, termRowid);
    if (sqlite3_step(stmts.clearException) != SQLITE_DONE) return false;
    sqlite3_reset(stmts.clearTerm);
    sqlite3_bind_int64(stmts.clearTerm, 1, termRowid);
    if (sqlite3_step(stmts.clearTerm) != SQLITE_DONE) return false;
    sqlite3_reset(stmts.clearMap);
    sqlite3_bind_int64(stmts.clearMap, 1, termRowid);
    return sqlite3_step(stmts.clearMap) == SQLITE_DONE;
}

// Whole-volume candidate cleanup (ForgetVolume). fts5 rows are removed
// rowid-by-rowid (the map is the identity anchor); the map/exception tables
// are cleaned set-based afterwards. Runs inside the caller's transaction.
bool CandidateIndexRemoveVolume(sqlite3* db, int64_t volumeRowId) {
    std::vector<int64_t> rowids;
    {
        Statement rows(db, "SELECT term_rowid FROM entry_name_index_map WHERE volume_id=?;");
        if (!rows.Valid()) return false;
        sqlite3_bind_int64(rows, 1, volumeRowId);
        while (sqlite3_step(rows) == SQLITE_ROW) rowids.push_back(sqlite3_column_int64(rows, 0));
    }
    for (const int64_t rowid : rowids) {
        Statement clearTerm(db, "DELETE FROM entry_name_terms WHERE rowid=?;");
        if (!clearTerm.Valid()) return false;
        sqlite3_bind_int64(clearTerm, 1, rowid);
        if (sqlite3_step(clearTerm) != SQLITE_DONE) return false;
    }
    Statement clearExceptions(db,
        "DELETE FROM entry_name_exceptions WHERE term_rowid IN "
        "(SELECT term_rowid FROM entry_name_index_map WHERE volume_id=?);");
    if (!clearExceptions.Valid()) return false;
    sqlite3_bind_int64(clearExceptions, 1, volumeRowId);
    if (sqlite3_step(clearExceptions) != SQLITE_DONE) return false;
    Statement clearMap(db, "DELETE FROM entry_name_index_map WHERE volume_id=?;");
    if (!clearMap.Valid()) return false;
    sqlite3_bind_int64(clearMap, 1, volumeRowId);
    return sqlite3_step(clearMap) == SQLITE_DONE;
}

// Full idempotent rebuild of the derived structures (migration + recovery).
// Per-volume transactions bound WAL growth; the meta marker is only flipped
// to ready after every volume is built, so an interrupted rebuild is always
// detected and redone at the next open.
bool RebuildCandidateIndex(sqlite3* db, Store::OpenPhaseTimings* timings = nullptr) {
    if (!ExecSql(db, "BEGIN IMMEDIATE;")) return false;
    if (!ExecSql(db, "DELETE FROM entry_name_exceptions;") ||
        !ExecSql(db, "DELETE FROM entry_name_terms;") ||
        !ExecSql(db, "DELETE FROM entry_name_index_map;") ||
        !ExecSql(db, "DELETE FROM entry_name_index_meta;")) {
        ExecSql(db, "ROLLBACK;");
        return false;
    }
    if (!ExecSql(db, "INSERT INTO entry_name_index_meta(volume_id, state) VALUES(1, 0) "
                     "ON CONFLICT(volume_id) DO UPDATE SET state=0;")) {
        ExecSql(db, "ROLLBACK;");
        return false;
    }
    if (!ExecSql(db, "COMMIT;")) {
        ExecSql(db, "ROLLBACK;");
        return false;
    }

    std::vector<int64_t> volumes;
    {
        Statement volumesStmt(db, "SELECT id FROM volumes;");
        if (!volumesStmt.Valid()) return false;
        int stepResult = SQLITE_ERROR;
        while ((stepResult = sqlite3_step(volumesStmt)) == SQLITE_ROW) {
            volumes.push_back(sqlite3_column_int64(volumesStmt, 0));
        }
        // durable-cold-search-index-and-hybrid-residency final review:
        // interruption is not exhaustion; never publish a partial rebuild.
        if (stepResult != SQLITE_DONE) return false;
    }
    for (const int64_t volumeRowId : volumes) {
        CandidateStatements candidateStmts(db, timings);
        if (!candidateStmts.Valid()) return false;
        if (!ExecSql(db, "BEGIN IMMEDIATE;")) return false;
        Statement rows(db,
            "SELECT frn_low, frn_high, name FROM entries WHERE volume_id=?;");
        bool ok = rows.Valid();
        if (ok) {
            sqlite3_bind_int64(rows, 1, volumeRowId);
            int stepResult = SQLITE_ERROR;
            while (ok && (stepResult = sqlite3_step(rows)) == SQLITE_ROW) {
                const uint64_t frnLow = static_cast<uint64_t>(sqlite3_column_int64(rows, 0));
                const uint64_t frnHigh = static_cast<uint64_t>(sqlite3_column_int64(rows, 1));
                const char* name = reinterpret_cast<const char*>(sqlite3_column_text(rows, 2));
                const int nameBytes = sqlite3_column_bytes(rows, 2);
                const std::string nameUtf8(name ? name : "", name ? static_cast<size_t>(nameBytes) : 0);
                ok = CandidateIndexUpsert(db, candidateStmts, volumeRowId, frnLow, frnHigh, nameUtf8,
                                          Utf8ToUtf16(nameUtf8.c_str(), static_cast<int>(nameUtf8.size())), true);
            }
            if (ok && stepResult != SQLITE_DONE) ok = false;
        }
        if (!ok) {
            ExecSql(db, "ROLLBACK;");
            return false;
        }
        if (!ExecSql(db, "COMMIT;")) {
            ExecSql(db, "ROLLBACK;");
            return false;
        }
    }

    if (!ExecSql(db, "BEGIN IMMEDIATE;")) return false;
    Statement countStmt(db, "SELECT (SELECT COUNT(*) FROM entry_name_index_map) = "
                            "(SELECT COUNT(*) FROM entries);");
    bool finalOk = countStmt.Valid() && sqlite3_step(countStmt) == SQLITE_ROW &&
                   sqlite3_column_int(countStmt, 0) == 1;
    if (finalOk) {
        // The ready marker also pins the HOST fold signature: the persisted
        // side-set classification is only valid for the host table it was
        // built against (lane-B P1).
        const auto foldStart = std::chrono::steady_clock::now();
        const uint64_t hostFoldSignature =
            ffindexstore::foldclassification::HostFoldSignature();
        if (timings != nullptr) {
            timings->hostFoldSignatureMs += ElapsedMs(foldStart);
            timings->hostFoldSignature = hostFoldSignature;
        }
        Statement mark(db, "UPDATE entry_name_index_meta SET state=1, fold_signature=? WHERE volume_id=1;");
        finalOk = mark.Valid();
        if (finalOk) {
            sqlite3_bind_int64(mark, 1, static_cast<sqlite3_int64>(
                static_cast<int64_t>(hostFoldSignature)));
            finalOk = sqlite3_step(mark) == SQLITE_DONE;
        }
    }
    if (!finalOk || !ExecSql(db, "COMMIT;")) {
        ExecSql(db, "ROLLBACK;");
        return false;
    }
    return true;
}

} // namespace
bool Store::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed,
                 OpenPhaseTimings* outTimings) {
    if (outIntegrityFailed != nullptr) {
        *outIntegrityFailed = false;
    }
    if (outTimings != nullptr) {
        *outTimings = OpenPhaseTimings{};
    }
    Close();

    // 5.1: totalMs is recorded on every return path via RAII.
    const auto openStart = std::chrono::steady_clock::now();
    struct TotalGuard {
        OpenPhaseTimings* timings;
        std::chrono::steady_clock::time_point start;
        ~TotalGuard() {
            if (timings != nullptr) {
                timings->totalMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
            }
        }
    } totalGuard{outTimings, openStart};

    const auto connectionStart = std::chrono::steady_clock::now();
    const int connectionResult =
        sqlite3_open_v2(dbPathUtf8.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (outTimings != nullptr) {
        outTimings->connectionOpenMs = ElapsedMs(connectionStart);
    }
    if (connectionResult != SQLITE_OK) {
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
    const auto integrityStart = std::chrono::steady_clock::now();
    const IntegrityVerdict verdict = RunIntegrityCheckDetailed();
    if (outTimings != nullptr) {
        outTimings->integrityCheckMs = ElapsedMs(integrityStart);
    }
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
    // (review lane A-2: these PRAGMAs run exactly once, timed below.)
    const auto pragmaStart = std::chrono::steady_clock::now();
    const bool pragmaOk = ExecSql(db_, "PRAGMA journal_mode=WAL;")
        && ExecSql(db_, "PRAGMA synchronous=NORMAL;")
        && ExecSql(db_, "PRAGMA foreign_keys=ON;");
    if (outTimings != nullptr) {
        outTimings->pragmaConfigMs = ElapsedMs(pragmaStart);
    }
    if (!pragmaOk) {
        Close();
        return false;
    }

    const auto schemaStart = std::chrono::steady_clock::now();
    // durable-cold-search-index-and-hybrid-residency final review: record
    // missing derived structures BEFORE CREATE IF NOT EXISTS can conceal
    // them. An intact ready marker/map cannot certify a recreated empty FTS
    // table or exception set. Include FTS5's required shadow tables.
    bool candidateStructuresExisted = false;
    {
        Statement structures(db_, "SELECT COUNT(*) FROM sqlite_schema WHERE type='table' AND name IN ("
            "'entry_name_index_map','entry_name_terms','entry_name_exceptions','entry_name_index_meta',"
            "'entry_name_terms_data','entry_name_terms_idx','entry_name_terms_content',"
            "'entry_name_terms_docsize','entry_name_terms_config');");
        if (!structures.Valid() || sqlite3_step(structures) != SQLITE_ROW) {
            Close();
            return false;
        }
        candidateStructuresExisted = sqlite3_column_int(structures, 0) == 9;
    }
    // durable-cold-search-index-and-hybrid-residency 3.1: additive v3
    // structures (idempotent; content built below before any version bump).
    // If repair is needed, schema recreation and invalidating the old ready
    // marker must commit together. Otherwise a failed clearing/rebuild can
    // leave recreated empty terms alongside an old ready marker on retry.
    if (!candidateStructuresExisted && !ExecSql(db_, "BEGIN IMMEDIATE;")) {
        Close();
        return false;
    }
    bool schemaOk = ExecSql(db_, kSchemaSql) && ExecSql(db_, kSchemaV3Sql);
    if (!candidateStructuresExisted) {
        schemaOk = schemaOk && ExecSql(db_, "UPDATE entry_name_index_meta SET state=0;") &&
                   ExecSql(db_, "COMMIT;");
        if (!schemaOk) ExecSql(db_, "ROLLBACK;");
    }
    if (outTimings != nullptr) {
        outTimings->schemaInitMs = ElapsedMs(schemaStart);
    }
    if (!schemaOk) {
        Close();
        return false;
    }

    const auto versionStart = std::chrono::steady_clock::now();
    Statement versionStmt(db_, "PRAGMA user_version;");
    // An unsuccessful version read is not evidence of a fresh database.
    // In particular, never rebuild/downgrade an unknown newer schema after
    // a transient SQLite prepare/step error.
    if (!versionStmt.Valid() || sqlite3_step(versionStmt) != SQLITE_ROW) {
        Close();
        return false;
    }
    const int existingVersion = sqlite3_column_int(versionStmt, 0);
    sqlite3_reset(versionStmt);
    if (outTimings != nullptr) {
        outTimings->schemaVersionReadMs = ElapsedMs(versionStart);
        outTimings->priorSchemaVersion = existingVersion;
    }
    // 5.1: one timed rebuild wrapper for every rebuild/migration site.
    const auto timedRebuild = [&]() -> bool {
        const auto rebuildStart = std::chrono::steady_clock::now();
        const bool rebuilt = RebuildCandidateIndex(db_, outTimings);
        if (outTimings != nullptr) {
            outTimings->candidateRebuildMs += ElapsedMs(rebuildStart);
            if (rebuilt) outTimings->candidateIndexRebuilt = true;
        }
        return rebuilt;
    };
    bool candidateIndexReady = false;
    if (existingVersion == 0) {
        // Fresh database (or pre-versioning database with no v2 data to
        // migrate). The candidate index is built from whatever entries
        // exist before the version marker is bumped, so an interrupted
        // migration is redone at the next open.
        if (!timedRebuild()) {
            Close();
            return false;
        }
        char pragma[64];
        std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
        if (!ExecSql(db_, pragma)) {
            Close();
            return false;
        }
        candidateIndexReady = true;
    } else if (existingVersion == 1) {
        // allocated-size-storage-accuracy D4: in-place migration of an
        // existing version-1 database. Runs before the version marker is
        // bumped; if the ALTER fails the database is left untouched and the
        // open fails closed (the existing integrity-check/rebuild fallback
        // in Main.cpp then applies).
        //
        // durable-cold-search reviewer-B P1: the ALTER must be IDEMPOTENT.
        // A crash or failure between the ALTER and the version bump leaves
        // user_version=1 with the column already present; re-running the raw
        // ALTER then fails with a duplicate-column error and would brick
        // every subsequent open. Guard on column existence instead.
        {
            bool allocatedColumnExists = false;
            Statement columnStmt(db_, "PRAGMA table_info(entries);");
            if (columnStmt.Valid()) {
                while (sqlite3_step(columnStmt) == SQLITE_ROW) {
                    const char* columnName = reinterpret_cast<const char*>(sqlite3_column_text(columnStmt, 1));
                    if (columnName != nullptr && std::strcmp(columnName, "allocated_size_bytes") == 0) {
                        allocatedColumnExists = true;
                        break;
                    }
                }
            }
            if (!allocatedColumnExists && !ExecSql(db_, kMigrateV1ToV2Sql)) {
                Close();
                return false;
            }
        }
        if (!timedRebuild()) {
            Close();
            return false;
        }
        char pragma[64];
        std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
        if (!ExecSql(db_, pragma)) {
            Close();
            return false;
        }
        candidateIndexReady = true;
    } else if (existingVersion == 2) {
        // v2 -> v3: pure additive derived state. The entries table is never
        // rewritten; the candidate index is built and only then is the
        // version marker bumped (lane-E crash matrix C2a-C2d).
        if (!timedRebuild()) {
            Close();
            return false;
        }
        char pragma[64];
        std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
        if (!ExecSql(db_, pragma)) {
            Close();
            return false;
        }
        candidateIndexReady = true;
    } else if (existingVersion != kCurrentSchemaVersion) {
        // A newer-than-understood schema is refused rather than guessed at.
        // A v2-era application opening a v3 database lands here and fails
        // closed (no downgrade, no data loss).
        Close();
        return false;
    }

    if (!candidateIndexReady) {
        // Version-3 open: the index is maintained transactionally with
        // entries, so state is only suspect after an interrupted rebuild.
        // The meta marker plus the mapped-rows cross-check detect it.
        const auto consistencyStart = std::chrono::steady_clock::now();
        double foldSignatureMs = 0.0;
        double signatureCompareMs = 0.0;
        Statement metaStmt(db_,
            "SELECT state, fold_signature, (SELECT COUNT(*) FROM entry_name_index_map), "
            "(SELECT COUNT(*) FROM entries) "
            "FROM entry_name_index_meta WHERE volume_id=1;");
        bool rebuildNeeded = true;
        if (metaStmt.Valid() && sqlite3_step(metaStmt) == SQLITE_ROW) {
            const int state = sqlite3_column_int(metaStmt, 0);
            const sqlite3_int64 foldSignature = sqlite3_column_int64(metaStmt, 1);
            const sqlite3_int64 mapped = sqlite3_column_int64(metaStmt, 2);
            const sqlite3_int64 entries = sqlite3_column_int64(metaStmt, 3);
            // The signature check is what makes the host-bound side-set
            // safe across machines: a database written where a unit was
            // ordinary cannot serve candidates on a host that folds it.
            const auto foldStart = std::chrono::steady_clock::now();
            const uint64_t hostFoldSignature =
                ffindexstore::foldclassification::HostFoldSignature();
            foldSignatureMs = ElapsedMs(foldStart);
            const auto compareStart = std::chrono::steady_clock::now();
            const sqlite3_int64 hostSignature =
                static_cast<sqlite3_int64>(static_cast<int64_t>(hostFoldSignature));
            rebuildNeeded = !candidateStructuresExisted || (state != 1) ||
                            (mapped != entries) || (foldSignature != hostSignature);
            signatureCompareMs = ElapsedMs(compareStart);
            if (outTimings != nullptr) {
                outTimings->hostFoldSignature = hostFoldSignature;
            }
        }
        if (outTimings != nullptr) {
            outTimings->hostFoldSignatureMs += foldSignatureMs;
            outTimings->signatureCompareMs += signatureCompareMs;
            outTimings->consistencyCheckMs +=
                ElapsedMs(consistencyStart) - foldSignatureMs - signatureCompareMs;
        }
        sqlite3_reset(metaStmt);
        if (rebuildNeeded && !timedRebuild()) {
            Close();
            return false;
        }
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
    checkpointDiagnostics_ = {};
}

std::unique_ptr<StoreReadSession> Store::OpenReadSession(
    const StoreReadSessionOptions& options) const {
    if (db_ == nullptr || dbPathUtf8_.empty()) return nullptr;
    std::unique_ptr<StoreReadSession> session(new StoreReadSession(dbPathUtf8_, options));
    if (!session->Open()) return nullptr;
    return session;
}

bool Store::RunCheckpoint(const int mode) {
    auto& diagnostics = checkpointDiagnostics_;
    ++diagnostics.sqliteCalls;
    diagnostics.logFrames = -1;
    diagnostics.checkpointedFrames = -1;
    diagnostics.restartAttempted = mode == SQLITE_CHECKPOINT_RESTART;
    diagnostics.lastResultCode = db_ == nullptr ? SQLITE_MISUSE : sqlite3_wal_checkpoint_v2(
        db_, nullptr, mode, &diagnostics.logFrames, &diagnostics.checkpointedFrames);
    const int primary = diagnostics.lastResultCode & 0xFF;
    if (primary == SQLITE_BUSY || primary == SQLITE_LOCKED) ++diagnostics.busyResults;
    else if (primary != SQLITE_OK) ++diagnostics.errorResults;
    return diagnostics.lastResultCode == SQLITE_OK;
}

bool Store::CheckpointPassive() {
    return RunCheckpoint(SQLITE_CHECKPOINT_PASSIVE);
}

bool Store::CheckpointIfWalExceeds(uint64_t thresholdBytes) {
    if (db_ == nullptr) {
        return false;
    }
    if (!RunCheckpoint(SQLITE_CHECKPOINT_PASSIVE)) return false;
    const int frameCount = checkpointDiagnostics_.logFrames;
    if (frameCount <= 0) return true;
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
        return RunCheckpoint(SQLITE_CHECKPOINT_RESTART);
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
        // durable-cold-search-index-and-hybrid-residency 3.1: candidate
        // structures are derived state and must vanish with the entries
        // (same transaction).
        ok = CandidateIndexRemoveVolume(db_, volumeRowId);
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

void SetTestingCandidateMaintenanceFault(int nthOperation, bool hardProcessExit) {
    gCandidateFaultRemaining.store(nthOperation > 0 ? nthOperation : 0, std::memory_order_release);
    gCandidateFaultHardExit.store(hardProcessExit, std::memory_order_release);
}

void SetTestingCandidateCapOverride(size_t cap) {
    // Review lane B P3: clamp instead of silently narrowing size_t -> int
    // (an out-of-range override must never quietly restore the production
    // cap).
    const size_t bounded = (cap > static_cast<size_t>(INT_MAX)) ? static_cast<size_t>(INT_MAX) : cap;
    gCandidateCapOverride.store(bounded > 0 ? static_cast<int>(bounded) : 0, std::memory_order_release);
}

void SetTestingCandidateMaintenanceDisabled(bool disabled) {
    gCandidateMaintenanceDisabled.store(disabled, std::memory_order_release);
}

void SetTestingCandidateGlobalAdmission(bool globalOnly) {
    gTestingGlobalAdmission.store(globalOnly, std::memory_order_release);
}

void SetTestingCandidateStagedDelivery(bool staged) {
    gTestingStagedDelivery.store(staged, std::memory_order_release);
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
    CandidateStatements candidateStmts(db_);
    if (!candidateStmts.Valid()) {
        ExecSql(db_, "ROLLBACK;");
        return false;
    }
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
            // durable-cold-search-index-and-hybrid-residency 3.1: maintain
            // the candidate index in the SAME transaction (crash-safe by
            // construction). name-less partial upserts only ensure the map
            // row (the indexed name stays untouched, matching the entries
            // CASE contract).
            if (ok) {
                ok = CandidateIndexUpsert(db_, candidateStmts, volumeRowId, record.id.low, record.id.high,
                                          nameUtf8, record.name, hasName);
            }
        } else {
            sqlite3_reset(remove);
            sqlite3_bind_int64(remove, 1, static_cast<sqlite3_int64>(volumeRowId));
            sqlite3_bind_int64(remove, 2, static_cast<sqlite3_int64>(change.record.id.low));
            sqlite3_bind_int64(remove, 3, static_cast<sqlite3_int64>(change.record.id.high));
            ok = sqlite3_step(remove) == SQLITE_DONE;
            if (ok) {
                ok = CandidateIndexRemove(candidateStmts, volumeRowId, change.record.id.low, change.record.id.high);
            }
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

std::vector<FileId> Store::ListEntryIds(VolumeRowId volumeRowId, bool* outSuccess) {
    if (outSuccess != nullptr) *outSuccess = false;
    std::vector<FileId> result;
    if (db_ == nullptr) {
        return result;
    }
    Statement stmt(db_, "SELECT frn_low, frn_high FROM entries WHERE volume_id=?;");
    if (!stmt.Valid()) {
        return result;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    int stepResult = SQLITE_ERROR;
    while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW) {
        result.push_back(ReadFileId(stmt, 0, 1));
    }
    if (outSuccess != nullptr) *outSuccess = stepResult == SQLITE_DONE;
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
        "  FROM entries e INDEXED BY idx_entries_parent"
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
    // Readiness belongs to one SQLite snapshot. Close/move assignment must
    // not reuse the previous connection's cached ready verdict.
    candidateReadinessChecked_ = false;
    candidateIndexUsable_ = false;
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

bool StoreReadSession::CandidateIndexUsable() {
    if (candidateReadinessChecked_) return candidateIndexUsable_;
    candidateReadinessChecked_ = true;
    candidateIndexUsable_ = false;
    if (!IsOpen()) return false;
    // 5.6 (review lane B): the derived structures are trustworthy in THIS
    // session's snapshot only when the ready marker is set AND the pinned
    // host fold signature matches this host. The rebuild path clears the
    // marker in its first transaction and re-flips it only after every
    // volume is rebuilt, so a snapshot taken mid-rebuild sees state=0 and
    // every query correctly degrades to the canonical stream instead of
    // consulting a partial index (silent-truncation class). The same
    // one-row check catches a database built on a host with different
    // ordinal folding. Any failure fails closed (no candidate route).
    Statement metaStmt(db_,
        "SELECT state, fold_signature FROM entry_name_index_meta WHERE volume_id=1;");
    if (!metaStmt.Valid()) return false;
    const int step = sqlite3_step(metaStmt);
    if (step != SQLITE_ROW) return false;
    const int state = sqlite3_column_int(metaStmt, 0);
    const sqlite3_int64 foldSignature = sqlite3_column_int64(metaStmt, 1);
    const sqlite3_int64 hostSignature = static_cast<sqlite3_int64>(static_cast<int64_t>(
        ffindexstore::foldclassification::HostFoldSignature()));
    candidateIndexUsable_ = (state == 1) && (foldSignature == hostSignature);
    return candidateIndexUsable_;
}

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

    // durable-cold-search-index-and-hybrid-residency 3.2: candidate rung.
    // For an eligible literal (ASCII, folded length >= 3) the durable FTS5
    // candidate index produces the row set
    //     { names containing the folded needle as a substring }
    //     UNION { host fold-exceptional / surrogate names }
    // which is a conservative superset of the canonical matches (the probe
    // evidence in evidence/architecture-decision-record.md), so visiting
    // exactly those rows is contract-equivalent to the full stream -- the
    // caller re-verifies each row with CompareStringOrdinal either way.
    // Any failure or ineligible shape falls through to the canonical stream
    // below (correctness never depends on the index being usable).
    // reviewer-A P2: fold defensively here. The API contract is an
    // ASCII-FOLDED needle, but an unfolded caller must not silently miss
    // FTS hits (the folded column is matched case-sensitively by design).
    std::string foldedNeedle(asciiFoldedNeedle);
    for (char& ch : foldedNeedle) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 0x20);
    }
    // Reviewer-A contract note: the eligibility gate is ASCII-only. A
    // non-ASCII needle is OUT OF CONTRACT for this API (the production
    // caller routes non-ASCII needles to the full canonical stream); such a
    // needle is ineligible here and falls back, but a fold-exceptional
    // non-ASCII needle can canonically match pure-ASCII names on some hosts,
    // so the superset claim is explicitly scoped to ASCII needles.
    const bool needleEligible = foldedNeedle.size() >= 3 &&
        std::all_of(foldedNeedle.begin(), foldedNeedle.end(),
                    [](char ch) { return static_cast<unsigned char>(ch) < 0x80; });
    const bool candidateRouteAvailable = needleEligible && CandidateIndexUsable();
    if (options_.candidatePlannerProbe && !candidateRouteAvailable) {
        // The planner made its decision: the candidate route is not usable
        // (ineligible needle or not-ready/foreign-host index). Report it so
        // tests can distinguish "route refused" from "route silently not
        // attempted".
        options_.candidatePlannerProbe(false, 0);
    }
    if (candidateRouteAvailable) {
        // durable-cold-search-index-and-hybrid-residency 5.3: volume-aware
        // bounded candidate admission. The counted workload must be the work
        // the candidate query will ACTUALLY do for the REQUESTED volume:
        // FTS postings admitted for this volume UNION this volume's
        // exception-side-set rows. A global count is not an adequate
        // approximation (PR #15 finalization campaign P2 defect): a
        // globally-broad but locally-selective needle was pushed to the
        // canonical scan by OTHER volumes' rows, and exception-side-set
        // candidates were never counted at all. Both counts are
        // LIMIT-bounded at cap+1 so the probe stays cheap for every extreme;
        // their sum can over-count (a row present in both sets), which is
        // the conservative direction for admission.
        const size_t cap = CandidateFallbackCap();
        std::string match;
        match.reserve(foldedNeedle.size() + 2);
        match.push_back('"');
        for (const char ch : foldedNeedle) {
            if (ch == '"') match.push_back('"');
            match.push_back(ch);
        }
        match.push_back('"');
        size_t candidateHits = 0;
        bool admissionKnown = false;
        if (gTestingGlobalAdmission.load(std::memory_order_acquire)) {
            // 5.3 testing arm ONLY: the pre-fix global (non-volume-scoped,
            // exception-blind) pre-count. Reproduces the campaign's Case
            // A/C/D defects on demand so the regression tests can prove they
            // discriminate. Never reachable in production.
            Statement countStmt(db_,
                "SELECT COUNT(*) FROM (SELECT 1 FROM entry_name_terms WHERE entry_name_terms MATCH ?1 LIMIT ?2);");
            if (countStmt.Valid()) {
                sqlite3_bind_text(countStmt, 1, match.data(), static_cast<int>(match.size()), SQLITE_TRANSIENT);
                sqlite3_bind_int64(countStmt, 2, static_cast<sqlite3_int64>(cap + 1));
                if (sqlite3_step(countStmt) == SQLITE_ROW) {
                    candidateHits = static_cast<size_t>(sqlite3_column_int64(countStmt, 0));
                    admissionKnown = true;
                }
            }
        } else {
            // Stage 1 (cheap global bound): if the WHOLE database admits at
            // most cap candidates then every volume does too, so the route
            // is safe without any volume-scoped probe. This preserves the
            // pre-planner fast path exactly for globally-selective needles
            // (the target classes): one bounded count, early-stopped at
            // cap+1 for both extremes. The side-set part is bounded too, so
            // a fold-exceptional-heavy host cannot silently over-admit
            // (mission case C).
            {
                Statement globalTerms(db_,
                    "SELECT COUNT(*) FROM (SELECT 1 FROM entry_name_terms WHERE entry_name_terms MATCH ?1 LIMIT ?2);");
                Statement globalExceptions(db_,
                    "SELECT COUNT(*) FROM (SELECT 1 FROM entry_name_exceptions LIMIT ?2);");
                if (globalTerms.Valid() && globalExceptions.Valid()) {
                    sqlite3_bind_text(globalTerms, 1, match.data(), static_cast<int>(match.size()), SQLITE_TRANSIENT);
                    sqlite3_bind_int64(globalTerms, 2, static_cast<sqlite3_int64>(cap + 1));
                    sqlite3_bind_int64(globalExceptions, 2, static_cast<sqlite3_int64>(cap + 1));
                    if (sqlite3_step(globalTerms) == SQLITE_ROW &&
                        sqlite3_step(globalExceptions) == SQLITE_ROW) {
                        candidateHits = static_cast<size_t>(sqlite3_column_int64(globalTerms, 0)) +
                                        static_cast<size_t>(sqlite3_column_int64(globalExceptions, 0));
                        admissionKnown = true;
                    }
                }
            }
            if (!admissionKnown || candidateHits > cap) {
                // Stage 2 (volume-aware; only for globally-broad needles):
                // the counted workload must be the work the candidate query
                // will ACTUALLY do for the REQUESTED volume -- FTS postings
                // admitted for this volume UNION this volume's
                // exception-side-set rows. A global count is not an adequate
                // approximation (PR #15 finalization campaign P2 defect): a
                // globally-broad but locally-selective needle was pushed to
                // the canonical scan by OTHER volumes' rows. CROSS JOIN pins
                // the driver: the FTS postings (and the tiny side-set)
                // stream OUTWARD and probe the map's term_rowid primary
                // key. The implicit-join form lets SQLite drive from the
                // volume's map index instead, which costs O(volume rows)
                // per query (measured: a 2M-row volume regressed selective
                // queries ~40x before this pin) -- the exact "expensive
                // count" failure mode the campaign forbids. Each part is
                // LIMIT-bounded at cap+1; their sum can over-count (a row
                // present in both sets), the conservative direction for
                // admission.
                candidateHits = 0;
                admissionKnown = false;
                {
                    Statement termsCount(db_,
                        "SELECT COUNT(*) FROM ("
                        "  SELECT 1 FROM (SELECT rowid AS rid FROM entry_name_terms"
                        "   WHERE entry_name_terms MATCH ?1) t"
                        "  CROSS JOIN entry_name_index_map m ON m.term_rowid = t.rid"
                        "  WHERE m.volume_id = ?2 LIMIT ?3);");
                    Statement exceptionCount(db_,
                        "SELECT COUNT(*) FROM ("
                        "  SELECT 1 FROM entry_name_exceptions x"
                        "  CROSS JOIN entry_name_index_map m ON m.term_rowid = x.term_rowid"
                        "  WHERE m.volume_id = ?2 LIMIT ?3);");
                    if (termsCount.Valid() && exceptionCount.Valid()) {
                        sqlite3_bind_text(termsCount, 1, match.data(), static_cast<int>(match.size()), SQLITE_TRANSIENT);
                        sqlite3_bind_int64(termsCount, 2, static_cast<sqlite3_int64>(volumeRowId));
                        sqlite3_bind_int64(termsCount, 3, static_cast<sqlite3_int64>(cap + 1));
                        sqlite3_bind_int64(exceptionCount, 2, static_cast<sqlite3_int64>(volumeRowId));
                        sqlite3_bind_int64(exceptionCount, 3, static_cast<sqlite3_int64>(cap + 1));
                        if (sqlite3_step(termsCount) == SQLITE_ROW &&
                            sqlite3_step(exceptionCount) == SQLITE_ROW) {
                            candidateHits = static_cast<size_t>(sqlite3_column_int64(termsCount, 0)) +
                                            static_cast<size_t>(sqlite3_column_int64(exceptionCount, 0));
                            admissionKnown = true;
                        }
                    }
                }
            }
        }
        // A failed pre-count is not evidence of a small workload: take the
        // canonical scan (cost-conservative) rather than trusting a zero.
        const bool useCandidateRoute = admissionKnown && candidateHits <= cap;
        if (options_.candidatePlannerProbe) {
            options_.candidatePlannerProbe(useCandidateRoute, candidateHits);
        }
        if (useCandidateRoute) {
            // 5.4: staged (atomic) candidate delivery. Rows are buffered
            // BEFORE any visitor delivery, so a query failure after partial
            // reads can never leak duplicate or partial rows into the
            // caller's result set -- the canonical stream below then
            // re-derives the complete set from scratch with zero deliveries
            // already made (PR #15 finalization campaign: the engine must
            // never see partial/duplicated results as a successful
            // canonical-equivalent search). The buffer is bounded by the
            // admitted cap; a real workload above the cap abandons atomically
            // and takes the canonical scan, so the cap keeps its cost
            // meaning even if the pre-count ever under-counts.
            std::vector<EntryRecord> staged;
            const bool stagedDelivery = gTestingStagedDelivery.load(std::memory_order_acquire);
            bool candidateRunOk = true;
            {
                // Plan pin (same rationale as the Stage-2 pre-count): drive
                // from the FTS postings UNION the tiny side-set, then probe
                // the map's term_rowid key and the entries primary key. The
                // implicit-join form lets SQLite drive from the volume's
                // entries and costs O(volume rows) per query whenever the
                // planner's row estimates are unfavorable -- measured as the
                // eligible-class latency floor appearing on statistics-free
                // databases (analyze-maintenance-scheduling campaign).
                Statement candidates(db_,
                    "SELECT e.frn_low, e.frn_high, e.parent_frn_low, e.parent_frn_high, e.name, e.size_bytes, "
                    "e.allocated_size_bytes, e.creation_time, e.last_modified_time, e.last_access_time, e.attributes "
                    "FROM (SELECT rowid AS rid FROM entry_name_terms WHERE entry_name_terms MATCH ?2"
                    " UNION SELECT term_rowid FROM entry_name_exceptions) t"
                    " CROSS JOIN entry_name_index_map m ON m.term_rowid = t.rid"
                    " CROSS JOIN entries e ON e.volume_id=m.volume_id AND e.frn_low=m.frn_low AND e.frn_high=m.frn_high"
                    " WHERE e.volume_id=?1;");
                if (!candidates.Valid()) {
                    candidateRunOk = false;
                } else {
                    sqlite3_bind_int64(candidates, 1, static_cast<sqlite3_int64>(volumeRowId));
                    sqlite3_bind_text(candidates, 2, match.data(), static_cast<int>(match.size()), SQLITE_TRANSIENT);
                    int stepResult = SQLITE_ERROR;
                    while ((stepResult = sqlite3_step(candidates)) == SQLITE_ROW) {
                        if (ShouldFail(ReadSessionFaultPoint::Stream)) {
                            failed_ = true;
                            return false;
                        }
                        if (ShouldFail(ReadSessionFaultPoint::CandidateStreamError)) {
                            // Deterministic mid-stream SQLite error (task
                            // 5.4 fault injection).
                            if (stagedDelivery) {
                                candidateRunOk = false;  // abandon ATOMICALLY
                                break;
                            }
                            stepResult = SQLITE_ERROR;  // legacy arm: fall through below
                            break;
                        }
                        if (stagedDelivery) {
                            if (staged.size() >= cap) {
                                candidateRunOk = false;
                                break;
                            }
                            staged.push_back(ReadEntryRecord(candidates));
                        } else {
                            // 5.4 testing arm ONLY (delivery off): the
                            // pre-fix deliver-as-you-go streaming.
                            if (!visitor(ReadEntryRecord(candidates))) return true;
                        }
                    }
                    if (stagedDelivery) {
                        if (candidateRunOk && stepResult != SQLITE_DONE) {
                            candidateRunOk = false;
                        }
                    } else if (stepResult == SQLITE_DONE) {
                        return true;
                    }
                }
            }
            if (candidateRunOk && stagedDelivery) {
                for (const EntryRecord& record : staged) {
                    if (!visitor(record)) return true;
                }
                return true;
            }
            // Atomic fall-through: nothing has been delivered to the
            // visitor, so the canonical stream below cannot duplicate rows
            // or corrupt capped top-N admission. (The legacy testing arm may
            // arrive here with rows already delivered -- that is the exact
            // pre-fix hazard the staged arm exists to eliminate.)
        }
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
