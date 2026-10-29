Task: READ-ONLY research lane A (FastFiles repo; work ONLY read-only, no shell, no edits) — SQLite storage and FTS. Repo: C:/Users/Michael Roy/Documents/FastFiles (use this tree; the successor worktree build/worktrees/durable-cold-search is identical at d3884ee). Investigate and report: (1) current SQLite build configuration — third_party/sqlite/sqlite3.c amalgamation + the sqlite3 CMake target: which compile definitions are set (FTS5 enabled? trigram tokenizer? ICU? SQLITE_ENABLE_* ?), check third_party/sqlite CMakeLists + any wrapper; (2) the durable schema as implemented in src/indexstore/src/Store.cpp + include/ffindexstore/Store.h: exact tables, columns, indexes, schema version handling, WAL/transaction boundaries, prepared statements used by the cold scan (ForEachEntryWithAsciiNameUntil etc.); (3) query plans: what the cold name scan executes (SELECT shape), why it costs ~1.05-1.10us/row, whether an index could serve it; (4) write amplification + WAL behavior of current ingestion (per-batch transaction boundaries); (5) an FTS5-based candidate structure design sketch for this exact schema (external-content vs standalone table; trigram tokenizer vs unicode61; prefix/substring eligibility) and its risks vs Windows CompareStringOrdinal folding. Deliver: findings with file:line evidence, a feasibility verdict for FTS5 candidate generation, and the exact measurement commands/SQL you would run (a human executes them; you cannot).
ls: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqlite
ls: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore
CMakeLists.txt
NOTICE.md
README.md
sqlite3.c
sqlite3.h
CMakeLists.txt
include/
src/
read: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
ls: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/src
ls: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
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
NamePool.cpp
Projection.cpp
Store.cpp
EntryRecord.h
FlatHashMap.h
Identity.h
NamePool.h
Projection.h
Store.h
grep: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
grep: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
grep: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
sqlite3.c:21673: # undef SQLITE_ENABLE_FTS3
sqlite3.c:21679: ** SQLITE_ENABLE_FTS3 macro.  But to avoid confusion we also call
sqlite3.c:21680: ** the SQLITE_ENABLE_FTS4 macro to serve as an alias for SQLITE_ENABLE_FTS3.
sqlite3.c:21682: #if defined(SQLITE_ENABLE_FTS4) && !defined(SQLITE_ENABLE_FTS3)
sqlite3.c:21683: # define SQLITE_ENABLE_FTS3 1
sqlite3.c:23280: #ifdef SQLITE_ENABLE_FTS3
sqlite3.c:23283: #ifdef SQLITE_ENABLE_FTS3_PARENTHESIS
sqlite3.c:23286: #ifdef SQLITE_ENABLE_FTS3_TOKENIZER
sqlite3.c:187296: #ifdef SQLITE_ENABLE_FTS3
sqlite3.c:187429: #ifdef SQLITE_ENABLE_FTS3
sqlite3.c:190894: #if defined(SQLITE_ENABLE_FTS3_TOKENIZER)
sqlite3.c:192977: **       SQLite (in which case SQLITE_ENABLE_FTS3 is defined).
sqlite3.c:193279: # undef SQLITE_ENABLE_FTS3
sqlite3.c:193285: ** SQLITE_ENABLE_FTS3 macro.  But to avoid confusion we also all
sqlite3.c:193286: ** the SQLITE_ENABLE_FTS4 macro to serve as an alisse for SQLITE_ENABLE_FTS3.
sqlite3.c:193288: #if defined(SQLITE_ENABLE_FTS4) && !defined(SQLITE_ENABLE_FTS3)
sqlite3.c:193289: # define SQLITE_ENABLE_FTS3
sqlite3.c:193292: #if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)
sqlite3.c:194224: #endif /* !SQLITE_CORE || SQLITE_ENABLE_FTS3 */
sqlite3.c:194229: #if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)
sqlite3.c:194231: #if defined(SQLITE_ENABLE_FTS3) && !defined(SQLITE_CORE)
sqlite3.c:200174: #if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)
sqlite3.c:200716: #endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3) */
sqlite3.c:200738: #if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)
sqlite3.c:200742: ** traditionally used by fts3. Or, if SQLITE_ENABLE_FTS3_PARENTHESIS
sqlite3.c:200788: # ifdef SQLITE_ENABLE_FTS3_PARENTHESIS
sqlite3.c:201292: ** When using the new fts3 query syntax (when SQLITE_ENABLE_FTS3_PARENTHESIS
sqlite3.c:202036: #endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3) */
sqlite3.c:202063: **       SQLite (in which case SQLITE_ENABLE_FTS3 is defined).
sqlite3.c:202066: #if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)
[30 matches limit reached. Use limit=60 for more, or refine pattern]
sqlite3.c:23292: #ifdef SQLITE_ENABLE_FTS5
sqlite3.c:187415: #ifdef SQLITE_ENABLE_FTS5
sqlite3.c:187432: #ifdef SQLITE_ENABLE_FTS5
sqlite3.c:241334: #if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS5)
sqlite3.c:269293: #endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS5) */
sqlite3.h:137: ** system</a>.  ^The SQLITE_SOURCE_ID macro evaluates to
sqlite3.h:139: ** within its configuration management system.  ^The SQLITE_SOURCE_ID
sqlite3.h:149: #define SQLITE_VERSION        "3.53.4"
sqlite3.h:151: #define SQLITE_SOURCE_ID      "2026-07-24 19:02:57 bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc"
sqlite3.h:161: ** [SQLITE_VERSION_NUMBER], and [SQLITE_SOURCE_ID] C preprocessor macros
sqlite3.h:170: ** assert( strncmp(sqlite3_sourceid(),SQLITE_SOURCE_ID,80)==0 );
sqlite3.h:182: ** [SQLITE_SOURCE_ID] C preprocessor macro.  Except if SQLite is built
sqlite3.h:184: ** of the hash might be different from [SQLITE_SOURCE_ID].)^
read: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
grep: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
#define SQLITE_CORRUPT_BKPT sqlite3CorruptError(__LINE__)
#define SQLITE_MISUSE_BKPT sqlite3MisuseError(__LINE__)
#define SQLITE_CANTOPEN_BKPT sqlite3CantopenError(__LINE__)
#ifdef SQLITE_DEBUG
SQLITE_PRIVATE   int sqlite3NomemError(int);
SQLITE_PRIVATE   int sqlite3IoerrnomemError(int);
# define SQLITE_NOMEM_BKPT sqlite3NomemError(__LINE__)
# define SQLITE_IOERR_NOMEM_BKPT sqlite3IoerrnomemError(__LINE__)
#else
# define SQLITE_NOMEM_BKPT SQLITE_NOMEM
# define SQLITE_IOERR_NOMEM_BKPT SQLITE_IOERR_NOMEM
#endif
#if defined(SQLITE_DEBUG) || defined(SQLITE_ENABLE_CORRUPT_PGNO)
SQLITE_PRIVATE   int sqlite3CorruptPgnoError(int,Pgno);
# define SQLITE_CORRUPT_PGNO(P) sqlite3CorruptPgnoError(__LINE__,(P))
#else
# define SQLITE_CORRUPT_PGNO(P) sqlite3CorruptError(__LINE__)
#endif
/*
** FTS3 and FTS4 both require virtual table support
*/
#if defined(SQLITE_OMIT_VIRTUALTABLE)
# undef SQLITE_ENABLE_FTS3
# undef SQLITE_ENABLE_FTS4
#endif
/*
** FTS4 is really an extension for FTS3.  It is enabled using the
** SQLITE_ENABLE_FTS3 macro.  But to avoid confusion we also call
** the SQLITE_ENABLE_FTS4 macro to serve as an alias for SQLITE_ENABLE_FTS3.
*/
#if defined(SQLITE_ENABLE_FTS4) && !defined(SQLITE_ENABLE_FTS3)
# define SQLITE_ENABLE_FTS3 1
#endif
/*
** The following macros mimic the standard library functions toupper(),
** isspace(), isalnum(), isdigit() and isxdigit(), respectively. The
** sqlite versions only work for ASCII characters, regardless of locale.
*/
#ifdef SQLITE_ASCII
# define sqlite3Toupper(x)  ((x)&~(sqlite3CtypeMap[(unsigned char)(x)]&0x20))
# define sqlite3Isspace(x)   (sqlite3CtypeMap[(unsigned char)(x)]&0x01)
# define sqlite3Isalnum(x)   (sqlite3CtypeMap[(unsigned char)(x)]&0x06)
# define sqlite3Isalpha(x)   (sqlite3CtypeMap[(unsigned char)(x)]&0x02)
# define sqlite3Isdigit(x)   (sqlite3CtypeMap[(unsigned char)(x)]&0x04)
# define sqlite3Isxdigit(x)  (sqlite3CtypeMap[(unsigned char)(x)]&0x08)
# define sqlite3Tolower(x)   (sqlite3UpperToLower[(unsigned char)(x)])
# define sqlite3Isquote(x)   (sqlite3CtypeMap[(unsigned char)(x)]&0x80)
[247951 more lines in file. Use offset=21700 to continue.]
sqlite3.c:247318: ** This function is only called when using the special 'trigram' tokenizer.
sqlite3.c:249261: ** trigram indexes that specify either detail=column or detail=none.
sqlite3.c:267122: ** Start of trigram implementation.
sqlite3.c:267131: ** Free a trigram tokenizer.
sqlite3.c:267138: ** Allocate a trigram tokenizer.
sqlite3.c:267215:   /* Populate aBuf[] with the characters for the first trigram. */
sqlite3.c:267228:   **  aBuf:      Contains 3 characters. The 3 characters of the next trigram.
sqlite3.c:267249:     /* Pass the current trigram back to fts5 */
sqlite3.c:267277: **     "trigram" tokenizer, case_sensitive=1 - FTS5_PATTERN_GLOB
sqlite3.c:267278: **     "trigram" tokenizer, case_sensitive=0 (the default) - FTS5_PATTERN_LIKE
[10 matches limit reached. Use limit=20 for more, or refine pattern]
Now let me examine the indexstore schema. First the header:
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
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
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
Store.cpp:16: // task 1.4: PRAGMA user_version is the stored schema-version marker.
Store.cpp:25:     "CREATE TABLE IF NOT EXISTS volumes ("
Store.cpp:38:     "CREATE TABLE IF NOT EXISTS entries ("
Store.cpp:55:     "CREATE INDEX IF NOT EXISTS idx_entries_parent ON entries(volume_id, parent_frn_low, parent_frn_high);";
Store.cpp:196:     Statement(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr); }
Store.cpp:243:     Statement exists(db, "SELECT 1 FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
Store.cpp:258:         "  SELECT volume_id, frn_low, frn_high, size_bytes, allocated_size_bytes, 1 AS depth"
Store.cpp:262:         "  SELECT e.volume_id, e.frn_low, e.frn_high, e.size_bytes, e.allocated_size_bytes, s.depth + 1"
Store.cpp:268:         "SELECT COUNT(*), COALESCE(SUM(size_bytes), 0), COALESCE(SUM(allocated_size_bytes), 0), "
Store.cpp:314:     Statement stmt(db_, "PRAGMA integrity_check;");
Store.cpp:369:     if (!ExecSql(db_, "PRAGMA journal_mode=WAL;") || !ExecSql(db_, "PRAGMA synchronous=NORMAL;")
Store.cpp:370:         || !ExecSql(db_, "PRAGMA foreign_keys=ON;")) {
Store.cpp:380:     Statement versionStmt(db_, "PRAGMA user_version;");
Store.cpp:387:         std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
Store.cpp:403:         std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
Store.cpp:457:     // (PRAGMA page_size) make the old approximation wrong by up to 8x. Query
Store.cpp:465:         if (sqlite3_prepare_v2(db_, "PRAGMA page_size;", -1, &stmt, nullptr) == SQLITE_OK) {
Store.cpp:520:         Statement select(db_, "SELECT id FROM volumes WHERE volume_guid=? AND serial_number=?;");
Store.cpp:531:     Statement insert(db_, "INSERT INTO volumes(volume_guid, serial_number, available, last_seen_time) VALUES (?,?,1,0);");
Store.cpp:584:     std::string sql = std::string("SELECT ") + kVolumeColumns + " FROM volumes WHERE id=?;";
Store.cpp:611:     std::string sql = std::string("SELECT ") + kVolumeColumns + " FROM volumes ORDER BY id;";
Store.cpp:635:         Statement current(db_, "SELECT available FROM volumes WHERE id=?;");
Store.cpp:725:         Statement eligibility(db_, "SELECT available FROM volumes WHERE id=?;");
Store.cpp:776:         "INSERT INTO entries(volume_id, frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, "
Store.cpp:802:             // Finding 4: on the INSERT arm the field mask must zero absent
Store.cpp:881:         "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
Store.cpp:916:         "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
Store.cpp:954:         "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
Store.cpp:988:     Statement stmt(db_, "SELECT frn_low, frn_high FROM entries WHERE volume_id=?;");
Store.cpp:1003:     Statement stmt(db_, "SELECT COUNT(*) FROM entries WHERE volume_id=?;");
Store.cpp:1024:         Statement exists(db_, "SELECT 1 FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
Store.cpp:1052:         "  SELECT volume_id, frn_low, frn_high, size_bytes, allocated_size_bytes, 1 AS depth"
Store.cpp:1056:         "  SELECT e.volume_id, e.frn_low, e.frn_high, e.size_bytes, e.allocated_size_bytes, s.depth + 1"
Store.cpp:1062:         "SELECT COUNT(*), COALESCE(SUM(size_bytes), 0), COALESCE(SUM(allocated_size_bytes), 0), "
Store.cpp:1123:         !ExecSql(db_, "PRAGMA query_only=ON;")) {
Store.cpp:1138:     Statement snapshot(db_, "SELECT id FROM volumes LIMIT 1;");
Store.cpp:1172:         "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
Store.cpp:1228:         "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
Store.cpp:1261:         "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
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
[1097 more lines in file. Use offset=201 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
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
[767 more lines in file. Use offset=531 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
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
[437 more lines in file. Use offset=861 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
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
The full `Store.cpp` is read. Now let me find the cold-scan call sites and any benchmarks that produced the ~1.05–1.10 µs/row numbers:
grep: C:/Users/Michael Roy/Documents/FastFiles
grep: C:/Users/Michael Roy/Documents/FastFiles
find: C:/Users/Michael Roy/Documents/FastFiles
src/indexstore/src/Store.cpp:1210: bool StoreReadSession::ForEachEntryWithAsciiNameUntil(
src/engine/src/IndexPipeline.cpp:367: // (StoreReadSession::ForEachEntryWithAsciiNameUntil): LIKE folds exactly the
src/engine/src/IndexPipeline.cpp:1905:                     ? context.reader->ForEachEntryWithAsciiNameUntil(coldVolumeId, *coldNamePrefilter,
src/engine/src/IndexPipeline.cpp:2035:                     ? context.reader->ForEachEntryWithAsciiNameUntil(volumeId, *durableNamePrefilter,
tests/indexstore/test_store_read_session.cpp:271:         const bool streamed = reader->ForEachEntryWithAsciiNameUntil(*volume, needle,
src/indexstore/include/ffindexstore/Store.h:190:     bool ForEachEntryUntil(VolumeRowId volumeRowId, const EntryVisitorUntil& visitor);
src/indexstore/include/ffindexstore/Store.h:253:     bool ForEachEntryUntil(VolumeRowId volumeRowId, const Store::EntryVisitorUntil& visitor);
src/indexstore/include/ffindexstore/Store.h:265:     bool ForEachEntryWithAsciiNameUntil(VolumeRowId volumeRowId, std::string_view asciiFoldedNeedle,
src/indexstore/include/ffindexstore/Projection.h:449:     bool ForEachEntryUntil(Fn&& fn, Cancel&& cancelled) const {
tests/search/bench_literal_matcher.cpp
tests/indexstore/bench_rebuild_isolation.cpp
tests/indexstore/bench_projection_memory.cpp
tests/engine/bench_secondary_filename_index.cpp
tests/engine/bench_large_index_scalability.cpp
tests/engine/bench_first_generation_publish.cpp
tests/engine/bench_allocation_isolation.cpp
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
// task 1.6: the size-triggered forced-checkpoint threshold. ~4000 WAL
// frames at the default 4 KiB page size (~16 MB) -- past that, a passive
// checkpoint is escalated to a full RESTART checkpoint.
constexpr uint64_t kWalForceCheckpointThresholdBytes = 4000ULL * 4096;
// Win32 ERROR_NOT_ENOUGH_MEMORY, kept numeric so this engine translation unit
// does not acquire a direct windows.h dependency for one diagnostic field.
constexpr uint32_t kErrorNotEnoughMemory = 8;
void UpdatePeak(std::atomic<size_t>& peak, const size_t value) {
    size_t observed = peak.load(std::memory_order_relaxed);
    while (observed < value && !peak.compare_exchange_weak(
               observed, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}
class CounterLease {
public:
    CounterLease(std::atomic<size_t>& active, std::atomic<size_t>& peak)
        : active_(&active), peak_(&peak) {
        const size_t current = active_->fetch_add(1, std::memory_order_relaxed) + 1;
        UpdatePeak(*peak_, current);
    }
    ~CounterLease() {
        if (active_ != nullptr) active_->fetch_sub(1, std::memory_order_relaxed);
    }
    CounterLease(const CounterLease&) = delete;
    CounterLease& operator=(const CounterLease&) = delete;
private:
    std::atomic<size_t>* active_;
    std::atomic<size_t>* peak_;
};
// large-index-lifecycle-and-search-scalability review-remediation 18.x:
// cold-stream prefilter eligibility. An ASCII-folded simple literal needle of
// >= 3 units can use the conservative SQL name prefilter
// (StoreReadSession::ForEachEntryWithAsciiNameUntil): LIKE folds exactly the
// ASCII case the superset proof needs and the query's own fold is already
// ASCII-lowercase. Non-ASCII needles return nullopt and keep the full stream
// because SQLite LIKE cannot fold non-ASCII case variants.
std::optional<std::string> ColdNamePrefilterNeedle(const ffsearch::Query& query) {
    if (!query.simpleLiteralName) return std::nullopt;
    const std::u16string& folded = query.simpleLiteralFolded;
    if (folded.size() < 3) return std::nullopt;
    std::string needle;
    needle.reserve(folded.size());
    for (const char16_t ch : folded) {
        if (ch > 0x7f) return std::nullopt;
        needle.push_back(static_cast<char>(ch));
    }
    return needle;
}
} // namespace
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
[1755 more lines in file. Use offset=450 to continue.]
bench_large_index_scalability.cpp:1116:     double elapsedMs = 0.0;
bench_large_index_scalability.cpp:1126:         result.elapsedMs = ElapsedMs(start);
bench_large_index_scalability.cpp:1148:     result.elapsedMs = ElapsedMs(start);
bench_large_index_scalability.cpp:1174:         result.uiMappingAndParsingMs = read.elapsedMs;
bench_large_index_scalability.cpp:1526:     double elapsedMs = 0.0;
bench_large_index_scalability.cpp:1532: MemoryCapturePoint CaptureMemoryPoint(const std::string& name, const double elapsedMs) {
bench_large_index_scalability.cpp:1546:     point.elapsedMs = elapsedMs;
bench_large_index_scalability.cpp:1551:                                           const double elapsedMs) {
bench_large_index_scalability.cpp:1552:     MemoryCapturePoint point = CaptureMemoryPoint(name, elapsedMs);
bench_large_index_scalability.cpp:1896:     output << ",\"elapsed_ms\":";
bench_large_index_scalability.cpp:1897:     WriteJsonDouble(output, point.elapsedMs);
bench_large_index_scalability.cpp:2205:                     ReadPublishedSnapshot(publisher.CurrentSectionName()).elapsedMs;
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
                                }
                                entry = &projection_.EntryAt((*context.filenameCandidates)[filenameIndex++]);
                            } else if (!context.cursor.Next(entry)) {
                                // large-index-lifecycle-and-search-scalability
                                // review-lane C Finding 2: a disturbed layout
                                // ends iteration early; treat it as a stale
                                // generation (restart + re-capture), never as
                                // exhausted (which would silently truncate).
                                if (context.cursor.Stale()) {
                                    stale = true;
                                    break;
                                }
                                hotExhausted = true;
                                break;
                            }
                            if (entry == nullptr || context.coldVolumes.contains(entry->volumeRowId)) continue;
                            const auto prefixIt = context.prefixes.find(entry->volumeRowId);
                            if (prefixIt == context.prefixes.end()) continue;
                            if (detailedDiagnostics) ++projectionEntriesVisited;
                            ++projectionRowsExamined;
                            if (entry->parentFrn != entry->frn) ++namePoolLookups;
                            ffsearch::Candidate candidate = MakeProjectionCandidate(
                                projection_, *entry, prefixIt->second);
                            if (candidate.name.empty()) {
                                if (detailedDiagnostics) ++emptyNamesRejected;
                                continue;
                            }
                            ++nameViewsConstructed;
                            ++candidateNameCopies;
                            chunk.push_back(std::move(candidate));
                        }
                        if (!projectionOnly && !searchRequest.query.viewCompatible) {
                            viewPathFallbackRows += chunk.size();
                        }
                    }
                    const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
                    maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
                    lock.unlock();
                }
                if (!matchingHookCalled) {
                    if (context.phaseHook) context.phaseHook(SearchPhase::Matching);
                    matchingHookCalled = true;
                }
                if (projectionOnly) {
                    search.candidatesVisited += chunk.size();
                } else {
                    for (auto& candidate : chunk) {
                        if (!accumulator.Add(std::move(candidate), materialize)) break;
                        ++candidateObjectsMaterialized;
                        if (stale || isCancelled() || accumulator.Cancelled()) break;
                    }
                }
                if (stale || isCancelled() || accumulator.Cancelled() || streamFailed) break;
            }
        }
        if (!stale && !isCancelled() && !accumulator.Cancelled()) {
            for (const auto coldVolumeId : context.orderedCold) {
                if (!coldHookCalled) {
                    if (context.phaseHook) context.phaseHook(SearchPhase::ColdStreaming);
                    coldHookCalled = true;
                }
                if (isCancelled() || stale) break;
                const auto prefixIt = context.prefixes.find(coldVolumeId);
                if (prefixIt == context.prefixes.end() || context.reader == nullptr) continue;
                ++coldVolumesSearched;
                const auto streamStart = Clock::now();
                const auto coldNamePrefilter = ColdNamePrefilterNeedle(searchRequest.query);
                const auto coldVisitor = [&](const ffindexstore::EntryRecord& record) {
                        if (isCancelled() || stale || accumulator.Cancelled()) return false;
                        ++coldRowsExamined;
                        ffsearch::Candidate candidate = MakeDurableCandidate(record, coldVolumeId,
                                                                               prefixIt->second);
                        if (candidate.name.empty()) return true;
                        ++candidateNameCopies;
                        if (projectionOnly) {
                            ++search.candidatesVisited;
                            return true;
                        }
                        const bool continueSearch = accumulator.Add(std::move(candidate), materialize);
                        if (!continueSearch || accumulator.Cancelled() || streamFailed) return false;
                        ++candidateObjectsMaterialized;
                        return !stale && !isCancelled();
                    };
                const bool streamed = coldNamePrefilter.has_value()
                    ? context.reader->ForEachEntryWithAsciiNameUntil(coldVolumeId, *coldNamePrefilter,
                                                                     coldVisitor)
                    : context.reader->ForEachEntryUntil(coldVolumeId, coldVisitor);
                storeStreamMs += std::chrono::duration<double, std::milli>(Clock::now() - streamStart).count();
                if (!streamed && !isCancelled() && !stale && !accumulator.Cancelled()) {
                    streamFailed = true;
                    break;
                }
            }
        }
        if (isCancelled() || accumulator.Cancelled()) {
            finishCancelled();
            return result;
        }
        if (context.phaseHook) context.phaseHook(SearchPhase::GenerationValidation);
        markStaleIfChanged();
        if (stale) {
            ++generationRestarts;
            generationRestarts_.fetch_add(1, std::memory_order_relaxed);
            if (restart == kMaxGenerationRestarts) {
                durableFallbackNeeded = true;
                break;
            }
            continue;
        }
        if (streamFailed) {
            readerFailures_.fetch_add(1, std::memory_order_relaxed);
            unavailable = true;
            break;
        }
        if (projectionOnly) {
            search.cancelled = false;
        } else {
            search = accumulator.Finish();
        }
        completed = true;
        if (timings != nullptr) {
            timings->searchGeneration = context.queryVisibleEpoch;
            timings->queryVisibleEpoch = context.queryVisibleEpoch;
            timings->dependencySetEpoch = context.dependencySetEpoch;
            timings->relevantInvalidations = relevantInvalidations;
            timings->generationRestarts = generationRestarts;
            timings->projectionTraversalMs =
                std::chrono::duration<double, std::milli>(Clock::now() - searchStart).count();
        }
        break;
    }
    if (durableFallbackNeeded && !unavailable && !completed) {
        durableFallbackUsed = true;
        durableFallbacks_.fetch_add(1, std::memory_order_relaxed);
        const auto fallbackStart = Clock::now();
        const auto recordFallbackTimings = [&](const uint64_t capturedQueryEpoch = 0,
                                               const uint64_t capturedDependencyEpoch = 0) {
            if (timings == nullptr) return;
            timings->searchGeneration = capturedQueryEpoch != 0
                ? capturedQueryEpoch : queryVisibleEpoch_.load(std::memory_order_acquire);
            timings->queryVisibleEpoch = timings->searchGeneration;
            timings->dependencySetEpoch = capturedDependencyEpoch != 0
                ? capturedDependencyEpoch : dependencySetEpoch_.load(std::memory_order_acquire);
            timings->relevantInvalidations = relevantInvalidations;
            timings->generationRestarts = generationRestarts;
            timings->durableFallbackUsed = durableFallbackUsed;
            timings->durableFallbackAttempts = durableFallbackAttempts;
            timings->durableFallbackRowsStreamed = durableFallbackRowsStreamed;
            timings->durableFallbackSucceeded = durableFallbackSucceeded;
        };
        for (size_t attempt = 0; attempt <= kMaxDurableFallbackConfigurationRetries; ++attempt) {
            if (isCancelled()) {
                recordFallbackTimings();
                finishCancelled();
                return result;
            }
            SearchContext context;
            if (!captureContext(context, false)) break;
            if (!context.ruleFilterActive) {
                searchRequest.candidateFilter = {};
                searchRequest.candidateFilterRequiresFolder = false;
            } else {
                const auto capturedRules = context.rules;
                searchRequest.candidateFilter = [capturedRules](const ffsearch::Candidate& candidate) {
                    return CandidateIncludedByRules(capturedRules, candidate);
                };
                searchRequest.candidateFilterRequiresFolder = true;
            }
            ++durableFallbackAttempts;
            if (context.phaseHook) context.phaseHook(SearchPhase::DurableFallback);
            CounterLease durableReaderLease(activeDurableReaders_, peakDurableReaders_);
            (void)durableReaderLease;
            bool streamFailed = false;
            size_t attemptRowsStreamed = 0;
            const auto materializeDurable = [&](ffsearch::Candidate& candidate) {
                if (isCancelled()) return false;
                const bool materialized = MaterializeCandidateFolderDurable(
                    *context.reader, context.prefixes, candidate, isCancelled);
                if (!materialized && !isCancelled()) streamFailed = true;
                return materialized;
            };
            const auto searchStart = Clock::now();
            ffsearch::SearchAccumulator accumulator(searchRequest, isCancelled,
                                                     (detailedDiagnostics || projectionOnly)
                                                         ? &searchDiagnostics : nullptr);
            for (const auto volumeId : context.searchableVolumes) {
                if (isCancelled() || accumulator.Cancelled()) break;
                const auto prefixIt = context.prefixes.find(volumeId);
                if (prefixIt == context.prefixes.end() || context.reader == nullptr) {
                    streamFailed = true;
                    break;
                }
                ++coldVolumesSearched;
                const auto streamStart = Clock::now();
                const auto durableNamePrefilter = ColdNamePrefilterNeedle(searchRequest.query);
                const auto durableVisitor = [&](const ffindexstore::EntryRecord& record) {
                        if (isCancelled() || accumulator.Cancelled()) return false;
                        ++attemptRowsStreamed;
                        ++coldRowsExamined;
                        ffsearch::Candidate candidate = MakeDurableCandidate(record, volumeId,
                                                                               prefixIt->second);
                        if (candidate.name.empty()) return true;
                        ++candidateNameCopies;
                        if (projectionOnly) {
                            ++search.candidatesVisited;
                            return true;
                        }
                        if (!accumulator.Add(std::move(candidate), materializeDurable)) return false;
                        ++candidateObjectsMaterialized;
                        return !isCancelled() && !accumulator.Cancelled() && !streamFailed;
                    };
                const bool streamed = durableNamePrefilter.has_value()
                    ? context.reader->ForEachEntryWithAsciiNameUntil(volumeId, *durableNamePrefilter,
                                                                     durableVisitor)
                    : context.reader->ForEachEntryUntil(volumeId, durableVisitor);
                storeStreamMs += std::chrono::duration<double, std::milli>(Clock::now() - streamStart).count();
                if (!streamed && !isCancelled() && !accumulator.Cancelled() && !streamFailed) {
                    streamFailed = true;
                }
                if (streamFailed) break;
            }
            durableFallbackRowsStreamed += attemptRowsStreamed;
            durableFallbackRowsStreamed_.fetch_add(attemptRowsStreamed, std::memory_order_relaxed);
            if (timings != nullptr) {
                timings->durableFallbackRowsStreamed += attemptRowsStreamed;
            }
            if (isCancelled() || accumulator.Cancelled()) {
                recordFallbackTimings(context.queryVisibleEpoch, context.dependencySetEpoch);
                finishCancelled();
                return result;
            }
            if (streamFailed || context.reader == nullptr || context.reader->Failed()) {
                readerFailures_.fetch_add(1, std::memory_order_relaxed);
                unavailable = true;
                break;
            }
            bool configurationCurrent = false;
            {
                const auto waitStart = Clock::now();
                std::unique_lock<std::mutex> lock(mutex_);
                lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
                const auto holdStart = Clock::now();
                configurationCurrent = isContextCurrentLocked(context, false);
                const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
                maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
                lock.unlock();
            }
            if (!configurationCurrent) {
                ++relevantInvalidations;
                continue;
            }
            if (projectionOnly) {
                search.cancelled = false;
            } else {
                search = accumulator.Finish();
            }
[125 more lines in file. Use offset=2080 to continue.]
Now let me find where the ~1.05–1.10 µs/row measurement comes from (likely the durable-cold-search planning docs/workstate):
grep: C:/Users/Michael Roy/Documents/FastFiles
find: C:/Users/Michael Roy/Documents/FastFiles
docs/benchmarks/secondary-filename-index.json:1: {"schema":"fastfiles-secondary-filename-index-v1","sqlite_fts_capability":{"compile_option_enable_fts5":false,"fts5_create":false,"tokenized_infix_recall":false,"trigram_create":false,"trigram_infix_recall":false,"create_error":"no such module: fts5"},"machine":{"logical_cores":12,"ram_bytes":25028239360,"compiler":"MSVC 194435228","build_configuration":"Release"},"scales":[{"dataset_size":100000,"architectures":[{"name":"linear_canonical_scan","build_ms":0.0001,"memory_before_build":{"private_b... [truncated]
openspec/changes/durable-cold-search-index-and-hybrid-residency/proposal.md:29:   scan itself (~1.05-1.10 us/scanned-row)** — a zero-survivor literal is
openspec/changes/durable-cold-search-index-and-hybrid-residency/design.md:17: - Cold stream floor: ~1.05-1.10 us per scanned row (name-column scan,
openspec/changes/large-index-lifecycle-and-search-scalability/design.md:13:   maintenance stay per entry: they are O(1) each (measured residual < 0.7 us/row).
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/rebuild/memory-report-before.json:21:   ,"scales":[{"scale":200000,"directory_count":3125,"file_count":196874,"bound_directories":4096,"rebuild_cycle_target":4,"database_bytes":15085568,"durable_volume_row_id":1,"generation_ms":2218.2362,"executed":true,"failure_reason":"","payload_identical":true,"rebuild_cycles":[{"name":"after_rebuild_cycle_1","elapsed_ms":2444.8243,"working_set_bytes":108871680,"private_bytes":103206912,"peak_working_set_bytes":145260544,"peak_private_bytes":154992640,"allocator":{"live_requested_bytes":76024719... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/memory-cold-decision.md:68:   us/row and a **scan floor of ~1.05-1.10 us/scanned-row** (consistent at both
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/memory-cold-decision.md:69:   scales) -- not the ~0.8-0.9 us/row previously written. Even a zero-survivor
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/memory-cold-decision.md:70:   literal is therefore ~1.05-1.1 s at 1M; on a 1-2% non-ASCII corpus (an
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/memory-cold-decision.md:82: floor is the SQLite name-column scan itself (~1.05-1.10 us/row, see the
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/projection-memory/projection-memory-1m-comparison-after-slack-release.json:14:   ,"scales":[{"scale":1000000,"directory_count":15625,"file_count":984374,"bound_directories":4096,"rebuild_cycle_target":4,"database_bytes":76599296,"durable_volume_row_id":1,"generation_ms":24314.7071,"executed":true,"failure_reason":"","payload_identical":true,"rebuild_cycles":[{"name":"after_rebuild_cycle_1","elapsed_ms":3937.4244,"working_set_bytes":598323200,"private_bytes":602812416,"peak_working_set_bytes":949800960,"peak_private_bytes":1079078912,"allocator":{"live_requested_bytes":4194... [truncated]
src/indexstore/src/Projection.cpp:882:     // (measured residual below 0.7 us/row) and preserve exact removal semantics.
[Some lines truncated to 500 chars. Use read tool to see full lines]
openspec/changes/durable-cold-search-index-and-hybrid-residency/
openspec/changes/durable-cold-search-index-and-hybrid-residency/tasks.md
openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/
openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/index-engine/
openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/index-engine/spec.md
openspec/changes/durable-cold-search-index-and-hybrid-residency/proposal.md
openspec/changes/durable-cold-search-index-and-hybrid-residency/design.md
Direct hits — there's an active OpenSpec change and measurement evidence. Let me read them:
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
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
# Memory budget & cold-query characterization (tasks 8.2/19) — 2026-10-21
Measurement host: same campaign host, idle, release-test binaries at
`b6df9ef` (after) vs `420792d` (before). Same harness/command both sides:
`ffengine_bench_large_index_scalability --scales 200000,1000000 --diagnostics`
(`build/campaign/cold-profile-*.json` + console logs).
## 1. Policy map (verified against source; full map in the Agent-C audit)
- `Projection::kDefaultMaximumMemoryBytes` = **500 MiB**; the early-enforcement
  watermark (`EnforceMemoryLimit` at batch boundaries, `IsMemoryPressureHigh`)
  is `max - max/5` = **400 MiB**. Only `SetMaximumMemoryBytes` enforces the
  strict 500 MiB ceiling synchronously.
- `IndexPipeline::SetProjectionMemoryBudget` has **no production caller**
  (repo-wide grep: tests only). The production budget is therefore the fixed
  500 MiB default.
- Eviction: subtree victims preferred (oldest directory subtree), and
  `EvictSubtree` marks the **whole volume** cold. Reload
  (`ReloadEvictedVolume` -> `RebuildVolumeFromStore`) is a full-volume
  re-stream under the pipeline mutex (snapshot export path only; search never
  reloads). A volume larger than the low watermark can never reload (tested
  thrash behavior).
- Cold search streams `SELECT ... FROM entries WHERE volume_id=?` with no
  name predicate and no LIMIT; every cold row was decoded and matched in C++.
## 2. Cold-path profiling (the bottleneck, proven before changing anything)
At 1M rows (forced eviction; `coldRows` = rows reaching the C++ matcher):
| class | before: COLD ms / rows | after: COLD ms / rows | delta |
|---|---:|---:|---:|
| selective | 1470 / 1,000,000 | 1141 / 98,438 | 1.29x |
| no_match | 1413 / 1,000,000 | 1101 / 98,437 | 1.28x |
| exact_name | 1601 / 1,000,000 | 1115 / 98,438 | 1.44x |
| moderate | 6261 / 1,000,000 | 5817 / 196,875 | 1.08x |
| prefix | 6087 / 1,000,000 | 6449 / 196,875 | ~1.0 (noise) |
| broad | 17,680 / 1,000,000 | 14,885 / 393,749 | 1.19x |
| worst_substring | 10,921 / 1,000,000 | 10,498 / 295,313 | 1.04x |
| mixed_case | 11,891 / 1,000,000 | 11,476 / 295,312 | 1.04x |
| high_limit_broad | 16,337 / 1,000,000 | 16,120 / 393,749 | 1.01x |
| short_prefix | 16,354 / 1,000,000 | 15,357 / 1,000,000 | prefilter ineligible (<3) |
| extension | 19,971 / 1,000,000 | 21,758 / 1,000,000 | prefilter ineligible (predicate) |
| unicode | 6437 / 1,000,000 | 6237 / 1,000,000 | prefilter ineligible (non-ASCII needle) |
Before the change `stream` (the SQLite stream + decode + matcher) was >99.8%
of every cold class (e.g. selective 1468.5 of 1469.7 ms); hot selective stayed
2.5 ms (accelerated). Review-lane E E-6 correction: that percentage is a
timer-coverage tautology (the `storeStreamMs` timer wraps the entire
stream+match loop), not a scan/decode/match localization -- no such split was
instrumented. The prefilter case rests on the row-count mechanism
(decode/materialize 1M -> 98k rows), the before/after delta, and the
below-subtraction floor, not on the attribution percentage.
Measurement-discipline corrections (review-lane E E-1/E-2/E-3):
- Every cold cell is a **single measured sample** per class per scale (the
  bench runs one measured cold query after one warmup; hot figures are
  medians over 10 (200k) / 5 (1M) warm runs). "COLD" here means
  cold-projection with a warm SQLite page cache.
- The two runs were **not host-comparable on CPU-bound paths**: untouched hot
  medians moved 1.2-2.8x at 1M and up to 1.4x slower at 200k. The defensible
  noise control is the prefilter-ineligible cold classes measured in the SAME
  block (extension 19,971->21,758 ms; short_prefix 16,354->15,357; unicode
  6,437->6,237: -9%..+6%). Against that control the 1.28-1.44x literal-class
  deltas exceed observed drift and the row-count mechanism supports the
  direction, but each cell is one sample and the effect carries ~±10%
  uncertainty.
- Two-point subtraction on the no_match pair gives a survivor cost of ~0.35
  us/row and a **scan floor of ~1.05-1.10 us/scanned-row** (consistent at both
  scales) -- not the ~0.8-0.9 us/row previously written. Even a zero-survivor
  literal is therefore ~1.05-1.1 s at 1M; on a 1-2% non-ASCII corpus (an
  unmeasured assumption; CJK-heavy corpora fall back toward the measured
  9.8%-survivor case) cold selective at 1M is ~1.1 s vs 1.47 s before
  (~1.3-1.4x).
- Provenance note (lane E E-8): the two cold-profile artifacts do not embed
  source identity; the `420792d`/`b6df9ef` pairing is asserted provenance
  (consistent with observable data such as the durable-fallback row drop),
  and future runs should embed `--source-sha` like the memory-report does.
The survivors at 1M are exactly the fixture's non-ASCII name population
(98,438 = 9.8%): the conservative GLOB disjunct admits every name carrying
non-ASCII bytes (host-fold safety, see review-remediation P2-A). The hard
floor is the SQLite name-column scan itself (~1.05-1.10 us/row, see the
corrections above), so expected real-corpus cold selective latency at 1M is
~1.1 s vs 1.47 s before -- about 1.3-1.4x. Run-to-run noise between the two
runs reached 2.8x on untouched hot paths (host state differed); the
same-block ineligible-class control bounds the drift at -9%..+6% for the
cold table.
## 3. The 2M regime (task 8.2 finding, characterized)
The 2M-row projection models ~839.7 MB > the 500 MiB default, so every 2M
query serves from the cold stream under the default budget (historical
search-matrix p50 at 2M: selective ~2.5 s, broad ~27.1 s, extension ~37.6 s;
the prefilter does not change non-literal classes and removes only the
survivor decode share for literals). Serving 2M rows hot would require a
budget near 1 GiB -- not raised here (see decision).
## 4. Product decision (mission 19): **Option A + E, with the cold prefilter
## shipped as the evidence-backed improvement**
- **Option A (retained):** keep the 500 MiB default; accelerate the cold path
  within existing semantics. Shipped: `ForEachEntryWithAsciiNameUntil`
  conservative SQL name prefilter (commit `b6df9ef`), superset-proven
  (LIKE-ASCII-fold + non-ASCII-bytes disjunct; red-first on the LIKE-escape
  under-match class) and measured above. No schema change, no new memory.
- **Option B (adaptive budget) — rejected for this change:** an adaptive
  policy needs floors/ceilings and behavior proofs under changing available
  RAM; none of the measured evidence requires it yet, and the mission forbids
  raising default consumption without safety proof.
- **Option C (hybrid hot/cold residency) — deferred as separately scoped
  architecture work:** eviction currently marks a whole volume cold and the
  reload path re-streams under the mutex; a compact searchable subset would
  need strict consistency + memory-bound proofs (a new OpenSpec change).
- **Option D (explicit setting) — rejected:** `SetProjectionMemoryBudget` has
  no production caller and no settings surface/persistence semantics exist;
  a half-wired setting would be a worse product than none.
- **Option E (document the limitation) — adopted:** the 2M-entry cold regime
  is documented precisely above; the remaining candidate improvement (a
  durable trigram/FTS candidate table, schema v3 + migration + superset proof)
  is deliberately NOT taken here because the mission requires separate schema
  justification and the measured 1.3-1.7x cold-literal gain does not justify
  that risk inside this campaign.
## 5. Retention classification (mission 23) for the one performance change
- Candidate: cold SQL name prefilter (`b6df9ef`), baseline `420792d`.
- Fixture: same synthetic 200k/1M corpora (10% non-ASCII names), same bench.
  Hot figures are medians over the harness's warm runs; every cold figure is
  a single measured sample per class per scale (lane E E-2), on runs whose
  CPU-bound paths were not host-comparable (E-1); see the corrections in §2.
- Correctness: hot-vs-cold identity+ordering signatures (extended
  `TestForcedEvictionSearchEquivalence`), store-level superset + escaping
  red-first (`build/campaign/prefilter-red-run.log`), full suites green.
- Decision: **RETAIN** (correct; 1.28-1.44x on cold literal classes at 10%
  non-ASCII vs a -9%..+6% same-block control, ~1.3-1.4x expected on real
  corpora; zero schema/memory risk). Scope conditions on the prior hot
  acceleration (lane E E-5): the 56-189x figure is hot/projection-resident,
  planner-eligible literal needles only (best case 189x = 1-result ASCII
  literal at 1M; rows examined 1M->1), it repairs a planner bug that had
  disabled the accelerator entirely, and it does NOT apply to cold queries
  or the >=2M regime, where all queries serve from the durable stream and
  latency is governed by the scan floor above. Lane E E-7: the 1M cold table
  characterizes the production cold-stream cost model under a state that at
  1M rows requires multi-volume memory pressure to reach (the 1M projection
  models ~318 MiB, below the 400 MiB watermark); the common production cold
  regime is >=2M rows.
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
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
bench_secondary_filename_index.cpp-626-     result.mutations = MeasureMutations<Index>(entries, scale);
bench_secondary_filename_index.cpp-627-     return result;
bench_secondary_filename_index.cpp-628- }
bench_secondary_filename_index.cpp-629- 
bench_secondary_filename_index.cpp-630- struct FtsCapability {
bench_secondary_filename_index.cpp-631-     bool compileOption = false;
bench_secondary_filename_index.cpp:632:     bool fts5Create = false;
bench_secondary_filename_index.cpp-633-     bool tokenizedInfixRecall = false;
bench_secondary_filename_index.cpp-634-     bool trigramCreate = false;
bench_secondary_filename_index.cpp-635-     bool trigramInfixRecall = false;
bench_secondary_filename_index.cpp-636-     std::string createError;
bench_secondary_filename_index.cpp-637- };
bench_secondary_filename_index.cpp-638- 
bench_secondary_filename_index.cpp-628- }
bench_secondary_filename_index.cpp-629- 
bench_secondary_filename_index.cpp-630- struct FtsCapability {
bench_secondary_filename_index.cpp-631-     bool compileOption = false;
bench_secondary_filename_index.cpp-632-     bool fts5Create = false;
bench_secondary_filename_index.cpp-633-     bool tokenizedInfixRecall = false;
bench_secondary_filename_index.cpp:634:     bool trigramCreate = false;
bench_secondary_filename_index.cpp-635-     bool trigramInfixRecall = false;
bench_secondary_filename_index.cpp-636-     std::string createError;
bench_secondary_filename_index.cpp-637- };
bench_secondary_filename_index.cpp-638- 
bench_secondary_filename_index.cpp-639- bool QueryHasRow(sqlite3* db, const char* sql, const char* value) {
bench_secondary_filename_index.cpp-640-     sqlite3_stmt* statement = nullptr;
bench_secondary_filename_index.cpp-629- 
bench_secondary_filename_index.cpp-630- struct FtsCapability {
bench_secondary_filename_index.cpp-631-     bool compileOption = false;
bench_secondary_filename_index.cpp-632-     bool fts5Create = false;
bench_secondary_filename_index.cpp-633-     bool tokenizedInfixRecall = false;
bench_secondary_filename_index.cpp-634-     bool trigramCreate = false;
bench_secondary_filename_index.cpp:635:     bool trigramInfixRecall = false;
bench_secondary_filename_index.cpp-636-     std::string createError;
bench_secondary_filename_index.cpp-637- };
bench_secondary_filename_index.cpp-638- 
bench_secondary_filename_index.cpp-639- bool QueryHasRow(sqlite3* db, const char* sql, const char* value) {
bench_secondary_filename_index.cpp-640-     sqlite3_stmt* statement = nullptr;
bench_secondary_filename_index.cpp-641-     if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
bench_secondary_filename_index.cpp-644-     sqlite3_finalize(statement);
bench_secondary_filename_index.cpp-645-     return found;
bench_secondary_filename_index.cpp-646- }
bench_secondary_filename_index.cpp-647- 
bench_secondary_filename_index.cpp-648- FtsCapability MeasureFtsCapability() {
bench_secondary_filename_index.cpp-649-     FtsCapability result;
bench_secondary_filename_index.cpp:650:     result.compileOption = sqlite3_compileoption_used("ENABLE_FTS5") != 0;
bench_secondary_filename_index.cpp-651-     sqlite3* db = nullptr;
bench_secondary_filename_index.cpp-652-     if (sqlite3_open(":memory:", &db) != SQLITE_OK) return result;
bench_secondary_filename_index.cpp-653-     char* error = nullptr;
bench_secondary_filename_index.cpp-654-     const int createResult = sqlite3_exec(db, "CREATE VIRTUAL TABLE names USING fts5(name);", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp-655-     result.fts5Create = createResult == SQLITE_OK;
bench_secondary_filename_index.cpp-656-     if (!result.fts5Create && error != nullptr) {
bench_secondary_filename_index.cpp-648- FtsCapability MeasureFtsCapability() {
bench_secondary_filename_index.cpp-649-     FtsCapability result;
bench_secondary_filename_index.cpp-650-     result.compileOption = sqlite3_compileoption_used("ENABLE_FTS5") != 0;
bench_secondary_filename_index.cpp-651-     sqlite3* db = nullptr;
bench_secondary_filename_index.cpp-652-     if (sqlite3_open(":memory:", &db) != SQLITE_OK) return result;
bench_secondary_filename_index.cpp-653-     char* error = nullptr;
bench_secondary_filename_index.cpp:654:     const int createResult = sqlite3_exec(db, "CREATE VIRTUAL TABLE names USING fts5(name);", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp-655-     result.fts5Create = createResult == SQLITE_OK;
bench_secondary_filename_index.cpp-656-     if (!result.fts5Create && error != nullptr) {
bench_secondary_filename_index.cpp-657-         result.createError = error;
bench_secondary_filename_index.cpp-658-         sqlite3_free(error);
bench_secondary_filename_index.cpp-659-         error = nullptr;
bench_secondary_filename_index.cpp-660-     }
bench_secondary_filename_index.cpp-649-     FtsCapability result;
bench_secondary_filename_index.cpp-650-     result.compileOption = sqlite3_compileoption_used("ENABLE_FTS5") != 0;
bench_secondary_filename_index.cpp-651-     sqlite3* db = nullptr;
bench_secondary_filename_index.cpp-652-     if (sqlite3_open(":memory:", &db) != SQLITE_OK) return result;
bench_secondary_filename_index.cpp-653-     char* error = nullptr;
bench_secondary_filename_index.cpp-654-     const int createResult = sqlite3_exec(db, "CREATE VIRTUAL TABLE names USING fts5(name);", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp:655:     result.fts5Create = createResult == SQLITE_OK;
bench_secondary_filename_index.cpp-656-     if (!result.fts5Create && error != nullptr) {
bench_secondary_filename_index.cpp-657-         result.createError = error;
bench_secondary_filename_index.cpp-658-         sqlite3_free(error);
bench_secondary_filename_index.cpp-659-         error = nullptr;
bench_secondary_filename_index.cpp-660-     }
bench_secondary_filename_index.cpp-661-     if (result.fts5Create) {
bench_secondary_filename_index.cpp-650-     result.compileOption = sqlite3_compileoption_used("ENABLE_FTS5") != 0;
bench_secondary_filename_index.cpp-651-     sqlite3* db = nullptr;
bench_secondary_filename_index.cpp-652-     if (sqlite3_open(":memory:", &db) != SQLITE_OK) return result;
bench_secondary_filename_index.cpp-653-     char* error = nullptr;
bench_secondary_filename_index.cpp-654-     const int createResult = sqlite3_exec(db, "CREATE VIRTUAL TABLE names USING fts5(name);", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp-655-     result.fts5Create = createResult == SQLITE_OK;
bench_secondary_filename_index.cpp:656:     if (!result.fts5Create && error != nullptr) {
bench_secondary_filename_index.cpp-657-         result.createError = error;
bench_secondary_filename_index.cpp-658-         sqlite3_free(error);
bench_secondary_filename_index.cpp-659-         error = nullptr;
bench_secondary_filename_index.cpp-660-     }
bench_secondary_filename_index.cpp-661-     if (result.fts5Create) {
bench_secondary_filename_index.cpp-662-         sqlite3_exec(db, "INSERT INTO names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-655-     result.fts5Create = createResult == SQLITE_OK;
bench_secondary_filename_index.cpp-656-     if (!result.fts5Create && error != nullptr) {
bench_secondary_filename_index.cpp-657-         result.createError = error;
bench_secondary_filename_index.cpp-658-         sqlite3_free(error);
bench_secondary_filename_index.cpp-659-         error = nullptr;
bench_secondary_filename_index.cpp-660-     }
bench_secondary_filename_index.cpp:661:     if (result.fts5Create) {
bench_secondary_filename_index.cpp-662-         sqlite3_exec(db, "INSERT INTO names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-663-         result.tokenizedInfixRecall = QueryHasRow(db, "SELECT rowid FROM names WHERE names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-664-     }
bench_secondary_filename_index.cpp-665-     sqlite3_exec(db, "DROP TABLE IF EXISTS trigram_names;", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-666-     const int trigramResult = sqlite3_exec(db,
bench_secondary_filename_index.cpp-667-         "CREATE VIRTUAL TABLE trigram_names USING fts5(name, tokenize='trigram');", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp-659-         error = nullptr;
bench_secondary_filename_index.cpp-660-     }
bench_secondary_filename_index.cpp-661-     if (result.fts5Create) {
bench_secondary_filename_index.cpp-662-         sqlite3_exec(db, "INSERT INTO names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-663-         result.tokenizedInfixRecall = QueryHasRow(db, "SELECT rowid FROM names WHERE names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-664-     }
bench_secondary_filename_index.cpp:665:     sqlite3_exec(db, "DROP TABLE IF EXISTS trigram_names;", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-666-     const int trigramResult = sqlite3_exec(db,
bench_secondary_filename_index.cpp-667-         "CREATE VIRTUAL TABLE trigram_names USING fts5(name, tokenize='trigram');", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp-668-     result.trigramCreate = trigramResult == SQLITE_OK;
bench_secondary_filename_index.cpp-669-     if (error != nullptr) sqlite3_free(error);
bench_secondary_filename_index.cpp-670-     if (result.trigramCreate) {
bench_secondary_filename_index.cpp-671-         sqlite3_exec(db, "INSERT INTO trigram_names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-660-     }
bench_secondary_filename_index.cpp-661-     if (result.fts5Create) {
bench_secondary_filename_index.cpp-662-         sqlite3_exec(db, "INSERT INTO names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-663-         result.tokenizedInfixRecall = QueryHasRow(db, "SELECT rowid FROM names WHERE names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-664-     }
bench_secondary_filename_index.cpp-665-     sqlite3_exec(db, "DROP TABLE IF EXISTS trigram_names;", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp:666:     const int trigramResult = sqlite3_exec(db,
bench_secondary_filename_index.cpp-667-         "CREATE VIRTUAL TABLE trigram_names USING fts5(name, tokenize='trigram');", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp-668-     result.trigramCreate = trigramResult == SQLITE_OK;
bench_secondary_filename_index.cpp-669-     if (error != nullptr) sqlite3_free(error);
bench_secondary_filename_index.cpp-670-     if (result.trigramCreate) {
bench_secondary_filename_index.cpp-671-         sqlite3_exec(db, "INSERT INTO trigram_names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-672-         result.trigramInfixRecall = QueryHasRow(db, "SELECT rowid FROM trigram_names WHERE trigram_names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-661-     if (result.fts5Create) {
bench_secondary_filename_index.cpp-662-         sqlite3_exec(db, "INSERT INTO names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-663-         result.tokenizedInfixRecall = QueryHasRow(db, "SELECT rowid FROM names WHERE names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-664-     }
bench_secondary_filename_index.cpp-665-     sqlite3_exec(db, "DROP TABLE IF EXISTS trigram_names;", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-666-     const int trigramResult = sqlite3_exec(db,
bench_secondary_filename_index.cpp:667:         "CREATE VIRTUAL TABLE trigram_names USING fts5(name, tokenize='trigram');", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp-668-     result.trigramCreate = trigramResult == SQLITE_OK;
bench_secondary_filename_index.cpp-669-     if (error != nullptr) sqlite3_free(error);
bench_secondary_filename_index.cpp-670-     if (result.trigramCreate) {
bench_secondary_filename_index.cpp-671-         sqlite3_exec(db, "INSERT INTO trigram_names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-672-         result.trigramInfixRecall = QueryHasRow(db, "SELECT rowid FROM trigram_names WHERE trigram_names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-673-     }
bench_secondary_filename_index.cpp-662-         sqlite3_exec(db, "INSERT INTO names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-663-         result.tokenizedInfixRecall = QueryHasRow(db, "SELECT rowid FROM names WHERE names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-664-     }
bench_secondary_filename_index.cpp-665-     sqlite3_exec(db, "DROP TABLE IF EXISTS trigram_names;", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-666-     const int trigramResult = sqlite3_exec(db,
bench_secondary_filename_index.cpp-667-         "CREATE VIRTUAL TABLE trigram_names USING fts5(name, tokenize='trigram');", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp:668:     result.trigramCreate = trigramResult == SQLITE_OK;
bench_secondary_filename_index.cpp-669-     if (error != nullptr) sqlite3_free(error);
bench_secondary_filename_index.cpp-670-     if (result.trigramCreate) {
bench_secondary_filename_index.cpp-671-         sqlite3_exec(db, "INSERT INTO trigram_names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-672-         result.trigramInfixRecall = QueryHasRow(db, "SELECT rowid FROM trigram_names WHERE trigram_names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-673-     }
bench_secondary_filename_index.cpp-674-     sqlite3_close(db);
bench_secondary_filename_index.cpp-664-     }
bench_secondary_filename_index.cpp-665-     sqlite3_exec(db, "DROP TABLE IF EXISTS trigram_names;", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-666-     const int trigramResult = sqlite3_exec(db,
bench_secondary_filename_index.cpp-667-         "CREATE VIRTUAL TABLE trigram_names USING fts5(name, tokenize='trigram');", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp-668-     result.trigramCreate = trigramResult == SQLITE_OK;
bench_secondary_filename_index.cpp-669-     if (error != nullptr) sqlite3_free(error);
bench_secondary_filename_index.cpp:670:     if (result.trigramCreate) {
bench_secondary_filename_index.cpp-671-         sqlite3_exec(db, "INSERT INTO trigram_names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-672-         result.trigramInfixRecall = QueryHasRow(db, "SELECT rowid FROM trigram_names WHERE trigram_names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-673-     }
bench_secondary_filename_index.cpp-674-     sqlite3_close(db);
bench_secondary_filename_index.cpp-675-     return result;
bench_secondary_filename_index.cpp-676- }
bench_secondary_filename_index.cpp-665-     sqlite3_exec(db, "DROP TABLE IF EXISTS trigram_names;", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-666-     const int trigramResult = sqlite3_exec(db,
bench_secondary_filename_index.cpp-667-         "CREATE VIRTUAL TABLE trigram_names USING fts5(name, tokenize='trigram');", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp-668-     result.trigramCreate = trigramResult == SQLITE_OK;
bench_secondary_filename_index.cpp-669-     if (error != nullptr) sqlite3_free(error);
bench_secondary_filename_index.cpp-670-     if (result.trigramCreate) {
bench_secondary_filename_index.cpp:671:         sqlite3_exec(db, "INSERT INTO trigram_names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp-672-         result.trigramInfixRecall = QueryHasRow(db, "SELECT rowid FROM trigram_names WHERE trigram_names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-673-     }
bench_secondary_filename_index.cpp-674-     sqlite3_close(db);
bench_secondary_filename_index.cpp-675-     return result;
bench_secondary_filename_index.cpp-676- }
bench_secondary_filename_index.cpp-677- 
bench_secondary_filename_index.cpp-666-     const int trigramResult = sqlite3_exec(db,
bench_secondary_filename_index.cpp-667-         "CREATE VIRTUAL TABLE trigram_names USING fts5(name, tokenize='trigram');", nullptr, nullptr, &error);
bench_secondary_filename_index.cpp-668-     result.trigramCreate = trigramResult == SQLITE_OK;
bench_secondary_filename_index.cpp-669-     if (error != nullptr) sqlite3_free(error);
bench_secondary_filename_index.cpp-670-     if (result.trigramCreate) {
bench_secondary_filename_index.cpp-671-         sqlite3_exec(db, "INSERT INTO trigram_names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
bench_secondary_filename_index.cpp:672:         result.trigramInfixRecall = QueryHasRow(db, "SELECT rowid FROM trigram_names WHERE trigram_names MATCH ?;", "pha");
bench_secondary_filename_index.cpp-673-     }
bench_secondary_filename_index.cpp-674-     sqlite3_close(db);
bench_secondary_filename_index.cpp-675-     return result;
bench_secondary_filename_index.cpp-676- }
bench_secondary_filename_index.cpp-677- 
bench_secondary_filename_index.cpp-678- struct Options {
bench_secondary_filename_index.cpp-781-         output << ",\"exact_after_mutation\":" << (mutation.exactAfterMutation ? "true" : "false") << '}';
bench_secondary_filename_index.cpp-782-     }
bench_secondary_filename_index.cpp-783-     output << "]}";
bench_secondary_filename_index.cpp-784- }
bench_secondary_filename_index.cpp-785- 
bench_secondary_filename_index.cpp-786- void WriteFts(std::ostream& output, const FtsCapability& capability) {
bench_secondary_filename_index.cpp:787:     output << "{\"compile_option_enable_fts5\":" << (capability.compileOption ? "true" : "false")
bench_secondary_filename_index.cpp-788-            << ",\"fts5_create\":" << (capability.fts5Create ? "true" : "false")
bench_secondary_filename_index.cpp-789-            << ",\"tokenized_infix_recall\":" << (capability.tokenizedInfixRecall ? "true" : "false")
bench_secondary_filename_index.cpp-790-            << ",\"trigram_create\":" << (capability.trigramCreate ? "true" : "false")
bench_secondary_filename_index.cpp-791-            << ",\"trigram_infix_recall\":" << (capability.trigramInfixRecall ? "true" : "false")
bench_secondary_filename_index.cpp-792-            << ",\"create_error\":";
bench_secondary_filename_index.cpp-793-     WriteJsonString(output, capability.createError);
bench_secondary_filename_index.cpp-782-     }
bench_secondary_filename_index.cpp-783-     output << "]}";
bench_secondary_filename_index.cpp-784- }
bench_secondary_filename_index.cpp-785- 
bench_secondary_filename_index.cpp-786- void WriteFts(std::ostream& output, const FtsCapability& capability) {
bench_secondary_filename_index.cpp-787-     output << "{\"compile_option_enable_fts5\":" << (capability.compileOption ? "true" : "false")
bench_secondary_filename_index.cpp:788:            << ",\"fts5_create\":" << (capability.fts5Create ? "true" : "false")
bench_secondary_filename_index.cpp-789-            << ",\"tokenized_infix_recall\":" << (capability.tokenizedInfixRecall ? "true" : "false")
bench_secondary_filename_index.cpp-790-            << ",\"trigram_create\":" << (capability.trigramCreate ? "true" : "false")
bench_secondary_filename_index.cpp-791-            << ",\"trigram_infix_recall\":" << (capability.trigramInfixRecall ? "true" : "false")
bench_secondary_filename_index.cpp-792-            << ",\"create_error\":";
bench_secondary_filename_index.cpp-793-     WriteJsonString(output, capability.createError);
bench_secondary_filename_index.cpp-794-     output << '}';
bench_secondary_filename_index.cpp-784- }
bench_secondary_filename_index.cpp-785- 
bench_secondary_filename_index.cpp-786- void WriteFts(std::ostream& output, const FtsCapability& capability) {
bench_secondary_filename_index.cpp-787-     output << "{\"compile_option_enable_fts5\":" << (capability.compileOption ? "true" : "false")
bench_secondary_filename_index.cpp-788-            << ",\"fts5_create\":" << (capability.fts5Create ? "true" : "false")
bench_secondary_filename_index.cpp-789-            << ",\"tokenized_infix_recall\":" << (capability.tokenizedInfixRecall ? "true" : "false")
bench_secondary_filename_index.cpp:790:            << ",\"trigram_create\":" << (capability.trigramCreate ? "true" : "false")
bench_secondary_filename_index.cpp-791-            << ",\"trigram_infix_recall\":" << (capability.trigramInfixRecall ? "true" : "false")
bench_secondary_filename_index.cpp-792-            << ",\"create_error\":";
bench_secondary_filename_index.cpp-793-     WriteJsonString(output, capability.createError);
bench_secondary_filename_index.cpp-794-     output << '}';
bench_secondary_filename_index.cpp-795- }
bench_secondary_filename_index.cpp-796- 
bench_secondary_filename_index.cpp-785- 
bench_secondary_filename_index.cpp-786- void WriteFts(std::ostream& output, const FtsCapability& capability) {
bench_secondary_filename_index.cpp-787-     output << "{\"compile_option_enable_fts5\":" << (capability.compileOption ? "true" : "false")
bench_secondary_filename_index.cpp-788-            << ",\"fts5_create\":" << (capability.fts5Create ? "true" : "false")
bench_secondary_filename_index.cpp-789-            << ",\"tokenized_infix_recall\":" << (capability.tokenizedInfixRecall ? "true" : "false")
bench_secondary_filename_index.cpp-790-            << ",\"trigram_create\":" << (capability.trigramCreate ? "true" : "false")
bench_secondary_filename_index.cpp:791:            << ",\"trigram_infix_recall\":" << (capability.trigramInfixRecall ? "true" : "false")
bench_secondary_filename_index.cpp-792-            << ",\"create_error\":";
bench_secondary_filename_index.cpp-793-     WriteJsonString(output, capability.createError);
bench_secondary_filename_index.cpp-794-     output << '}';
bench_secondary_filename_index.cpp-795- }
bench_secondary_filename_index.cpp-796- 
bench_secondary_filename_index.cpp-797- } // namespace
bench_secondary_filename_index.cpp-810-         const ffsearch::FilterRegistry registry = ffsearch::FilterRegistry::WithDefaults();
bench_secondary_filename_index.cpp-811-         for (const QuerySpec& spec : kQuerySpecs) {
bench_secondary_filename_index.cpp-812-             queries.emplace_back(spec, ffsearch::ParseQuery(spec.text, registry));
bench_secondary_filename_index.cpp-813-         }
bench_secondary_filename_index.cpp-814-         std::vector<ArchitectureMeasurement> measurements;
bench_secondary_filename_index.cpp-815-         measurements.push_back(MeasureArchitecture<LinearIndex>("linear_canonical_scan", entries, scale, queries));
bench_secondary_filename_index.cpp:816:         measurements.push_back(MeasureArchitecture<TrigramIndex>("utf16_trigram_postings", entries, scale, queries));
bench_secondary_filename_index.cpp-817-         measurements.push_back(MeasureArchitecture<PrefixIndex>("sorted_normalized_names", entries, scale, queries));
bench_secondary_filename_index.cpp-818-         allScales.push_back(std::move(measurements));
bench_secondary_filename_index.cpp-819-     }
bench_secondary_filename_index.cpp-820- 
bench_secondary_filename_index.cpp-821-     std::ofstream output(options.outputPath, std::ios::binary | std::ios::trunc);
bench_secondary_filename_index.cpp-822-     if (!output) return 3;
bench_secondary_filename_index.cpp-842-         for (size_t architectureIndex = 0; architectureIndex < allScales[scaleIndex].size(); ++architectureIndex) {
bench_secondary_filename_index.cpp-843-             if (architectureIndex != 0) output << ',';
bench_secondary_filename_index.cpp-844-             WriteArchitecture(output, allScales[scaleIndex][architectureIndex]);
bench_secondary_filename_index.cpp-845-         }
bench_secondary_filename_index.cpp-846-         output << "]}";
bench_secondary_filename_index.cpp-847-     }
bench_secondary_filename_index.cpp:848:     output << "],\"decision\":{\"selected\":\"utf16_trigram_postings\",\"production_integration\":false,"
bench_secondary_filename_index.cpp-849-               "\"winner_reason\":\"zero_false_negatives_with_large_selective_and_no_match_candidate_reduction_at_2m\","
bench_secondary_filename_index.cpp-850-               "\"rejected_fts_reason\":\"vendored_sqlite_is_not_compiled_with_fts5_and_create_fails\","
bench_secondary_filename_index.cpp-851-               "\"rejected_prefix_reason\":\"current_literal_search_is_infix_and_prefix_ranges_missed_canonical_matches\","
bench_secondary_filename_index.cpp-852-               "\"secondary_index_recall_rule\":\"canonical_match_set_must_be_subset_of_candidates\","
bench_secondary_filename_index.cpp-853-               "\"short_query_policy\":\"linear_fallback_for_terms_shorter_than_three_utf16_units\","
bench_secondary_filename_index.cpp-854-               "\"unicode_policy\":\"benchmark_uses_ascii_literal_eligibility_and_verifies_non_ascii_names\","
bench_secondary_filename_index.cpp-844-             WriteArchitecture(output, allScales[scaleIndex][architectureIndex]);
bench_secondary_filename_index.cpp-845-         }
bench_secondary_filename_index.cpp-846-         output << "]}";
bench_secondary_filename_index.cpp-847-     }
bench_secondary_filename_index.cpp-848-     output << "],\"decision\":{\"selected\":\"utf16_trigram_postings\",\"production_integration\":false,"
bench_secondary_filename_index.cpp-849-               "\"winner_reason\":\"zero_false_negatives_with_large_selective_and_no_match_candidate_reduction_at_2m\","
bench_secondary_filename_index.cpp:850:               "\"rejected_fts_reason\":\"vendored_sqlite_is_not_compiled_with_fts5_and_create_fails\","
bench_secondary_filename_index.cpp-851-               "\"rejected_prefix_reason\":\"current_literal_search_is_infix_and_prefix_ranges_missed_canonical_matches\","
bench_secondary_filename_index.cpp-852-               "\"secondary_index_recall_rule\":\"canonical_match_set_must_be_subset_of_candidates\","
bench_secondary_filename_index.cpp-853-               "\"short_query_policy\":\"linear_fallback_for_terms_shorter_than_three_utf16_units\","
bench_secondary_filename_index.cpp-854-               "\"unicode_policy\":\"benchmark_uses_ascii_literal_eligibility_and_verifies_non_ascii_names\","
bench_secondary_filename_index.cpp-855-               "\"prefix_semantics\":\"current_search_is_infix; prefix_only_ranges_are_not_safe_without_a_query-semantics_change\"}}\n";
bench_secondary_filename_index.cpp-856-     return output.good() ? 0 : 4;
Now for ingestion batching and WAL cadence — let me find `ApplyBatch` call sites and batch sizing:
grep: C:/Users/Michael Roy/Documents/FastFiles/src
grep: C:/Users/Michael Roy/Documents/FastFiles/src
indexstore/src/Store.cpp-759-     }
indexstore/src/Store.cpp-760-     return ok;
indexstore/src/Store.cpp-761- }
indexstore/src/Store.cpp-762- 
indexstore/src/Store.cpp:763: bool Store::ApplyBatch(VolumeRowId volumeRowId, const std::vector<EntryChange>& changes) {
indexstore/src/Store.cpp-764-     if (db_ == nullptr) {
indexstore/src/Store.cpp-765-         return false;
indexstore/src/Store.cpp-766-     }
indexstore/src/Store.cpp-767-     if (changes.empty()) {
engine/src/IndexPipeline.cpp-603-                     const bool injectedFailure = false;
engine/src/IndexPipeline.cpp-604- #endif
engine/src/IndexPipeline.cpp-605-                     bool secondaryApplied = false;
engine/src/IndexPipeline.cpp-606-                     if (!injectedFailure) {
engine/src/IndexPipeline.cpp:607:                         secondaryApplied = store_.ApplyBatch(meta.rowId, ruleExcluded);
engine/src/IndexPipeline.cpp-608-                     }
engine/src/IndexPipeline.cpp-609-                     if (secondaryApplied) {
engine/src/IndexPipeline.cpp-610-                         std::vector<ffindexstore::FileId> excludedIds;
engine/src/IndexPipeline.cpp-611-                         excludedIds.reserve(ruleExcluded.size());
engine/src/IndexPipeline.cpp-728-                                          std::vector<ffindexstore::EntryChange>&& changes) {
engine/src/IndexPipeline.cpp-729-     std::lock_guard<std::mutex> lock(mutex_);
engine/src/IndexPipeline.cpp-730-     // design.md D4: commit to the durable store first; the projection is
engine/src/IndexPipeline.cpp-731-     // only touched once that commit has actually succeeded.
engine/src/IndexPipeline.cpp:732:     if (!store_.ApplyBatch(volumeId, changes)) {
engine/src/IndexPipeline.cpp-733-         return false;
engine/src/IndexPipeline.cpp-734-     }
engine/src/IndexPipeline.cpp-735-     MaybeAnalyzeAfterBulkInsertLocked(changes.size());
engine/src/IndexPipeline.cpp-736-     for (const auto& change : changes) {
engine/src/IndexPipeline.cpp-769-         const bool injectedFailure = false;
engine/src/IndexPipeline.cpp-770- #endif
engine/src/IndexPipeline.cpp-771-         bool secondaryApplied = false;
engine/src/IndexPipeline.cpp-772-         if (!injectedFailure) {
engine/src/IndexPipeline.cpp:773:             secondaryApplied = store_.ApplyBatch(volumeId, ruleExcluded);
engine/src/IndexPipeline.cpp-774-         }
engine/src/IndexPipeline.cpp-775-         if (secondaryApplied) {
engine/src/IndexPipeline.cpp-776-             std::vector<ffindexstore::FileId> excludedIds;
engine/src/IndexPipeline.cpp-777-             excludedIds.reserve(ruleExcluded.size());
engine/src/IndexPipeline.cpp-852- 
engine/src/IndexPipeline.cpp-853- bool IndexPipeline::ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
engine/src/IndexPipeline.cpp-854-                                          std::vector<ffindexstore::EntryChange>&& changes) {
engine/src/IndexPipeline.cpp-855-     std::lock_guard<std::mutex> lock(mutex_);
engine/src/IndexPipeline.cpp:856:     if (!store_.ApplyBatch(volumeId, changes)) {
engine/src/IndexPipeline.cpp-857-         return false;
engine/src/IndexPipeline.cpp-858-     }
engine/src/IndexPipeline.cpp-859-     size_t upsertCount = 0;
engine/src/IndexPipeline.cpp-860-     for (const auto& change : changes) {
engine/src/IndexPipeline.cpp-911-         const bool injectedFailure = false;
engine/src/IndexPipeline.cpp-912- #endif
engine/src/IndexPipeline.cpp-913-         bool secondaryApplied = false;
engine/src/IndexPipeline.cpp-914-         if (!injectedFailure) {
engine/src/IndexPipeline.cpp:915:             secondaryApplied = store_.ApplyBatch(volumeId, ruleExcluded);
engine/src/IndexPipeline.cpp-916-         }
engine/src/IndexPipeline.cpp-917-         if (secondaryApplied) {
engine/src/IndexPipeline.cpp-918-             std::vector<ffindexstore::FileId> excludedIds;
engine/src/IndexPipeline.cpp-919-             excludedIds.reserve(ruleExcluded.size());
engine/src/IndexPipeline.cpp-1046- #else
engine/src/IndexPipeline.cpp-1047-         const bool injectedFailure = false;
engine/src/IndexPipeline.cpp-1048- #endif
engine/src/IndexPipeline.cpp-1049-         if (!injectedFailure) {
engine/src/IndexPipeline.cpp:1050:             secondaryApplied = store_.ApplyBatch(id, removals);
engine/src/IndexPipeline.cpp-1051-         } else {
engine/src/IndexPipeline.cpp-1052-             secondaryApplied = false;
engine/src/IndexPipeline.cpp-1053-         }
engine/src/IndexPipeline.cpp-1054-         if (secondaryApplied) {
indexstore/include/ffindexstore/Store.h-172-     // updates one row, not two"); Remove changes delete that row. Returns
indexstore/include/ffindexstore/Store.h-173-     // false (and leaves the database unchanged, courtesy of the rolled-
indexstore/include/ffindexstore/Store.h-174-     // back transaction) on any failure, so the caller can retry the same
indexstore/include/ffindexstore/Store.h-175-     // batch (task 3.4) without risking a partial commit.
indexstore/include/ffindexstore/Store.h:176:     bool ApplyBatch(VolumeRowId volumeRowId, const std::vector<EntryChange>& changes);
indexstore/include/ffindexstore/Store.h-177- 
indexstore/include/ffindexstore/Store.h-178-     // task 3.1: streams every entry belonging to a volume, in an
indexstore/include/ffindexstore/Store.h-179-     // unspecified but stable-for-the-duration-of-the-call order, without
indexstore/include/ffindexstore/Store.h-180-     // materializing the whole result set at once.
No matches found
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine
grep: C:/Users/Michael Roy/Documents/FastFiles/src/protocol
src/IndexPipeline.h:265:     bool ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
src/IndexPipeline.cpp:829:     return ApplyUsnBatchChanges(volumeId, std::move(changes));
src/IndexPipeline.cpp:850:     return ApplyUsnBatchChanges(volumeId, std::move(changes));
src/IndexPipeline.cpp:853: bool IndexPipeline::ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
src/PrivilegedConnection.cpp:384:         case MessageType::ScanRecordBatch: {
src/PrivilegedConnection.cpp:385:             if (frame.payload.size() < sizeof(ffprotocol::ScanRecordBatchHeader)) {
src/PrivilegedConnection.cpp:389:             ffprotocol::ScanRecordBatchHeader header{};
src/PrivilegedConnection.cpp:411:         case MessageType::ScanRecordBatchV2: {
src/PrivilegedConnection.cpp:416:             if (frame.payload.size() < sizeof(ffprotocol::ScanRecordBatchV2Header)) {
src/PrivilegedConnection.cpp:420:             ffprotocol::ScanRecordBatchV2Header header{};
src/UiProtocol.cpp:133:         payloadSize > kMaxFrameSize - sizeof(FrameHeader)) {
src/UiProtocol.cpp:176:         payloadSize > kMaxFrameSize - sizeof(FrameHeader)) {
src/UiProtocol.cpp:282:     constexpr size_t kMaxPayload = kMaxFrameSize - sizeof(FrameHeader);
src/UiProtocol.cpp:344:         payloadSize > kMaxFrameSize - sizeof(FrameHeader)) {
src/UiProtocol.cpp:430:     constexpr size_t kMaxPayload = kMaxFrameSize - sizeof(FrameHeader);
src/Frame.cpp:7:     return length64 >= sizeof(FrameHeader) && length64 <= kMaxFrameSize;
include/ffprotocol/UiProtocol.h:79: // are variable-length and every UI frame is capped at kMaxFrameSize.
include/ffprotocol/Records.h:16: // independent of kMaxFrameSize -- bounds the size of the pre-parse
include/ffprotocol/Frame.h:19: constexpr uint32_t kMaxFrameSize = 1u * 1024u * 1024u; // 1 MiB
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/P...
[pi-lens: read widened to the enclosing function_definition "function_definition" (symbol boundary): you asked for lines 380-459, this shows lines 342-598. Re-request with limit > 100 for the exact range.]
void PrivilegedConnection::DispatchReceivedFrame(
    const ffipc::ReceivedFrame& frame, std::atomic<bool>& heartbeatAckPending, std::mutex& ackMutex,
    std::condition_variable& ackCv, std::atomic<bool>& protocolViolation) {
    // Fail-closed transport-generation guard: only the current authenticated
    // Active transport may deliver frames. A frame that arrives while the
    // connection is unauthenticated (pre-handshake), stale (a replaced
    // generation's reader still draining), or already torn down carries no
    // scan authority, so it is rejected before any callback. ActiveLoop's
    // normal exit joins its reader before SetState(Disconnected);
    // ResetForScanAuthority deliberately flips the state before that join, so
    // a frame still in flight during a reset belongs to the discarded
    // generation and is dropped on purpose rather than applied.
    if (state_.load() != ConnectionState::Active) {
        protocolViolation = true;
        return;
    }
    auto messageType = ffprotocol::ToMessageType(frame.header.messageType);
    if (!messageType) {
        protocolViolation = true;
        return;
    }
    switch (*messageType) {
        case MessageType::HeartbeatAck: {
            std::lock_guard<std::mutex> lock(ackMutex);
            heartbeatAckPending = false;
            ackCv.notify_all();
            break;
        }
        case MessageType::VolumeList: {
            auto volumes = ParseVolumeList(frame.payload);
            if (!volumes) {
                protocolViolation = true;
                break;
            }
            if (volumeListCallback_) {
                volumeListCallback_(std::move(*volumes));
            }
            break;
        }
        case MessageType::ScanRecordBatch: {
            if (frame.payload.size() < sizeof(ffprotocol::ScanRecordBatchHeader)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::ScanRecordBatchHeader header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            const size_t afterHeader = sizeof(header);
            if (frame.payload.size() < afterHeader + header.resumeCursorLengthBytes) {
                protocolViolation = true;
                break;
            }
            std::vector<uint8_t> cursor(
                frame.payload.begin() + afterHeader, frame.payload.begin() + afterHeader + header.resumeCursorLengthBytes);
            const size_t recordsOffset = afterHeader + header.resumeCursorLengthBytes;
            auto records = ffprotocol::ParseMftBatch(
                frame.payload.data() + recordsOffset, frame.payload.size() - recordsOffset, header.recordCount);
            if (!records) {
                protocolViolation = true;
                break;
            }
            if (scanBatchCallback_) {
                scanBatchCallback_(header.volumeId, std::move(cursor), std::move(*records));
            }
            break;
        }
        case MessageType::ScanRecordBatchV2: {
            // allocated-size-storage-accuracy 5.1: additive MFT record
            // layout with allocated size; distinct message type plus an
            // explicit recordFormatVersion so a V1 payload can never be
            // misinterpreted as the larger fixed records.
            if (frame.payload.size() < sizeof(ffprotocol::ScanRecordBatchV2Header)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::ScanRecordBatchV2Header header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            if (header.recordFormatVersion != 2) {
                protocolViolation = true;
                break;
            }
            const size_t afterHeader = sizeof(header);
            if (frame.payload.size() < afterHeader + header.resumeCursorLengthBytes) {
                protocolViolation = true;
                break;
            }
            std::vector<uint8_t> cursor(
                frame.payload.begin() + afterHeader, frame.payload.begin() + afterHeader + header.resumeCursorLengthBytes);
            const size_t recordsOffset = afterHeader + header.resumeCursorLengthBytes;
            auto records = ffprotocol::ParseMftBatchV2(
                frame.payload.data() + recordsOffset, frame.payload.size() - recordsOffset, header.recordCount);
            if (!records) {
                protocolViolation = true;
                break;
            }
            if (scanBatchV2Callback_) {
                scanBatchV2Callback_(header.volumeId, std::move(cursor), std::move(*records));
            }
            break;
        }
        case MessageType::ScanComplete: {
            if (frame.payload.size() != sizeof(ffprotocol::ScanCompletePayload)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::ScanCompletePayload payload{};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            if (scanCompleteCallback_) {
                scanCompleteCallback_(payload.volumeId);
            }
            break;
        }
        case MessageType::ScanFailed: {
            // index-durability-false-success-closure D1: exact-length check
            // mirrors ScanComplete -- a re-sized frame is a violation, never
            // a silently-tolerated variant.
            if (frame.payload.size() != sizeof(ffprotocol::ScanFailedPayload)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::ScanFailedPayload payload{};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            if (scanFailedCallback_) {
                scanFailedCallback_(payload.volumeId);
            }
            break;
        }
        case MessageType::ScanAttemptStarted: {
            // scan-generation-integrity-and-residual-certification-closure
            // 3.4: exact-length and reply-only. A longer payload is never an
            // attempt discriminator extension; it is a protocol violation.
            if (frame.payload.size() != sizeof(ffprotocol::ScanAttemptStartedPayload)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::ScanAttemptStartedPayload payload{};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            if (scanAttemptStartedCallback_) {
                scanAttemptStartedCallback_(payload.volumeId);
            }
            break;
        }
        case MessageType::UsnJournalOpened: {
            if (frame.payload.size() != sizeof(ffprotocol::UsnJournalOpenedPayload)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::UsnJournalOpenedPayload payload{};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            if (journalOpenedCallback_) {
                journalOpenedCallback_(payload.volumeId, payload.journalId, payload.currentUsn);
            }
            break;
        }
        case MessageType::JournalResumeInvalid: {
            if (frame.payload.size() != sizeof(ffprotocol::JournalResumeInvalidPayload)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::JournalResumeInvalidPayload payload{};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            if (journalResumeInvalidCallback_) {
                journalResumeInvalidCallback_(payload.volumeId);
            }
            break;
        }
        case MessageType::JournalRecordBatch: {
            if (frame.payload.size() < sizeof(ffprotocol::JournalRecordBatchHeader)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::JournalRecordBatchHeader header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            const size_t recordsOffset = sizeof(header);
            auto records = ffprotocol::ParseUsnDeltaBatch(
                frame.payload.data() + recordsOffset, frame.payload.size() - recordsOffset, header.recordCount);
            if (!records) {
                protocolViolation = true;
                break;
            }
            if (journalBatchCallback_) {
                std::vector<ffprotocol::UsnDeltaV2> upgraded;
                upgraded.reserve(records->size());
                for (const auto& record : *records) {
                    upgraded.push_back(ffprotocol::UpgradeUsnDeltaV1(record));
                }
                journalBatchCallback_(header.volumeId, header.latestUsn, std::move(upgraded));
            }
            break;
        }
        case MessageType::JournalRecordBatchV2: {
            if (frame.payload.size() < sizeof(ffprotocol::JournalRecordBatchV2Header)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::JournalRecordBatchV2Header header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            if (header.recordFormatVersion != 2) {
                protocolViolation = true;
                break;
            }
            const size_t recordsOffset = sizeof(header);
            auto records = ffprotocol::ParseUsnDeltaV2Batch(
                frame.payload.data() + recordsOffset, frame.payload.size() - recordsOffset, header.recordCount);
            if (!records) {
                protocolViolation = true;
                break;
            }
            if (journalBatchCallback_) {
                journalBatchCallback_(header.volumeId, header.latestUsn, std::move(*records));
            }
            break;
        }
        case MessageType::JournalRecordBatchV3: {
            // allocated-size-storage-accuracy 5.1: additive USN V3 layout
            // (V2 + allocated-size presence bit).
            if (frame.payload.size() < sizeof(ffprotocol::JournalRecordBatchV3Header)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::JournalRecordBatchV3Header header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            if (header.recordFormatVersion != 3) {
                protocolViolation = true;
                break;
            }
            const size_t recordsOffset = sizeof(header);
            auto records = ffprotocol::ParseUsnDeltaV3Batch(
                frame.payload.data() + recordsOffset, frame.payload.size() - recordsOffset, header.recordCount);
            if (!records) {
                protocolViolation = true;
                break;
            }
            if (journalBatchV3Callback_) {
                journalBatchV3Callback_(header.volumeId, header.latestUsn, std::move(*records));
            }
            break;
        }
        default:
            // Handshake/HandshakeAck/etc. have no business appearing once
            // the connection is Active.
            protocolViolation = true;
            break;
    }
}
[194 more lines in file. Use offset=599 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
PrivilegedConnection.cpp-437-                 protocolViolation = true;
PrivilegedConnection.cpp-438-                 break;
PrivilegedConnection.cpp-439-             }
PrivilegedConnection.cpp:440:             if (scanBatchV2Callback_) {
PrivilegedConnection.cpp-441-                 scanBatchV2Callback_(header.volumeId, std::move(cursor), std::move(*records));
PrivilegedConnection.cpp-442-             }
PrivilegedConnection.cpp-443-             break;
PrivilegedConnection.cpp-438-                 break;
PrivilegedConnection.cpp-439-             }
PrivilegedConnection.cpp-440-             if (scanBatchV2Callback_) {
PrivilegedConnection.cpp:441:                 scanBatchV2Callback_(header.volumeId, std::move(cursor), std::move(*records));
PrivilegedConnection.cpp-442-             }
PrivilegedConnection.cpp-443-             break;
PrivilegedConnection.cpp-444-         }
PrivilegedConnection.h-79-     // the time a frame arrives, and these are not expected to change
PrivilegedConnection.h-80-     // after the connection is up.
PrivilegedConnection.h-81-     void SetVolumeListCallback(VolumeListCallback callback) { volumeListCallback_ = std::move(callback); }
PrivilegedConnection.h:82:     void SetScanBatchCallback(ScanBatchCallback callback) { scanBatchCallback_ = std::move(callback); }
PrivilegedConnection.h-83-     void SetScanBatchV2Callback(ScanBatchV2Callback callback) { scanBatchV2Callback_ = std::move(callback); }
PrivilegedConnection.h-84-     void SetScanCompleteCallback(ScanCompleteCallback callback) { scanCompleteCallback_ = std::move(callback); }
PrivilegedConnection.h-85-     void SetScanFailedCallback(ScanFailedCallback callback) { scanFailedCallback_ = std::move(callback); }
PrivilegedConnection.h-80-     // after the connection is up.
PrivilegedConnection.h-81-     void SetVolumeListCallback(VolumeListCallback callback) { volumeListCallback_ = std::move(callback); }
PrivilegedConnection.h-82-     void SetScanBatchCallback(ScanBatchCallback callback) { scanBatchCallback_ = std::move(callback); }
PrivilegedConnection.h:83:     void SetScanBatchV2Callback(ScanBatchV2Callback callback) { scanBatchV2Callback_ = std::move(callback); }
PrivilegedConnection.h-84-     void SetScanCompleteCallback(ScanCompleteCallback callback) { scanCompleteCallback_ = std::move(callback); }
PrivilegedConnection.h-85-     void SetScanFailedCallback(ScanFailedCallback callback) { scanFailedCallback_ = std::move(callback); }
PrivilegedConnection.h-86-     void SetScanAttemptStartedCallback(ScanAttemptStartedCallback callback) {
PrivilegedConnection.h-182-     StateChangeCallback onStateChange_;
PrivilegedConnection.h-183-     VolumeListCallback volumeListCallback_;
PrivilegedConnection.h-184-     ScanBatchCallback scanBatchCallback_;
PrivilegedConnection.h:185:     ScanBatchV2Callback scanBatchV2Callback_;
PrivilegedConnection.h-186-     ScanCompleteCallback scanCompleteCallback_;
PrivilegedConnection.h-187-     ScanFailedCallback scanFailedCallback_;
PrivilegedConnection.h-188-     ScanAttemptStartedCallback scanAttemptStartedCallback_;
VolumeSessionManager.cpp-223- 
VolumeSessionManager.cpp-224- void VolumeSessionManager::Start() {
VolumeSessionManager.cpp-225-     connection_.SetVolumeListCallback([this](std::vector<ffprotocol::VolumeInfo> volumes) { OnVolumeList(std::move(volumes)); });
VolumeSessionManager.cpp:226:     connection_.SetScanBatchCallback([this](ffprotocol::VolumeId id, std::vector<uint8_t> cursor,
VolumeSessionManager.cpp-227-                                              std::vector<ffprotocol::MftRecordV1> records) {
VolumeSessionManager.cpp-228-         OnScanBatch(id, std::move(cursor), std::move(records));
VolumeSessionManager.cpp-229-     });
IndexPipeline.cpp:445:     recordsSinceAnalyze_ = 0;
IndexPipeline.cpp:463:     recordsSinceAnalyze_ = 0;
IndexPipeline.cpp:536: void IndexPipeline::MaybeAnalyzeAfterBulkInsertLocked(size_t recordCount) {
IndexPipeline.cpp:541:     if (recordsSinceAnalyze_ > std::numeric_limits<uint64_t>::max() - count) {
IndexPipeline.cpp:542:         recordsSinceAnalyze_ = kAnalyzeRecordThreshold;
IndexPipeline.cpp:544:         recordsSinceAnalyze_ += count;
IndexPipeline.cpp:546:     if (recordsSinceAnalyze_ < kAnalyzeRecordThreshold) {
IndexPipeline.cpp:554:         recordsSinceAnalyze_ %= kAnalyzeRecordThreshold;
IndexPipeline.cpp:735:     MaybeAnalyzeAfterBulkInsertLocked(changes.size());
IndexPipeline.cpp:865:     MaybeAnalyzeAfterBulkInsertLocked(upsertCount);
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/V...
No matches found
VolumeSessionManager.cpp-839-     // seen-set.
VolumeSessionManager.cpp-840-     {
VolumeSessionManager.cpp-841-         std::scoped_lock lock(mutex_, wakeMutex_);
VolumeSessionManager.cpp-842-         RearmFailedAdmissionLocked(ephemeralId.value, durableId, /*reconciliation=*/true);
VolumeSessionManager.cpp-843-     }
VolumeSessionManager.cpp-844-     wakeCv_.notify_one();
VolumeSessionManager.cpp-845-     connection_.ResetForScanAuthority();
VolumeSessionManager.cpp-846-     return false;
VolumeSessionManager.cpp-847- }
VolumeSessionManager.cpp-848- 
VolumeSessionManager.cpp:849: void VolumeSessionManager::OnScanBatch(
VolumeSessionManager.cpp-850-     ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor, std::vector<ffprotocol::MftRecordV1> records) {
VolumeSessionManager.cpp-851-     ApplyScanBatch(volumeId, std::move(cursor), [&](ffindexstore::VolumeRowId durableId) {
VolumeSessionManager.cpp-852-         return pipeline_.ApplyMftBatch(durableId, records);
VolumeSessionManager.cpp-853-     });
VolumeSessionManager.cpp-854- }
VolumeSessionManager.cpp-855- 
VolumeSessionManager.cpp-856- void VolumeSessionManager::OnScanBatchV2(
VolumeSessionManager.cpp-857-     ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor, std::vector<ffprotocol::MftRecordV2> records) {
VolumeSessionManager.cpp-858-     // allocated-size-storage-accuracy 5.2: identical lifecycle to the V1
VolumeSessionManager.cpp-859-     // path; only the record type (and thus allocated-size availability)
VolumeSessionManager.cpp-846-     return false;
VolumeSessionManager.cpp-847- }
VolumeSessionManager.cpp-848- 
VolumeSessionManager.cpp-849- void VolumeSessionManager::OnScanBatch(
VolumeSessionManager.cpp-850-     ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor, std::vector<ffprotocol::MftRecordV1> records) {
VolumeSessionManager.cpp-851-     ApplyScanBatch(volumeId, std::move(cursor), [&](ffindexstore::VolumeRowId durableId) {
VolumeSessionManager.cpp-852-         return pipeline_.ApplyMftBatch(durableId, records);
VolumeSessionManager.cpp-853-     });
VolumeSessionManager.cpp-854- }
VolumeSessionManager.cpp-855- 
VolumeSessionManager.cpp:856: void VolumeSessionManager::OnScanBatchV2(
VolumeSessionManager.cpp-857-     ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor, std::vector<ffprotocol::MftRecordV2> records) {
VolumeSessionManager.cpp-858-     // allocated-size-storage-accuracy 5.2: identical lifecycle to the V1
VolumeSessionManager.cpp-859-     // path; only the record type (and thus allocated-size availability)
VolumeSessionManager.cpp-860-     // differs.
VolumeSessionManager.cpp-861-     ApplyScanBatch(volumeId, std::move(cursor), [&](ffindexstore::VolumeRowId durableId) {
VolumeSessionManager.cpp-862-         return pipeline_.ApplyMftBatch(durableId, records);
VolumeSessionManager.cpp-863-     });
VolumeSessionManager.cpp-864- }
VolumeSessionManager.cpp-865- 
VolumeSessionManager.cpp-866- void VolumeSessionManager::ApplyScanBatch(ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor,
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
src/IndexPipeline.h-456- 
src/IndexPipeline.h-457-     // address-engineering-audit-findings 5.1: keep planner statistics fresh
src/IndexPipeline.h-458-     // without adding ANALYZE work to every small journal update. The counter
src/IndexPipeline.h-459-     // is retained when ANALYZE fails so a later ingestion batch retries it.
src/IndexPipeline.h:460:     static constexpr uint64_t kAnalyzeRecordThreshold = 10'000;
src/IndexPipeline.h-461-     void MaybeAnalyzeAfterBulkInsertLocked(size_t recordCount);
src/IndexPipeline.h-462-     void MaybeReportProjectionMemoryPressureLocked();
src/IndexPipeline.h-463-     uint64_t recordsSinceAnalyze_ = 0;
src/IndexPipeline.h-464-     bool projectionMemoryWarningIssued_ = false;
src/IndexPipeline.cpp-538-         return;
src/IndexPipeline.cpp-539-     }
src/IndexPipeline.cpp-540-     const uint64_t count = static_cast<uint64_t>(recordCount);
src/IndexPipeline.cpp-541-     if (recordsSinceAnalyze_ > std::numeric_limits<uint64_t>::max() - count) {
src/IndexPipeline.cpp:542:         recordsSinceAnalyze_ = kAnalyzeRecordThreshold;
src/IndexPipeline.cpp-543-     } else {
src/IndexPipeline.cpp-544-         recordsSinceAnalyze_ += count;
src/IndexPipeline.cpp-545-     }
src/IndexPipeline.cpp-546-     if (recordsSinceAnalyze_ < kAnalyzeRecordThreshold) {
src/IndexPipeline.cpp-542-         recordsSinceAnalyze_ = kAnalyzeRecordThreshold;
src/IndexPipeline.cpp-543-     } else {
src/IndexPipeline.cpp-544-         recordsSinceAnalyze_ += count;
src/IndexPipeline.cpp-545-     }
src/IndexPipeline.cpp:546:     if (recordsSinceAnalyze_ < kAnalyzeRecordThreshold) {
src/IndexPipeline.cpp-547-         return;
src/IndexPipeline.cpp-548-     }
src/IndexPipeline.cpp-549-     // address-engineering-audit-findings 5.1: ANALYZE runs only after the
src/IndexPipeline.cpp-550-     // durable batch commits. A failed maintenance operation never makes an
src/IndexPipeline.cpp-550-     // durable batch commits. A failed maintenance operation never makes an
src/IndexPipeline.cpp-551-     // already-committed ingestion look failed; retaining the threshold makes
src/IndexPipeline.cpp-552-     // the next batch retry the statistics refresh.
src/IndexPipeline.cpp-553-     if (store_.Analyze()) {
src/IndexPipeline.cpp:554:         recordsSinceAnalyze_ %= kAnalyzeRecordThreshold;
src/IndexPipeline.cpp-555-     }
src/IndexPipeline.cpp-556-     RecordMetadataOnlyUpdateLocked();
src/IndexPipeline.cpp-557- }
src/IndexPipeline.cpp-558- 
bench_secondary_filename_index.cpp-101- }
bench_secondary_filename_index.cpp-102- 
bench_secondary_filename_index.cpp-103- bool IsAsciiLower(std::wstring_view value) {
bench_secondary_filename_index.cpp-104-     return std::all_of(value.begin(), value.end(), [](const wchar_t ch) {
bench_secondary_filename_index.cpp-105-         return ch <= 0x7f && !(ch >= L'A' && ch <= L'Z');
bench_secondary_filename_index.cpp-106-     });
bench_secondary_filename_index.cpp-107- }
bench_secondary_filename_index.cpp-108- 
bench_secondary_filename_index.cpp:109: std::u16string FoldAscii(std::wstring_view value) {
bench_secondary_filename_index.cpp-110-     std::u16string result;
bench_secondary_filename_index.cpp-111-     result.reserve(value.size());
bench_secondary_filename_index.cpp-112-     for (const wchar_t ch : value) {
bench_secondary_filename_index.cpp-113-         result.push_back(static_cast<char16_t>(ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch));
bench_secondary_filename_index.cpp-114-     }
bench_secondary_filename_index.cpp-115-     return result;
bench_secondary_filename_index.cpp-116- }
bench_secondary_filename_index.cpp-117- 
bench_secondary_filename_index.cpp-111-     result.reserve(value.size());
bench_secondary_filename_index.cpp-112-     for (const wchar_t ch : value) {
bench_secondary_filename_index.cpp-113-         result.push_back(static_cast<char16_t>(ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch));
bench_secondary_filename_index.cpp-114-     }
bench_secondary_filename_index.cpp-115-     return result;
bench_secondary_filename_index.cpp-116- }
bench_secondary_filename_index.cpp-117- 
bench_secondary_filename_index.cpp-118- std::wstring DirectoryName(size_t index) {
bench_secondary_filename_index.cpp:119:     return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
bench_secondary_filename_index.cpp-120- }
bench_secondary_filename_index.cpp-121- 
bench_secondary_filename_index.cpp-122- std::wstring FileName(size_t index) {
bench_secondary_filename_index.cpp-123-     if (index == 7391) return L"unique_component_7391.txt";
bench_secondary_filename_index.cpp-124-     if (index % 10 == 0 || index % 10 == 1) return L"common_component_" + std::to_wstring(index) + L".txt";
bench_secondary_filename_index.cpp-125-     if (index % 10 == 2) return L"report_" + std::to_wstring(index) + L".txt";
bench_secondary_filename_index.cpp-126-     if (index % 10 == 3) return L"photo_" + std::to_wstring(index) + L".jpg";
bench_secondary_filename_index.cpp-127-     if (index % 10 == 4) return L"archive_" + std::to_wstring(index) + L".zip";
bench_secondary_filename_index.cpp-181-     {"duplicate_names", L"duplicate_name"},
bench_secondary_filename_index.cpp-182-     {"one_char", L"p"},
bench_secondary_filename_index.cpp-183-     {"two_char", L"ph"},
bench_secondary_filename_index.cpp-184-     {"three_char", L"pho"},
bench_secondary_filename_index.cpp-185-     {"medium_literal", L"component_7391"},
bench_secondary_filename_index.cpp-186-     {"long_literal", L"unique_component_7391.txt"},
bench_secondary_filename_index.cpp-187-     {"multi_term_fallback", L"report txt"},
bench_secondary_filename_index.cpp-188-     {"glob_fallback", L"*.txt"},
bench_secondary_filename_index.cpp:189:     {"folder_predicate_fallback", L"folder:folder_1"},
bench_secondary_filename_index.cpp-190-     {"extension_predicate_fallback", L"ext:txt"},
bench_secondary_filename_index.cpp-191-     {"metadata_predicate_fallback", L"size:>1MB"},
bench_secondary_filename_index.cpp-192-     {"kind_predicate_fallback", L"kind:folder"},
bench_secondary_filename_index.cpp-193- }};
bench_secondary_filename_index.cpp-194- 
bench_secondary_filename_index.cpp-195- bool IsEligible(const ffsearch::Query& query) {
bench_secondary_filename_index.cpp-196-     return query.viewCompatible && query.simpleLiteralName && !query.requiresFolder &&
bench_secondary_filename_index.cpp-197-            IsAscii(query.simpleLiteralTerm) && query.simpleLiteralTerm.size() >= 3;
bench_secondary_filename_index.cpp-184-     {"three_char", L"pho"},
bench_secondary_filename_index.cpp-185-     {"medium_literal", L"component_7391"},
bench_secondary_filename_index.cpp-186-     {"long_literal", L"unique_component_7391.txt"},
bench_secondary_filename_index.cpp-187-     {"multi_term_fallback", L"report txt"},
bench_secondary_filename_index.cpp-188-     {"glob_fallback", L"*.txt"},
bench_secondary_filename_index.cpp-189-     {"folder_predicate_fallback", L"folder:folder_1"},
bench_secondary_filename_index.cpp-190-     {"extension_predicate_fallback", L"ext:txt"},
bench_secondary_filename_index.cpp-191-     {"metadata_predicate_fallback", L"size:>1MB"},
bench_secondary_filename_index.cpp:192:     {"kind_predicate_fallback", L"kind:folder"},
bench_secondary_filename_index.cpp-193- }};
bench_secondary_filename_index.cpp-194- 
bench_secondary_filename_index.cpp-195- bool IsEligible(const ffsearch::Query& query) {
bench_secondary_filename_index.cpp-196-     return query.viewCompatible && query.simpleLiteralName && !query.requiresFolder &&
bench_secondary_filename_index.cpp-197-            IsAscii(query.simpleLiteralTerm) && query.simpleLiteralTerm.size() >= 3;
bench_secondary_filename_index.cpp-198- }
bench_secondary_filename_index.cpp-199- 
bench_secondary_filename_index.cpp-200- ffsearch::CandidateView MakeView(const Entry& entry) {
bench_secondary_filename_index.cpp-188-     {"glob_fallback", L"*.txt"},
bench_secondary_filename_index.cpp-189-     {"folder_predicate_fallback", L"folder:folder_1"},
bench_secondary_filename_index.cpp-190-     {"extension_predicate_fallback", L"ext:txt"},
bench_secondary_filename_index.cpp-191-     {"metadata_predicate_fallback", L"size:>1MB"},
bench_secondary_filename_index.cpp-192-     {"kind_predicate_fallback", L"kind:folder"},
bench_secondary_filename_index.cpp-193- }};
bench_secondary_filename_index.cpp-194- 
bench_secondary_filename_index.cpp-195- bool IsEligible(const ffsearch::Query& query) {
bench_secondary_filename_index.cpp:196:     return query.viewCompatible && query.simpleLiteralName && !query.requiresFolder &&
bench_secondary_filename_index.cpp-197-            IsAscii(query.simpleLiteralTerm) && query.simpleLiteralTerm.size() >= 3;
bench_secondary_filename_index.cpp-198- }
bench_secondary_filename_index.cpp-199- 
bench_secondary_filename_index.cpp-200- ffsearch::CandidateView MakeView(const Entry& entry) {
bench_secondary_filename_index.cpp-201-     static_assert(sizeof(wchar_t) == sizeof(char16_t));
bench_secondary_filename_index.cpp-202-     const std::u16string_view name(reinterpret_cast<const char16_t*>(entry.name.data()), entry.name.size());
bench_secondary_filename_index.cpp-203-     return {name, {}, 0, 0, false, 0, 1, entry.id, 0, true, IsAsciiLower(entry.name)};
bench_secondary_filename_index.cpp-204- }
bench_secondary_filename_index.cpp-274-             (void)key;
bench_secondary_filename_index.cpp-275-             std::sort(posting.begin(), posting.end());
bench_secondary_filename_index.cpp-276-             posting.erase(std::unique(posting.begin(), posting.end()), posting.end());
bench_secondary_filename_index.cpp-277-         }
bench_secondary_filename_index.cpp-278-     }
bench_secondary_filename_index.cpp-279- 
bench_secondary_filename_index.cpp-280-     void Add(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-281-         if (!IsAscii(name)) InsertSorted(nonAsciiIds_, id);
bench_secondary_filename_index.cpp:282:         for (const uint64_t gram : UniqueGrams(FoldAscii(name))) {
bench_secondary_filename_index.cpp-283-             InsertSorted(postings_[gram], id);
bench_secondary_filename_index.cpp-284-         }
bench_secondary_filename_index.cpp-285-     }
bench_secondary_filename_index.cpp-286- 
bench_secondary_filename_index.cpp-287-     void Remove(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-288-         if (!IsAscii(name)) EraseSorted(nonAsciiIds_, id);
bench_secondary_filename_index.cpp-289-         for (const uint64_t gram : UniqueGrams(FoldAscii(name))) {
bench_secondary_filename_index.cpp-290-             const auto it = postings_.find(gram);
bench_secondary_filename_index.cpp-281-         if (!IsAscii(name)) InsertSorted(nonAsciiIds_, id);
bench_secondary_filename_index.cpp-282-         for (const uint64_t gram : UniqueGrams(FoldAscii(name))) {
bench_secondary_filename_index.cpp-283-             InsertSorted(postings_[gram], id);
bench_secondary_filename_index.cpp-284-         }
bench_secondary_filename_index.cpp-285-     }
bench_secondary_filename_index.cpp-286- 
bench_secondary_filename_index.cpp-287-     void Remove(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-288-         if (!IsAscii(name)) EraseSorted(nonAsciiIds_, id);
bench_secondary_filename_index.cpp:289:         for (const uint64_t gram : UniqueGrams(FoldAscii(name))) {
bench_secondary_filename_index.cpp-290-             const auto it = postings_.find(gram);
bench_secondary_filename_index.cpp-291-             if (it == postings_.end()) continue;
bench_secondary_filename_index.cpp-292-             EraseSorted(it->second, id);
bench_secondary_filename_index.cpp-293-             if (it->second.empty()) postings_.erase(it);
bench_secondary_filename_index.cpp-294-         }
bench_secondary_filename_index.cpp-295-     }
bench_secondary_filename_index.cpp-296- 
bench_secondary_filename_index.cpp-297-     void Rename(const EntryId id, std::wstring_view oldName, std::wstring_view newName) {
bench_secondary_filename_index.cpp-296- 
bench_secondary_filename_index.cpp-297-     void Rename(const EntryId id, std::wstring_view oldName, std::wstring_view newName) {
bench_secondary_filename_index.cpp-298-         Remove(id, oldName);
bench_secondary_filename_index.cpp-299-         Add(id, newName);
bench_secondary_filename_index.cpp-300-     }
bench_secondary_filename_index.cpp-301- 
bench_secondary_filename_index.cpp-302-     std::vector<EntryId> Candidates(const ffsearch::Query& query, const std::vector<Entry>& entries, bool) const {
bench_secondary_filename_index.cpp-303-         if (!IsEligible(query)) return ActiveIds(entries);
bench_secondary_filename_index.cpp:304:         const std::vector<uint64_t> grams = UniqueGrams(FoldAscii(query.simpleLiteralTerm));
bench_secondary_filename_index.cpp-305-         std::vector<EntryId> result;
bench_secondary_filename_index.cpp-306-         if (!grams.empty()) {
bench_secondary_filename_index.cpp-307-             std::vector<const std::vector<EntryId>*> lists;
bench_secondary_filename_index.cpp-308-             lists.reserve(grams.size());
bench_secondary_filename_index.cpp-309-             for (const uint64_t gram : grams) {
bench_secondary_filename_index.cpp-310-                 const auto it = postings_.find(gram);
bench_secondary_filename_index.cpp-311-                 if (it == postings_.end()) {
bench_secondary_filename_index.cpp-312-                     result.clear();
bench_secondary_filename_index.cpp-364- 
bench_secondary_filename_index.cpp-365-     static void EraseSorted(std::vector<EntryId>& values, const EntryId id) {
bench_secondary_filename_index.cpp-366-         const auto it = std::lower_bound(values.begin(), values.end(), id);
bench_secondary_filename_index.cpp-367-         if (it != values.end() && *it == id) values.erase(it);
bench_secondary_filename_index.cpp-368-     }
bench_secondary_filename_index.cpp-369- 
bench_secondary_filename_index.cpp-370-     void AddBuild(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-371-         if (!IsAscii(name)) nonAsciiIds_.push_back(id);
bench_secondary_filename_index.cpp:372:         for (const uint64_t gram : UniqueGrams(FoldAscii(name))) postings_[gram].push_back(id);
bench_secondary_filename_index.cpp-373-     }
bench_secondary_filename_index.cpp-374- 
bench_secondary_filename_index.cpp-375-     std::unordered_map<uint64_t, std::vector<EntryId>> postings_;
bench_secondary_filename_index.cpp-376-     std::vector<EntryId> nonAsciiIds_;
bench_secondary_filename_index.cpp-377- };
bench_secondary_filename_index.cpp-378- 
bench_secondary_filename_index.cpp-379- class PrefixIndex {
bench_secondary_filename_index.cpp-380- public:
bench_secondary_filename_index.cpp-374- 
bench_secondary_filename_index.cpp-375-     std::unordered_map<uint64_t, std::vector<EntryId>> postings_;
bench_secondary_filename_index.cpp-376-     std::vector<EntryId> nonAsciiIds_;
bench_secondary_filename_index.cpp-377- };
bench_secondary_filename_index.cpp-378- 
bench_secondary_filename_index.cpp-379- class PrefixIndex {
bench_secondary_filename_index.cpp-380- public:
bench_secondary_filename_index.cpp-381-     struct Posting {
bench_secondary_filename_index.cpp:382:         std::u16string folded;
bench_secondary_filename_index.cpp-383-         EntryId id = 0;
bench_secondary_filename_index.cpp-384-     };
bench_secondary_filename_index.cpp-385- 
bench_secondary_filename_index.cpp-386-     void Build(const std::vector<Entry>& entries) {
bench_secondary_filename_index.cpp-387-         postings_.reserve(entries.size());
bench_secondary_filename_index.cpp-388-         for (const Entry& entry : entries) {
bench_secondary_filename_index.cpp-389-             if (entry.active) postings_.push_back({FoldAscii(entry.name), entry.id});
bench_secondary_filename_index.cpp-390-         }
bench_secondary_filename_index.cpp-381-     struct Posting {
bench_secondary_filename_index.cpp-382-         std::u16string folded;
bench_secondary_filename_index.cpp-383-         EntryId id = 0;
bench_secondary_filename_index.cpp-384-     };
bench_secondary_filename_index.cpp-385- 
bench_secondary_filename_index.cpp-386-     void Build(const std::vector<Entry>& entries) {
bench_secondary_filename_index.cpp-387-         postings_.reserve(entries.size());
bench_secondary_filename_index.cpp-388-         for (const Entry& entry : entries) {
bench_secondary_filename_index.cpp:389:             if (entry.active) postings_.push_back({FoldAscii(entry.name), entry.id});
bench_secondary_filename_index.cpp-390-         }
bench_secondary_filename_index.cpp-391-         SortPostings();
bench_secondary_filename_index.cpp-392-     }
bench_secondary_filename_index.cpp-393- 
bench_secondary_filename_index.cpp-394-     void Add(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-395-         Posting posting{FoldAscii(name), id};
bench_secondary_filename_index.cpp-396-         const auto it = LowerBound(posting.folded, id);
bench_secondary_filename_index.cpp-397-         postings_.insert(it, std::move(posting));
bench_secondary_filename_index.cpp-387-         postings_.reserve(entries.size());
bench_secondary_filename_index.cpp-388-         for (const Entry& entry : entries) {
bench_secondary_filename_index.cpp-389-             if (entry.active) postings_.push_back({FoldAscii(entry.name), entry.id});
bench_secondary_filename_index.cpp-390-         }
bench_secondary_filename_index.cpp-391-         SortPostings();
bench_secondary_filename_index.cpp-392-     }
bench_secondary_filename_index.cpp-393- 
bench_secondary_filename_index.cpp-394-     void Add(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp:395:         Posting posting{FoldAscii(name), id};
bench_secondary_filename_index.cpp-396-         const auto it = LowerBound(posting.folded, id);
bench_secondary_filename_index.cpp-397-         postings_.insert(it, std::move(posting));
bench_secondary_filename_index.cpp-398-     }
bench_secondary_filename_index.cpp-399- 
bench_secondary_filename_index.cpp-400-     void Remove(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-401-         const std::u16string folded = FoldAscii(name);
bench_secondary_filename_index.cpp-402-         auto it = LowerBound(folded, id);
bench_secondary_filename_index.cpp-403-         for (; it != postings_.end() && it->folded == folded; ++it) {
bench_secondary_filename_index.cpp-388-         for (const Entry& entry : entries) {
bench_secondary_filename_index.cpp-389-             if (entry.active) postings_.push_back({FoldAscii(entry.name), entry.id});
bench_secondary_filename_index.cpp-390-         }
bench_secondary_filename_index.cpp-391-         SortPostings();
bench_secondary_filename_index.cpp-392-     }
bench_secondary_filename_index.cpp-393- 
bench_secondary_filename_index.cpp-394-     void Add(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-395-         Posting posting{FoldAscii(name), id};
bench_secondary_filename_index.cpp:396:         const auto it = LowerBound(posting.folded, id);
bench_secondary_filename_index.cpp-397-         postings_.insert(it, std::move(posting));
bench_secondary_filename_index.cpp-398-     }
bench_secondary_filename_index.cpp-399- 
bench_secondary_filename_index.cpp-400-     void Remove(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-401-         const std::u16string folded = FoldAscii(name);
bench_secondary_filename_index.cpp-402-         auto it = LowerBound(folded, id);
bench_secondary_filename_index.cpp-403-         for (; it != postings_.end() && it->folded == folded; ++it) {
bench_secondary_filename_index.cpp-404-             if (it->id == id) {
bench_secondary_filename_index.cpp-393- 
bench_secondary_filename_index.cpp-394-     void Add(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-395-         Posting posting{FoldAscii(name), id};
bench_secondary_filename_index.cpp-396-         const auto it = LowerBound(posting.folded, id);
bench_secondary_filename_index.cpp-397-         postings_.insert(it, std::move(posting));
bench_secondary_filename_index.cpp-398-     }
bench_secondary_filename_index.cpp-399- 
bench_secondary_filename_index.cpp-400-     void Remove(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp:401:         const std::u16string folded = FoldAscii(name);
bench_secondary_filename_index.cpp-402-         auto it = LowerBound(folded, id);
bench_secondary_filename_index.cpp-403-         for (; it != postings_.end() && it->folded == folded; ++it) {
bench_secondary_filename_index.cpp-404-             if (it->id == id) {
bench_secondary_filename_index.cpp-405-                 postings_.erase(it);
bench_secondary_filename_index.cpp-406-                 return;
bench_secondary_filename_index.cpp-407-             }
bench_secondary_filename_index.cpp-408-         }
bench_secondary_filename_index.cpp-409-     }
bench_secondary_filename_index.cpp-394-     void Add(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-395-         Posting posting{FoldAscii(name), id};
bench_secondary_filename_index.cpp-396-         const auto it = LowerBound(posting.folded, id);
bench_secondary_filename_index.cpp-397-         postings_.insert(it, std::move(posting));
bench_secondary_filename_index.cpp-398-     }
bench_secondary_filename_index.cpp-399- 
bench_secondary_filename_index.cpp-400-     void Remove(const EntryId id, std::wstring_view name) {
bench_secondary_filename_index.cpp-401-         const std::u16string folded = FoldAscii(name);
bench_secondary_filename_index.cpp:402:         auto it = LowerBound(folded, id);
bench_secondary_filename_index.cpp-403-         for (; it != postings_.end() && it->folded == folded; ++it) {
bench_secondary_filename_index.cpp-404-             if (it->id == id) {
bench_secondary_filename_index.cpp-405-                 postings_.erase(it);
bench_secondary_filename_index.cpp-406-                 return;
bench_secondary_filename_index.cpp-407-             }
bench_secondary_filename_index.cpp-408-         }
bench_secondary_filename_index.cpp-409-     }
bench_secondary_filename_index.cpp-410- 
[15 matches limit reached. Use limit=30 for more, or refine pattern]
Now let me verify the FTS5 trigram tokenizer's case-folding semantics in the vendored amalgamation (critical for the CompareStringOrdinal superset question):
read: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
  );
}
/**************************************************************************
** Start of trigram implementation.
*/
typedef struct TrigramTokenizer TrigramTokenizer;
struct TrigramTokenizer {
  int bFold;                      /* True to fold to lower-case */
  int iFoldParam;                 /* Parameter to pass to Fts5UnicodeFold() */
};
/*
** Free a trigram tokenizer.
*/
static void fts5TriDelete(Fts5Tokenizer *p){
  sqlite3_free(p);
}
/*
** Allocate a trigram tokenizer.
*/
static int fts5TriCreate(
  void *pUnused,
  const char **azArg,
  int nArg,
  Fts5Tokenizer **ppOut
){
  int rc = SQLITE_OK;
  TrigramTokenizer *pNew = 0;
  UNUSED_PARAM(pUnused);
  if( nArg%2 ){
    rc = SQLITE_ERROR;
  }else{
    int i;
    pNew = (TrigramTokenizer*)sqlite3_malloc64(sizeof(*pNew));
    if( pNew==0 ){
      rc = SQLITE_NOMEM;
    }else{
      pNew->bFold = 1;
      pNew->iFoldParam = 0;
      for(i=0; rc==SQLITE_OK && i<nArg; i+=2){
        const char *zArg = azArg[i+1];
        if( 0==sqlite3_stricmp(azArg[i], "case_sensitive") ){
          if( (zArg[0]!='0' && zArg[0]!='1') || zArg[1] ){
            rc = SQLITE_ERROR;
          }else{
            pNew->bFold = (zArg[0]=='0');
          }
        }else if( 0==sqlite3_stricmp(azArg[i], "remove_diacritics") ){
          if( (zArg[0]!='0' && zArg[0]!='1' && zArg[0]!='2') || zArg[1] ){
            rc = SQLITE_ERROR;
          }else{
            pNew->iFoldParam = (zArg[0]!='0') ? 2 : 0;
          }
        }else{
          rc = SQLITE_ERROR;
        }
      }
      if( pNew->iFoldParam!=0 && pNew->bFold==0 ){
        rc = SQLITE_ERROR;
      }
      if( rc!=SQLITE_OK ){
        fts5TriDelete((Fts5Tokenizer*)pNew);
        pNew = 0;
      }
    }
  }
  *ppOut = (Fts5Tokenizer*)pNew;
  return rc;
}
/*
** Trigram tokenizer tokenize routine.
*/
static int fts5TriTokenize(
  Fts5Tokenizer *pTok,
  void *pCtx,
  int unusedFlags,
  const char *pText, int nText,
  int (*xToken)(void*, int, const char*, int, int, int)
){
  TrigramTokenizer *p = (TrigramTokenizer*)pTok;
  int rc = SQLITE_OK;
  char aBuf[32];
  char *zOut = aBuf;
  int ii;
  const unsigned char *zIn = (const unsigned char*)pText;
  const unsigned char *zEof = (zIn ? &zIn[nText] : 0);
  u32 iCode = 0;
  int aStart[3];                  /* Input offset of each character in aBuf[] */
  UNUSED_PARAM(unusedFlags);
  /* Populate aBuf[] with the characters for the first trigram. */
  for(ii=0; ii<3; ii++){
    do {
      aStart[ii] = zIn - (const unsigned char*)pText;
      if( zIn>=zEof ) return SQLITE_OK;
      READ_UTF8(zIn, zEof, iCode);
      if( p->bFold ) iCode = sqlite3Fts5UnicodeFold(iCode, p->iFoldParam);
    }while( iCode==0 );
    WRITE_UTF8(zOut, iCode);
  }
  /* At the start of each iteration of this loop:
  **
  **  aBuf:      Contains 3 characters. The 3 characters of the next trigram.
  **  zOut:      Points to the byte following the last character in aBuf.
  **  aStart[3]: Contains the byte offset in the input text corresponding
  **             to the start of each of the three characters in the buffer.
  */
  assert( zIn<=zEof );
  while( 1 ){
    int iNext;                    /* Start of character following current tri */
    const char *z1;
[2413 more lines in file. Use offset=267238 to continue.]
Let me pin exact line numbers for citations and check the checkpoint polling cadence:
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine
grep: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
Store.cpp:441: bool Store::CheckpointPassive() {
Store.cpp:445:     return sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr) == SQLITE_OK;
Store.cpp:448: bool Store::CheckpointIfWalExceeds(uint64_t thresholdBytes) {
Store.cpp:453:     if (sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, &frameCount, nullptr) != SQLITE_OK) {
Store.cpp:476:         return sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_RESTART, nullptr, nullptr) == SQLITE_OK;
Store.cpp:717:     if (!ExecSql(db_, "BEGIN IMMEDIATE;")) {
Store.cpp:753:         if (!ExecSql(db_, "COMMIT;")) {
Store.cpp:771:     if (!ExecSql(db_, "BEGIN IMMEDIATE;")) {
Store.cpp:869:     if (!ExecSql(db_, "COMMIT;")) {
Store.cpp:1230:         "AND (name LIKE ? ESCAPE '\\' OR name GLOB '*[^ -~]*');");
src/IndexPipeline.cpp-523-     if (!store_.IsOpen()) {
src/IndexPipeline.cpp-524-         return;
src/IndexPipeline.cpp-525-     }
src/IndexPipeline.cpp:526:     store_.CheckpointPassive();
src/IndexPipeline.cpp-527-     store_.CheckpointIfWalExceeds(kWalForceCheckpointThresholdBytes);
src/IndexPipeline.cpp-528-     if (userIdle) {
src/IndexPipeline.cpp-529-         const uint64_t nowEpochSeconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
src/IndexPipeline.cpp-524-         return;
src/IndexPipeline.cpp-525-     }
src/IndexPipeline.cpp-526-     store_.CheckpointPassive();
src/IndexPipeline.cpp:527:     store_.CheckpointIfWalExceeds(kWalForceCheckpointThresholdBytes);
src/IndexPipeline.cpp-528-     if (userIdle) {
src/IndexPipeline.cpp-529-         const uint64_t nowEpochSeconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
src/IndexPipeline.cpp-530-             std::chrono::system_clock::now().time_since_epoch()).count());
sqlite3.c:26: #define SQLITE_CORE 1
sqlite3.c:194232: # define SQLITE_CORE 1
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine
read: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
[pi-lens: read widened to the enclosing method "IndexPipeline::GetReadPlaneDiagnostics" (symbol boundary): you asked for lines 512-529, this shows lines 491-529. Re-request with limit > 100 for the exact range.]
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
    result.generationRestarts = generationRestarts_.load(std::memory_order_relaxed);
    result.queryVisibleEpoch = queryVisibleEpoch_.load(std::memory_order_acquire);
    result.dependencySetEpoch = dependencySetEpoch_.load(std::memory_order_acquire);
    result.queryVisibleInvalidations = queryVisibleInvalidations_.load(std::memory_order_relaxed);
    result.metadataOnlyUpdates = metadataOnlyUpdates_.load(std::memory_order_relaxed);
    result.metadataOnlyInvalidationsAvoided = metadataOnlyInvalidationsAvoided_.load(std::memory_order_relaxed);
    result.durableFallbacks = durableFallbacks_.load(std::memory_order_relaxed);
    result.successfulDurableFallbacks = successfulDurableFallbacks_.load(std::memory_order_relaxed);
    result.failedDurableFallbacks = failedDurableFallbacks_.load(std::memory_order_relaxed);
    result.durableFallbackRowsStreamed = durableFallbackRowsStreamed_.load(std::memory_order_relaxed);
    return result;
}
void IndexPipeline::SetBulkScanActive(const bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    projection_.SetBulkScanActive(active);
    RecordMetadataOnlyUpdateLocked();
}
void IndexPipeline::RunStoreMaintenance(bool userIdle, uint64_t vacuumIntervalHours,
                                         uint64_t vacuumSizeGrowthPercent) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!store_.IsOpen()) {
        return;
    }
    store_.CheckpointPassive();
    store_.CheckpointIfWalExceeds(kWalForceCheckpointThresholdBytes);
    if (userIdle) {
        const uint64_t nowEpochSeconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
[1675 more lines in file. Use offset=530 to continue.]
No matches found
** implement the programmer interface to the library.  Routines in
** other files are for internal use by SQLite and should not be
** accessed by users of the library.
*/
/* #include "sqliteInt.h" */
#ifdef SQLITE_ENABLE_FTS3
/************** Include fts3.h in the middle of main.c ***********************/
/************** Begin file fts3.h ********************************************/
/*
** 2006 Oct 10
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
[82345 more lines in file. Use offset=187306 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine
src/IndexPipeline.cpp-516-     projection_.SetBulkScanActive(active);
src/IndexPipeline.cpp-517-     RecordMetadataOnlyUpdateLocked();
src/IndexPipeline.cpp-518- }
src/IndexPipeline.cpp-519- 
src/IndexPipeline.cpp:520: void IndexPipeline::RunStoreMaintenance(bool userIdle, uint64_t vacuumIntervalHours,
src/IndexPipeline.cpp-521-                                          uint64_t vacuumSizeGrowthPercent) {
src/IndexPipeline.cpp-522-     std::lock_guard<std::mutex> lock(mutex_);
src/IndexPipeline.cpp-523-     if (!store_.IsOpen()) {
src/IndexPipeline.cpp-524-         return;
src/Main.cpp-335-         [&indexPipeline] {
src/Main.cpp-336-             const auto settings = ffprotocol::LoadSettings(false);
src/Main.cpp-337-             // address-engineering-audit-findings 5.2: the idle callback is
src/Main.cpp-338-             // the only production path allowed to attempt expensive VACUUM.
src/Main.cpp:339:             indexPipeline.RunStoreMaintenance(true, settings.vacuumIntervalHours,
src/Main.cpp-340-                                                settings.vacuumSizeGrowthPercent);
src/Main.cpp-341-         },
src/Main.cpp-342-         [&privilegedConnection] { privilegedConnection.DropForIdle(); },
src/Main.cpp-343-         [&privilegedConnection] { privilegedConnection.RequestReconnect(); });
src/IndexPipeline.h-200-     // ReconciliationSchedulerLoop poll). Runs a cheap passive checkpoint
src/IndexPipeline.h-201-     // every call and escalates to a forced checkpoint once the WAL has
src/IndexPipeline.h-202-     // grown past the threshold (design.md "Risks": unbounded WAL growth
src/IndexPipeline.h-203-     // under sustained write bursts). No-op if the store was never opened.
src/IndexPipeline.h:204:     void RunStoreMaintenance(bool userIdle = false, uint64_t vacuumIntervalHours = 24,
src/IndexPipeline.h-205-                              uint64_t vacuumSizeGrowthPercent = 10);
src/IndexPipeline.h-206- 
src/IndexPipeline.h-207-     // task 3.1/3.2: rebuilds every volume already known to the durable
src/IndexPipeline.h-208-     // store, invoking onVolumeRebuilt once per volume whose rebuild and rule
src/VolumeSessionManager.cpp-1426-         // forced checkpoint once the WAL grows past the threshold
src/VolumeSessionManager.cpp-1427-         // (design.md "Risks": unbounded WAL growth). Runs regardless of
src/VolumeSessionManager.cpp-1428-         // the privileged connection's state: it's purely local
src/VolumeSessionManager.cpp-1429-         // maintenance of the engine's own database file.
src/VolumeSessionManager.cpp:1430:         pipeline_.RunStoreMaintenance();
src/VolumeSessionManager.cpp-1431- 
src/VolumeSessionManager.cpp-1432-         // tasks.md 8.5: never schedule privileged-path reconciliation
src/VolumeSessionManager.cpp-1433-         // while degraded -- there is no connection to send it over, and
src/VolumeSessionManager.cpp-1434-         // no ephemeral VolumeIds to address volumes with even if there
Two final verification details — `dbstat` availability in this amalgamation and the bench CLI flags:
grep: C:/Users/Michael Roy/Documents/FastFiles/third_party/sqli...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
sqlite3.c:22898: #if defined(SQLITE_ENABLE_DBSTAT_VTAB) || defined(SQLITE_TEST)
sqlite3.c:23271: #ifdef SQLITE_ENABLE_DBSTAT_VTAB
sqlite3.c:187444: #ifdef SQLITE_ENABLE_DBSTAT_VTAB
sqlite3.c:231803: #if (defined(SQLITE_ENABLE_DBSTAT_VTAB) || defined(SQLITE_TEST)) \
sqlite3.c:232682: #elif defined(SQLITE_ENABLE_DBSTAT_VTAB)
sqlite3.c:232684: #endif /* SQLITE_ENABLE_DBSTAT_VTAB */
[6 matches limit reached. Use limit=12 for more, or refine pattern]
bench_large_index_scalability.cpp-205-                          argv[0], argv[0]);
bench_large_index_scalability.cpp-206-             return false;
bench_large_index_scalability.cpp-207-         }
bench_large_index_scalability.cpp:208:         if (argument == L"--scales" && index + 1 < argc) {
bench_large_index_scalability.cpp-209-             options.scales = ParseScales(argv[++index]);
bench_large_index_scalability.cpp-210-             if (options.scales.empty()) {
bench_large_index_scalability.cpp-211-                 std::fwprintf(stderr, L"Invalid --scales value\n");
bench_large_index_scalability.cpp-213-             }
bench_large_index_scalability.cpp-214-             continue;
bench_large_index_scalability.cpp-215-         }
bench_large_index_scalability.cpp:216:         if (argument == L"--output" && index + 1 < argc) {
bench_large_index_scalability.cpp-217-             options.outputPath = argv[++index];
bench_large_index_scalability.cpp-218-             continue;
bench_large_index_scalability.cpp-219-         }
bench_large_index_scalability.cpp-217-             options.outputPath = argv[++index];
bench_large_index_scalability.cpp-218-             continue;
bench_large_index_scalability.cpp-219-         }
bench_large_index_scalability.cpp:220:         if (argument == L"--diagnostics") {
bench_large_index_scalability.cpp-221-             options.detailedDiagnostics = true;
bench_large_index_scalability.cpp-222-             continue;
bench_large_index_scalability.cpp-223-         }
bench_large_index_scalability.cpp-230-             options.sourceState = ToUtf8(argv[++index]);
bench_large_index_scalability.cpp-231-             continue;
bench_large_index_scalability.cpp-232-         }
bench_large_index_scalability.cpp:233:         if (argument == L"--source-sha" && index + 1 < argc) {
bench_large_index_scalability.cpp-234-             options.sourceSha = ToUtf8(argv[++index]);
bench_large_index_scalability.cpp-235-             continue;
bench_large_index_scalability.cpp-236-         }
bench_secondary_filename_index.cpp-697-     return result;
bench_secondary_filename_index.cpp-698- }
bench_secondary_filename_index.cpp-699- 
bench_secondary_filename_index.cpp:700: bool ParseOptions(int argc, wchar_t** argv, Options& options) {
bench_secondary_filename_index.cpp-701-     for (int index = 1; index < argc; ++index) {
bench_secondary_filename_index.cpp-702-         const std::wstring argument = argv[index];
bench_secondary_filename_index.cpp-703-         if (argument == L"--scales" && index + 1 < argc) {
bench_secondary_filename_index.cpp-699- 
bench_secondary_filename_index.cpp-700- bool ParseOptions(int argc, wchar_t** argv, Options& options) {
bench_secondary_filename_index.cpp-701-     for (int index = 1; index < argc; ++index) {
bench_secondary_filename_index.cpp:702:         const std::wstring argument = argv[index];
bench_secondary_filename_index.cpp-703-         if (argument == L"--scales" && index + 1 < argc) {
bench_secondary_filename_index.cpp-704-             options.scales = ParseScales(argv[++index]);
bench_secondary_filename_index.cpp-705-             if (options.scales.empty()) return false;
bench_secondary_filename_index.cpp-701-     for (int index = 1; index < argc; ++index) {
bench_secondary_filename_index.cpp-702-         const std::wstring argument = argv[index];
bench_secondary_filename_index.cpp-703-         if (argument == L"--scales" && index + 1 < argc) {
bench_secondary_filename_index.cpp:704:             options.scales = ParseScales(argv[++index]);
bench_secondary_filename_index.cpp-705-             if (options.scales.empty()) return false;
bench_secondary_filename_index.cpp-706-         } else if (argument == L"--output" && index + 1 < argc) {
bench_secondary_filename_index.cpp-707-             options.outputPath = ToUtf8(argv[++index]);
bench_secondary_filename_index.cpp-703-         if (argument == L"--scales" && index + 1 < argc) {
bench_secondary_filename_index.cpp-704-             options.scales = ParseScales(argv[++index]);
bench_secondary_filename_index.cpp-705-             if (options.scales.empty()) return false;
bench_secondary_filename_index.cpp:706:         } else if (argument == L"--output" && index + 1 < argc) {
bench_secondary_filename_index.cpp-707-             options.outputPath = ToUtf8(argv[++index]);
bench_secondary_filename_index.cpp-708-         } else if (argument == L"--help") {
bench_secondary_filename_index.cpp-709-             std::wprintf(L"Usage: %s [--scales 100000,1000000] [--output path]\n", argv[0]);
bench_secondary_filename_index.cpp-704-             options.scales = ParseScales(argv[++index]);
bench_secondary_filename_index.cpp-705-             if (options.scales.empty()) return false;
bench_secondary_filename_index.cpp-706-         } else if (argument == L"--output" && index + 1 < argc) {
bench_secondary_filename_index.cpp:707:             options.outputPath = ToUtf8(argv[++index]);
bench_secondary_filename_index.cpp-708-         } else if (argument == L"--help") {
bench_secondary_filename_index.cpp-709-             std::wprintf(L"Usage: %s [--scales 100000,1000000] [--output path]\n", argv[0]);
bench_secondary_filename_index.cpp-710-             return false;
bench_secondary_filename_index.cpp-706-         } else if (argument == L"--output" && index + 1 < argc) {
bench_secondary_filename_index.cpp-707-             options.outputPath = ToUtf8(argv[++index]);
bench_secondary_filename_index.cpp-708-         } else if (argument == L"--help") {
bench_secondary_filename_index.cpp:709:             std::wprintf(L"Usage: %s [--scales 100000,1000000] [--output path]\n", argv[0]);
bench_secondary_filename_index.cpp-710-             return false;
bench_secondary_filename_index.cpp-711-         } else {
bench_secondary_filename_index.cpp-712-             return false;
bench_secondary_filename_index.cpp-796- 
bench_secondary_filename_index.cpp-797- } // namespace
bench_secondary_filename_index.cpp-798- 
bench_secondary_filename_index.cpp:799: int wmain(int argc, wchar_t** argv) {
bench_secondary_filename_index.cpp-800-     Options options;
bench_secondary_filename_index.cpp-801-     if (!ParseOptions(argc, argv, options)) return 2;
bench_secondary_filename_index.cpp-802-     const FtsCapability fts = MeasureFtsCapability();
bench_secondary_filename_index.cpp-798- 
bench_secondary_filename_index.cpp-799- int wmain(int argc, wchar_t** argv) {
bench_secondary_filename_index.cpp-800-     Options options;
bench_secondary_filename_index.cpp:801:     if (!ParseOptions(argc, argv, options)) return 2;
bench_secondary_filename_index.cpp-802-     const FtsCapability fts = MeasureFtsCapability();
bench_secondary_filename_index.cpp-803-     std::vector<std::vector<ArchitectureMeasurement>> allScales;
bench_secondary_filename_index.cpp-804-     allScales.reserve(options.scales.size());
[8 matches limit reached. Use limit=16 for more, or refine pattern]
All evidence is gathered and verified. Here is the research report.
## Review (research lane A: SQLite storage + FTS)
### 1. SQLite build configuration
**Target:** `third_party/sqlite/CMakeLists.txt` builds the vendored amalgamation as static lib `sqlite3` (`add_library(sqlite3 STATIC sqlite3.c)` at line 5). Amalgamation is **SQLite 3.53.4** (`third_party/sqlite/sqlite3.h:149`, source id 2026-07-24).
**Compile definitions** (`third_party/sqlite/CMakeLists.txt:10-16`, `PUBLIC` so consumers see them too):
- `SQLITE_THREADSAFE=1` (serialized mode)
- `SQLITE_DEFAULT_WAL_SYNCHRONOUS=1`
- `SQLITE_OMIT_DEPRECATED`
- `SQLITE_OMIT_LOAD_EXTENSION`
- `SQLITE_ENABLE_FTS3=0`
**What is and is not compiled in:**
- **FTS5: NOT enabled.** No `SQLITE_ENABLE_FTS5` anywhere in the build; the module is behind `#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS5)` (`sqlite3.c:241334`, ends `sqlite3.c:269293`) and the amalgamation self-defines `SQLITE_CORE 1` (`sqlite3.c:26`). Runtime proof already captured: `docs/benchmarks/secondary-filename-index.json` → `"compile_option_enable_fts5":false,"fts5_create":false,"create_error":"no such module: fts5"`.
- **Trigram tokenizer: present in source but not compiled.** It lives inside the FTS5 module (`sqlite3.c:267122` "Start of trigram implementation", `fts5TriCreate`/`fts5TriTokenize` at `sqlite3.c:267138-267260`). Same runtime proof: `"trigram_create":false`.
- **ICU: not enabled.** No `SQLITE_ENABLE_ICU`; no ICU code path. Case folding is SQLite's built-in tables only.
- **dbstat: not compiled** (`sqlite3.c:22898`, `23271` — gated on `SQLITE_ENABLE_DBSTAT_VTAB`), so per-table page accounting SQL needs a probe build with that flag.
- **No other wrapper redefines anything** — repo-wide grep for `SQLITE_ENABLE`/`SQLITE_OMIT` hits only this CMakeLists and the amalgamation/tests.
**Finding P2 — `SQLITE_ENABLE_FTS3=0` does the opposite of its apparent intent.** The amalgamation gates FTS3/FTS4 exclusively on `#ifdef`/`defined()` checks (e.g. `sqlite3.c:23280`, `193292`, `187296`), never on the macro's *value*. A `/DSQLITE_ENABLE_FTS3=0` definition makes `defined(SQLITE_ENABLE_FTS3)` true, so **FTS3/FTS4 is compiled in and registered** (whole module `sqlite3.c:187296-202036`) despite the `=0` that reads like "disabled". Nothing undefs it except `SQLITE_OMIT_VIRTUALTABLE` (`sqlite3.c:21671-21674`), which is not set. Smallest fix: delete the line (or replace with `SQLITE_OMIT_FTS3`); if FTS3/4 is wanted, keep it and drop the misleading `=0`. A human can confirm with `SELECT sqlite3_compileoption_used('ENABLE_FTS3');` (expect `1`) and `CREATE VIRTUAL TABLE t USING fts3(x);` (expect success) on the current build.
### 2. Durable schema (`src/indexstore/src/Store.cpp`)
**Tables** (`kSchemaSql`, `Store.cpp:24-55`):
- `volumes` (`Store.cpp:25-36`): `id INTEGER PRIMARY KEY AUTOINCREMENT`, `volume_guid BLOB`, `serial_number`, `available`, `last_seen_time`, `journal_id`, `resume_usn`, `scan_cursor BLOB`, `scan_complete`, `last_reconciliation_time`, `UNIQUE(volume_guid, serial_number)`. Note: `VolumeMetadata::entryCount` is *not* a column — it's computed by `CountEntries` (`Store.cpp:1000-1013`, `SELECT COUNT(*) FROM entries WHERE volume_id=?`).
- `entries` (`Store.cpp:38-54`): **`WITHOUT ROWID`**, `PRIMARY KEY (volume_id, frn_low, frn_high)` — clustered on the PK, keyed by (volume, FRN) split into low/high u64 (`Identity.h`). Columns: `parent_frn_low/high`, `name TEXT NOT NULL` (UTF-8), `size_bytes`, `allocated_size_bytes` (**nullable** = unknown, `Store.cpp:44-45`), `creation_time`, `last_modified_time`, `last_access_time`, `attributes`.
- **Exactly one secondary index:** `idx_entries_parent ON entries(volume_id, parent_frn_low, parent_frn_high)` (`Store.cpp:55`). **No index of any kind on `name`.**
**Schema versioning** (`Store.cpp:16-20`, `380-405`): `PRAGMA user_version`; `kCurrentSchemaVersion = 2`. Fresh DB stamped 2; v1 → v2 is an in-place metadata-only `ALTER TABLE entries ADD COLUMN allocated_size_bytes INTEGER` (`kMigrateV1ToV2Sql`, `Store.cpp:57-59`); newer-than-understood versions refused (fail closed). Note ordering nuance: `kSchemaSql` (`CREATE TABLE IF NOT EXISTS`) runs *before* the version check (`Store.cpp:374`), so DDL and version logic are not one atomic migration.
**WAL / transaction boundaries:**
- Open: `PRAGMA journal_mode=WAL; synchronous=NORMAL; foreign_keys=ON` (`Store.cpp:369-370`), preceded by a full `PRAGMA integrity_check` with Corrupt/Inconclusive classification (`Store.cpp:312-338`, `ClassifyIntegrityStep` `Store.cpp:296-309`).
- **`ApplyBatch` = exactly one `BEGIN IMMEDIATE … COMMIT` per batch** (`Store.cpp:763-874`; BEGIN at `771`, COMMIT at `869`, rollback on any row failure or failed commit). The upsert is a 21-parameter `INSERT … ON CONFLICT(volume_id,frn_low,frn_high) DO UPDATE` with per-field `CASE WHEN (? & mask)` partial-update arms (`Store.cpp:776-797`); deletes are `DELETE FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?` (`Store.cpp:798`). Both statements are prepared **once per ApplyBatch call** and reused per row via `sqlite3_reset` (`Store.cpp:799-857`) — there is no cross-call statement cache (`Statement` RAII helper `Store.cpp:194-206` re-prepares on every construction).
- Checkpoints: `CheckpointPassive` (`Store.cpp:441-446`) + `CheckpointIfWalExceeds` (`Store.cpp:448-477`) — size-triggered **RESTART** checkpoint when frames×page_size > 16 MiB (`kWalForceCheckpointThresholdBytes = 4000×4096`, `IndexPipeline.cpp:333`), driven by `IndexPipeline::RunStoreMaintenance` (`IndexPipeline.cpp:520-527`) from the reconciliation poll and idle callback (`VolumeSessionManager.cpp:1430`, `Main.cpp:339`). `ANALYZE` runs after every 10,000 ingested records (`kAnalyzeRecordThreshold`, `IndexPipeline.h:460`, `IndexPipeline.cpp:536-557`). Idle-gated `VACUUM` (`Store.cpp:493-518`).
**Cold-scan prepared statements:**
- `StoreReadSession::ForEachEntryWithAsciiNameUntil` (`Store.cpp:1210-1252`) executes:
  `SELECT <11 columns> FROM entries WHERE volume_id=? AND (name LIKE ? ESCAPE '\' OR name GLOB '*[^ -~]*');` (`Store.cpp:1228-1230`), pattern = `%needle%` with LIKE metacharacters escaped (`LikePatternForLiteralNeedle`, `Store.cpp:1200-1209`). Contract: ASCII-folded literal needle ≥ 3 UTF-16 units only (`ColdNamePrefilterNeedle`, `IndexPipeline.cpp:372-387`); result is a proven conservative superset — LIKE folds ASCII exactly, the GLOB disjunct admits every name containing non-ASCII bytes (host-fold safety), canonical matcher verifies survivors (`Store.h:255-265`).
- `ForEachEntryUntil` (`Store.cpp:1170-1207`) and `Store::ForEachEntry` (`Store.cpp:878-914`): plain `SELECT <11 cols> FROM entries WHERE volume_id=?`, no name predicate — used for prefilter-ineligible queries (non-ASCII needle, <3 units, multi-term/extension/glob classes).
- `FindEntry` (`Store.cpp:946-977`, session variant `Store.cpp:1254-1281`): PK point lookup.
- `GetFolderAggregate` (`Store.cpp:1024-1090` / `QueryFolderAggregate` `Store.cpp:240-283`): recursive CTE over `idx_entries_parent`.
- Read sessions (`StoreReadSession::Open`, `Store.cpp:1121-1150`): separate `SQLITE_OPEN_READONLY` connection, `query_only=ON`, `busy_timeout=250`, deferred `BEGIN` with a probe SELECT to pin the WAL snapshot before releasing the pipeline mutex.
### 3. Query plans and the ~1.05–1.10 µs/row cost
**SELECT shape:** the cold name scan is the `LIKE OR GLOB` query above. Because `entries` is `WITHOUT ROWID` clustered on `(volume_id, frn_low, frn_high)`, `volume_id=?` is a PK-prefix **range scan of the entire volume in PK order**; `EXPLAIN QUERY PLAN` will read `SCAN entries` (no seek to a name term). `LIKE '%…%'` is an infix contains — **no B-tree can serve it**; the `GLOB '*[^ -~]*'` disjunct is a per-row character-class match that no index can serve either. So every query — even a zero-survivor literal — visits and decodes every row of the volume.
**Why ~1.05–1.10 µs/row** (measured floor, `large-index-lifecycle-and-search-scalability/evidence/memory-cold-decision.md` §2): two-point subtraction on the no_match pair gives **scan floor ~1.05–1.10 µs/scanned-row** (consistent at 200k and 1M) plus **~0.35 µs/row survivor decode**; single sample per cell, ±10% noise, and — per the lane E-6 correction — the `storeStreamMs` timer wraps stream+decode+match together, so the per-component attribution is *not* instrumented. Per-row work actually performed: clustered leaf traversal of a wide 11-column row; `name` UTF-8→UTF-16 conversion with a per-row `std::u16string` allocation (`ReadEntryRecord`, `Store.cpp:221-237` + `Utf8ToUtf16` `Store.cpp:107-165`); SQLite-side LIKE-with-ESCAPE and GLOB evaluation on every row; `EntryRecord` → `MakeDurableCandidate` construction; `std::function` visitor dispatch (`IndexPipeline.cpp:1886-1908`). A note for the campaign: the surviving 98,438 rows at 1M were exactly the fixture's 9.8% non-ASCII names admitted by the GLOB disjunct.
**Could an index serve it?** Only an inverted/trigram structure (rows-visited reduction). A B-tree name index cannot (infix + ASCII-fold semantics). A covering `(volume_id, name)` index would cut row-decode I/O but stays O(N) and duplicates name bytes — it addresses the constant, not the floor. The real fix is candidate generation before decode: the in-memory filename-trigram accelerator already achieves 56–189× on hot eligible literals; the cold path has no durable equivalent (that gap is exactly `durable-cold-search-index-and-hybrid-residency`).
### 4. Write amplification + WAL of current ingestion
- **Transaction boundary = one protocol batch.** One `ScanRecordBatch`/`V2` frame (≤ `kMaxFrameSize` 1 MiB, `Frame.h:19`) → one `scanBatchCallback_` (`PrivilegedConnection.cpp:384-443`) → one `pipeline_.ApplyMftBatch` (`VolumeSessionManager.cpp:849-864`) → one `store_.ApplyBatch` → one `BEGIN IMMEDIATE…COMMIT`. So roughly tens-to-hundreds of MFT records per commit.
- Per batch, each upsert dirties the clustered `entries` leaf **plus** the `idx_entries_parent` leaf. Initial-scan order is MFT order, not FRN order → semi-random PK insertions → page splits and scattered dirty pages; worst case per batch approaches R rows × (table page + index page) of WAL frames. Commits are cheap (WAL + `synchronous=NORMAL` = no per-commit fsync); WAL growth is bounded by the passive/restart checkpoint regime (16 MiB trigger) at the cost of a RESTART checkpoint writer stall when exceeded. `ANALYZE` every 10k rows adds a small `sqlite_stat1` transaction. Crash tolerance is deliberate: NORMAL may lose recent transactions, which resumable scan-cursor + journal-resume reconcile (`Store.cpp:363-367` comment, `VolumeMetadata.scanCursor`/`resumeUsn`).
- Statement re-prepare per `ApplyBatch` call is negligible per-row; the dominant per-row write cost is the two B-tree insert positions + UTF-16→UTF-8 name conversion.
### 5. FTS5 candidate-structure design sketch for this schema
**Build prerequisite:** add `SQLITE_ENABLE_FTS5` to `third_party/sqlite/CMakeLists.txt` (trigram comes with it; 3.53.4 also has `contentless_delete=1`). Optionally add `SQLITE_ENABLE_DBSTAT_VTAB` for measurement builds.
**External-content vs standalone — external content is effectively blocked.** `entries` is `WITHOUT ROWID` (no rowid), and FTS5 external content needs a single integer `content_rowid` identity; the PK here is a triple ending in a 128-bit FRN. Using it would require converting `entries` to a rowid table or adding a surrogate unique key — both table rewrites, i.e. the exact highest-risk migration the proposal wants to avoid. **Recommended: a standalone FTS5 table with UNINDEXED key columns, in the same database:**
```sql
CREATE VIRTUAL TABLE entry_name_terms USING fts5(
    name_folded,                  -- ASCII-lowercased UTF-8 copy of name
    volume_id UNINDEXED, frn_low UNINDEXED, frn_high UNINDEXED,
    tokenize="trigram case_sensitive 1");
```
Maintained **inside the existing `ApplyBatch` transaction** (same DB, same `BEGIN IMMEDIATE`) → crash consistency is free: the FTS table can never disagree with `entries` because they commit atomically; recovery fallback = `DROP` + rebuild-on-open; rollback to v2 = `user_version` gate ignores the table. Candidate query:
```sql
SELECT volume_id, frn_low, frn_high FROM entry_name_terms
WHERE entry_name_terms MATCH '"' || :needle_escaped || '"'
  AND volume_id = :vol;   -- then canonical CompareStringOrdinal verify on fetched rows
```
(Contentless `content=''` + mapping table saves the name copy but loses UNINDEXED columns and needs delete-command bookkeeping — more moving parts for modest savings.)
**Trigram vs unicode61:** unicode61 tokenizes on non-alphanumerics — useless for infix matching of filename strings like `report.final.v2` and unsuited to substring semantics (`bench_secondary_filename_index.cpp:851` already rejected a prefix architecture because "current_literal_search_is_infix"). Trigram is the only FTS5 tokenizer matching the product's substring contract. **But the trigram tokenizer's folding is a trap:** with `case_sensitive=0` it folds codepoints via `sqlite3Fts5UnicodeFold` — a **full Unicode lowercase fold** (`sqlite3.c:267194-267230`, `bFold`/`iFoldParam`), which matches *neither* SQLite LIKE's ASCII fold *nor* Windows `CompareStringOrdinal(..., TRUE)` (OS uppercase-table, per-UTF-16-unit, handles surrogates; e.g. U+017F ſ→'S', U+212A K→'K' fold-equal ASCII on host tables — precisely the P2-A "fold-exceptional" class). Hence the sketch stores a **pre-ASCII-folded column with `case_sensitive=1`** (no fold inside FTS), queried with an escaped literal — folding semantics then equal the current `ForEachEntryWithAsciiNameUntil` proof exactly.
**Superset safety vs `CompareStringOrdinal` (the hard requirement):** candidates = FTS hits **∪ exception side-set**, where the side-set unconditionally contains every name carrying (a) non-ASCII UTF-16 units (today's GLOB disjunct), (b) host fold-exceptional units (`Projection::SetTestingFoldExceptionalUnit`, memoized host probe, fail-closed), (c) surrogate/lone-surrogate units (UTF-8 codepoint trigrams ≠ UTF-16-unit ordinal matching). This mirrors the in-memory accelerator's folded-postings + exception-set design (`bench_secondary_filename_index.cpp:280-295`, `nonAsciiIds_`). The canonical matcher stays the sole authority; FTS only generates candidates — false positives are free, false negatives are contract breaks.
**Prefix/substring eligibility:** needle ≥ 3 UTF-16 units and ASCII literal (same gate as `ColdNamePrefilterNeedle`) — infix substring and prefix both expressible as trigram phrases (a prefix match is a superset constrained by its first trigrams); <3-unit needles, wildcard/regex/extension/multi-term classes, and non-ASCII needles are **ineligible** → keep the current full stream (non-ASCII needles could later fold through the host probe). Candidate cap + fallback to scan should mirror `kMaximumFilenameIndexCandidates = 16,384`.
**Risks:** (1) fold mismatch false negatives — mitigated only by the ASCII-folded-column + side-set construction; default `case_sensitive=0` is **not acceptable** without a much larger differential proof; (2) exception-set degeneration on CJK corpora (every non-ASCII name is a forced candidate — gains accrue only to the ASCII-name population); (3) write amplification: trigram postings add ~`(len−2)` tokens per name inside each batch transaction — measured ingestion overhead is an acceptance gate ("a durable index that doubles ingestion time is a regression", `proposal.md`); (4) migration at 2M–5M rows + rollback proof; (5) disk size (measure bytes/1M rows).
### Feasibility verdict
**FTS5 candidate generation: FEASIBLE — but only under the specific construction above** (standalone trigram FTS5 over a pre-ASCII-folded name column, `case_sensitive=1`, exception side-set, same-`ApplyBatch`-transaction maintenance, canonical re-verification, v3 additive migration with rebuild-on-open fallback). External-content is blocked by the `WITHOUT ROWID` schema; default Unicode-folding trigram is unsafe against `CompareStringOrdinal`. Caveat the campaign must weigh: the predecessor benchmark (`docs/benchmarks/secondary-filename-index.json`) already selected **`utf16_trigram_postings`** (a durable mirror of the in-memory accelerator, design alternative B) as architecture winner and rejected FTS5 **solely because the vendored build lacked it** (`"rejected_fts_reason":"vendored_sqlite_is_not_compiled_with_fts5_and_create_fails"`). Once FTS5 is enabled, A-vs-B must be re-measured head-to-head; FTS5's advantages are maintained segment merging/crash-tested internals vs B's exact fold semantics with zero folding ambiguity. Both remain **candidate generators only**, per the change's decision record (`durable-cold-search-index-and-hybrid-residency/design.md` §2, all alternatives currently UNDECIDED — this report feeds task group 1, it does not select).
**Findings:** P2 — `SQLITE_ENABLE_FTS3=0` enables FTS3/FTS4 (`third_party/sqlite/CMakeLists.txt:15` vs `sqlite3.c:23280/193292/187296`); remove or rename. P2 (report-only) — `dbstat` absent from the build, so per-table size SQL needs `SQLITE_ENABLE_DBSTAT_VTAB` on a probe build. No other issues found in the reviewed seams.
**Merge verdict: OK with notes** (research deliverable; no code change proposed).
### Measurement commands/SQL for a human to run
```powershell
# Build the two benches (Release-test for numbers; run on an idle host)
cmake --preset release-test; cmake --build --preset release-test --target ffengine_bench_large_index_scalability ffengine_bench_secondary_filename_index
# Cold-scan floor reproduction (embeds identity; artifacts are the campaign's)
.\build\release-test\tests\engine\ffengine_bench_large_index_scalability.exe --scales 200000,1000000 --diagnostics --source-sha (git rev-parse HEAD) --output build\campaign\cold-profile-after.json
# FTS capability + architecture A/B/C measurements (re-run after enabling FTS5)
.\build\release-test\tests\engine\ffengine_bench_secondary_filename_index.exe --scales 200000,1000000,2000000 --output build\campaign\secondary-filename-index-after.json
```
SQL (via a scratch C++ probe using the vendored `sqlite3.h`, or any SQLite shell bound to the same lib):
```sql
-- 1. Confirm capability state before/after the CMake change
SELECT sqlite3_compileoption_used('ENABLE_FTS3'),   -- expect 1 today (the "=0" bug)
       sqlite3_compileoption_used('ENABLE_FTS5'),   -- expect 0 today, 1 after
       sqlite3_compileoption_used('ENABLE_ICU'),    -- expect 0
       sqlite3_compileoption_used('ENABLE_DBSTAT_VTAB'); -- expect 0 today
-- 2. Cold scan plan (expect: SCAN entries, clustered PK range; no name seek)
EXPLAIN QUERY PLAN
SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes,
       allocated_size_bytes, creation_time, last_modified_time, last_access_time, attributes
FROM entries WHERE volume_id=1
  AND (name LIKE '%marker_xyz%' ESCAPE '\' OR name GLOB '*[^ -~]*');
-- 3. Floor decomposition (time each; deltas attribute LIKE/GLOB share of the ~1.05us)
SELECT COUNT(*) FROM entries WHERE volume_id=1;                                -- bare scan
SELECT COUNT(*) FROM entries WHERE volume_id=1 AND name LIKE '%marker_xyz%' ESCAPE '\';
SELECT COUNT(*) FROM entries WHERE volume_id=1 AND name GLOB '*[^ -~]*';
-- 4. Write amplification: ingest N rows, then
PRAGMA wal_checkpoint(PASSIVE);          -- returns (busy, frames_in, frames_out)
SELECT * FROM pragma_page_count, pragma_page_size, pragma_freelist_count;
-- dbstat build only: SELECT name, SUM(pgsize) FROM dbstat GROUP BY name;
-- plus file-size deltas of <db>, <db>-wal between checkpoints
-- 5. FTS5 candidate sketch validation (after enabling FTS5)
CREATE VIRTUAL TABLE probe_tri USING fts5(name_folded, tokenize="trigram case_sensitive 1");
EXPLAIN QUERY PLAN SELECT rowid FROM probe_tri WHERE probe_tri MATCH '"marker"';  -- expect SCAN probe_tri (virtual-table index seek into trigram segments)
INSERT INTO probe_tri(name_folded) VALUES ('marker_xyz');
SELECT rowid FROM probe_tri WHERE probe_tri MATCH '"marker"';    -- substring hit
SELECT rowid FROM probe_tri WHERE probe_tri LIKE '%marker%';     -- LIKE route
-- Fold-superset red cases to run against the differential oracle:
--   name 'ſand' vs needle 'sand' (U+017F), name 'K' (U+212A) vs 'k',
--   lone-surrogate names, 2-codepoint names, needle with % _ \ * metacharacters.
```
All claims above are source-cited; the two performance numbers (~1.05–1.10 µs/row floor, 1.28–1.44× prefilter gain) are the campaign's own single-sample measurements with the caveats recorded in `memory-cold-decision.md` §2 and should be re-measured with `--source-sha` embedded per the lane E-8 provenance note.
