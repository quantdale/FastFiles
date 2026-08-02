#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ffindexstore/EntryRecord.h"
#include "ffindexstore/Identity.h"

struct sqlite3;

namespace ffindexstore {

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
    // Runs an integrity check first (task 1.7); if it fails against an
    // existing file, the open fails (outIntegrityFailed is set to true) so
    // the caller can fall back to a fresh scan rather than trusting data
    // known to be corrupt (spec "Corruption is detectable").
    bool Open(const std::string& dbPathUtf8, bool* outIntegrityFailed = nullptr);
    void Close();
    bool IsOpen() const noexcept { return db_ != nullptr; }

    // task 1.7, also callable standalone (e.g. from a diagnostics path).
    bool RunIntegrityCheck();

    // task 1.6: passive checkpoint, cheap and safe to call frequently
    // (e.g. on an idle timer); does not block writers.
    bool CheckpointPassive();
    // Forces a full checkpoint if the WAL file has grown past
    // thresholdBytes -- the size-triggered forced checkpoint mitigation
    // for unbounded WAL growth under sustained write bursts (design.md
    // "Risks"). Returns false only on an I/O/database error; a WAL file
    // under the threshold is a normal, successful no-op.
    bool CheckpointIfWalExceeds(const std::string& dbPathUtf8, uint64_t thresholdBytes);

    // --- Volume metadata (tasks.md 1.3, section 7/8's persistence needs) ---

    // Looks up the volume by its durable key, creating a new row (with
    // available=true, no journal/scan state) if this is the first time
    // it's been seen. The returned VolumeRowId is stable for the life of
    // the row (see Identity.h).
    std::optional<VolumeRowId> GetOrCreateVolume(const VolumeKey& key);
    std::optional<VolumeMetadata> GetVolumeMetadata(VolumeRowId volumeRowId);
    std::vector<VolumeMetadata> GetAllVolumes();

    // tasks.md 7.2/7.3: mark availability without touching entries.
    bool SetVolumeAvailable(VolumeRowId volumeRowId, bool available, uint64_t nowTimestamp);
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

    // task 8.1/8.4: full identity set for a volume, used by reconciliation
    // to anti-join against fresh ground truth without re-streaming every
    // field for entries that haven't changed.
    std::vector<FileId> ListEntryIds(VolumeRowId volumeRowId);

    uint64_t CountEntries(VolumeRowId volumeRowId);

    // file-preview-and-properties §6.2 / storage-analysis §3.1: recursive
    // subtree aggregate from the durable store. Returns {itemCount,
    // totalSizeBytes} for all descendants of `parentFrn` within `volumeRowId`,
    // or {0, 0} if the root is unknown. Uses a recursive CTE so this works
    // without rebuilding the projection.
    struct FolderAggregate {
        uint64_t itemCount = 0;
        uint64_t totalSizeBytes = 0;
    };
    FolderAggregate GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn);

private:
    sqlite3* db_ = nullptr;
};

} // namespace ffindexstore
