#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ffindexstore/FileId.h"
#include "ffindexstore/IngestEntry.h"

struct sqlite3;

namespace ffindexstore {

// Stable identity for a volume: GUID plus cached serial number (design.md
// D6), independent of the wire protocol's ephemeral, connection-scoped
// VolumeId.
struct VolumeIdentity {
    std::wstring volumeGuid;
    uint64_t serialNumber = 0;
};

struct VolumeRecord {
    DurableVolumeId id = kInvalidDurableVolumeId;
    VolumeIdentity identity;
    wchar_t driveLetter = L'\0'; // last-known, cosmetic only -- never part of identity
    bool available = true;
    uint64_t lastSeenTime = 0; // FILETIME
    std::optional<uint64_t> journalId;
    std::optional<uint64_t> resumeUsn;
    std::vector<uint8_t> scanCursor; // empty = no in-progress-scan cursor persisted
    bool scanComplete = false;
    bool needsReconciliation = false;
    uint64_t lastReconciliationTime = 0;
    uint64_t entryCount = 0; // tracked for projection pre-sizing (task 2.5)
};

// The durable, crash-safe source of truth (design.md D1): a single-file
// SQLite database in WAL mode. Every entry is keyed by (volume id,
// FileReferenceNumber) only -- never by path (D7) -- and names are stored
// denormalized as plain strings (D3; interning is an in-memory-only
// optimization the Projection applies, not this store).
//
// Not internally synchronized beyond what SQLite's own WAL mode provides
// (concurrent readers do not block the writer and vice versa, per
// PRAGMA journal_mode=WAL) -- callers must still serialize their own
// write-path calls (IndexStore does, matching the single-writer ingestion
// pipeline).
class DurableStore {
public:
    ~DurableStore();

    // Opens (creating if absent) the database at `databaseFilePath`,
    // configures WAL + synchronous=NORMAL, ensures schema exists (creating
    // it from scratch if the file is new), and runs an integrity check
    // (task 1.7). If the integrity check fails, the caller (IndexStore)
    // is expected to discard the file and start a fresh scan -- this
    // method itself does not delete anything.
    bool Open(const std::wstring& databaseFilePath);
    void Close();

    bool IsOpen() const noexcept { return db_ != nullptr; }
    bool LastIntegrityCheckPassed() const noexcept { return integrityOk_; }

    // --- Volume identity and lifecycle (tasks 1.3, 7.1-7.6) ---

    // Returns the existing durable id for `identity` if already known, or
    // creates a new `volumes` row and returns its new id.
    DurableVolumeId GetOrCreateVolume(const VolumeIdentity& identity, wchar_t driveLetterHint, uint64_t nowFileTime);

    std::optional<VolumeRecord> GetVolume(DurableVolumeId id) const;
    std::vector<VolumeRecord> AllVolumes() const;

    void MarkVolumeAvailable(DurableVolumeId id, wchar_t driveLetterHint, uint64_t nowFileTime);
    void MarkVolumeUnavailable(DurableVolumeId id, uint64_t nowFileTime);

    void SetJournalId(DurableVolumeId id, uint64_t journalId);
    void SetResumeUsn(DurableVolumeId id, uint64_t resumeUsn);
    void SetScanCursor(DurableVolumeId id, const std::vector<uint8_t>& cursor);
    void ClearScanCursor(DurableVolumeId id);
    void SetScanComplete(DurableVolumeId id, bool complete);
    void SetNeedsReconciliation(DurableVolumeId id, bool needsReconciliation);
    void SetLastReconciliationTime(DurableVolumeId id, uint64_t nowFileTime);

    // Explicit user action only (task 7.4) -- deletes the volume's row and
    // every entry belonging to it. Never called automatically on
    // disconnect.
    bool ForgetVolume(DurableVolumeId id);

    // --- Ingestion (tasks 1.5, 3.3, 3.4) ---

    // Commits the entire batch as one transaction. Returns false (with the
    // transaction rolled back, so partial writes never persist) on any
    // failure; the caller MUST NOT apply the batch to the in-memory
    // projection when this returns false, and should retry (task 3.4).
    bool CommitBatch(DurableVolumeId volumeId, const std::vector<IngestEntry>& batch);

    // --- Startup rebuild (task 3.1) ---

    // Streams every entry currently persisted for `volumeId` to `visitor`,
    // one row at a time, in a single pass -- never materializes the whole
    // result set in memory at once.
    void StreamEntries(DurableVolumeId volumeId, const std::function<void(const IngestEntry&)>& visitor) const;

    // --- Reconciliation (task 8.1, 8.4) ---

    // Entries persisted for `volumeId` whose FileReferenceNumber is absent
    // from `groundTruthKeys` -- i.e. entries with no on-disk counterpart
    // (design.md D1's anti-join rationale). Implemented via a temporary
    // table, not by loading both sides into C++ containers.
    std::vector<EntryKey> FindStaleEntries(DurableVolumeId volumeId, const std::vector<FileId128>& groundTruthKeys) const;

    // Removes exactly the given entries (used to apply FindStaleEntries'
    // result) -- never the volume's whole entry set (task 8.4).
    bool RemoveEntries(DurableVolumeId volumeId, const std::vector<EntryKey>& keys);

    // --- Maintenance (task 1.6) ---

    // Passive WAL checkpoint: opportunistic, never blocks on readers.
    void CheckpointPassive();
    // Forces a full checkpoint if the WAL file has grown past
    // `thresholdPages` database pages, to bound unbounded WAL growth under
    // sustained write bursts (design.md Risks).
    void CheckpointIfWalExceeds(int thresholdPages);

private:
    sqlite3* db_ = nullptr;
    bool integrityOk_ = false;

    bool EnsureSchema();
};

} // namespace ffindexstore
