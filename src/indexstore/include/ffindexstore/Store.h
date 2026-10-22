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
// durable-cold-search-index-and-hybrid-residency 4.x testing seam (always
// compiled, never called in production; the SetSearchTestHooks precedent):
// deterministically fail the Nth candidate-index maintenance operation
// inside ApplyBatch (1-based; 0 disables). With hardProcessExit the seam
// terminates the calling process AT that point instead of returning failure,
// so the kill-during-commit tests exercise real process death inside an
// uncommitted transaction (never a graceful rollback).
void SetTestingCandidateMaintenanceFault(int nthOperation, bool hardProcessExit);

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
