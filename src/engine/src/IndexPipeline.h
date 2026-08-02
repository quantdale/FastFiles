#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ffindexstore/Projection.h"
#include "ffindexstore/Store.h"
#include "ffprotocol/Records.h"
#include "ffprotocol/SnapshotFormat.h"

namespace ffengine {

// index-storage-and-scanning tasks.md sections 3/6/8: owns the durable
// SQLite store and the in-memory projection, and is the single point
// where the engine's ingestion pipeline enforces "commit to the durable
// store before applying to the projection" (design.md D4) -- every
// mutating method here does exactly that internally, so no caller can
// accidentally apply a batch to the projection without it having first
// durably committed.
//
// Internally synchronized (a single mutex serializes rebuild/ingestion/
// export) -- callers (the engine's scan/journal callbacks, which run on
// PrivilegedConnection's reader thread, and any UI-facing snapshot-export
// caller) do not need to coordinate with each other.
class IndexPipeline {
public:
    // outIntegrityFailed (optional, task 1.7): set to true when the open
    // failed specifically because the existing file failed its integrity
    // check, so the caller can apply the rebuild-from-fresh-scan fallback
    // (delete the file and reopen) rather than treating it as a generic
    // I/O failure.
    bool Open(const std::string& dbPathUtf8, bool* outIntegrityFailed = nullptr);
    void Close();

    // task 1.6: scheduled WAL maintenance, intended to be called on a
    // low-frequency timer (the engine calls it from VolumeSessionManager's
    // ReconciliationSchedulerLoop poll). Runs a cheap passive checkpoint
    // every call and escalates to a forced checkpoint once the WAL has
    // grown past the threshold (design.md "Risks": unbounded WAL growth
    // under sustained write bursts). No-op if the store was never opened.
    void RunStoreMaintenance();

    // task 3.1/3.2: rebuilds every volume already known to the durable
    // store, invoking onVolumeRebuilt once per volume as soon as that
    // volume's rebuild completes (not gated on every volume finishing).
    using VolumeRebuiltCallback = std::function<void(ffindexstore::VolumeRowId)>;
    void RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt);

    ffindexstore::VolumeRowId ResolveVolume(const ffindexstore::VolumeKey& key);
    std::optional<ffindexstore::VolumeMetadata> GetVolumeMetadata(ffindexstore::VolumeRowId id);
    std::vector<ffindexstore::VolumeMetadata> GetAllVolumes();

    // tasks.md 3.3/3.4/6.2: commits the batch to the durable store first;
    // only applies it to the projection once that commit succeeds. On a
    // failed commit, the projection is left untouched and false is
    // returned so the caller can retry the same batch later -- it is
    // never partially applied. Entries are always linked by
    // FileReferenceNumber (never a re-derived path), which is what tasks.md
    // 6.2 calls "filename canonicalization ... keyed off
    // FileReferenceNumber" in practice: EntryRecord.parentId always comes
    // from the wire record's own parent FRN field, never from a computed
    // path.
    bool ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV1>& records);
    // USN_REASON_FILE_DELETE-flagged records are applied as removals;
    // everything else is an upsert (tasks.md 6.1's engine-side consumer).
    bool ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& records);

    bool SetVolumeAvailable(ffindexstore::VolumeRowId id, bool available, uint64_t nowTimestamp);
    bool SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor);
    bool MarkScanComplete(ffindexstore::VolumeRowId id);
    bool SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn);
    bool SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp);
    bool ForgetVolume(ffindexstore::VolumeRowId id);

    // tasks.md section 8: a from-scratch scan (StartVolumeScan issued
    // with no resume cursor) doubles as a reconciliation pass once it
    // runs to completion, since a full MFT enumeration is ground truth
    // for what currently exists. BeginReconciliationPass starts tracking
    // which (already-known) entries this pass observes; every Upsert
    // applied via ApplyMftBatch while a pass is active for that volume is
    // recorded; FinishReconciliationPass (call on ScanComplete) removes
    // any previously-persisted entry that the pass never observed --
    // tasks.md 8.1's "entries present in the index but no longer
    // resolvable". Scoped to a single continuous engine session: if the
    // engine restarts mid-pass, the in-memory observed-set is lost and
    // the next *fully-completed* pass reconciles correctly rather than
    // acting on a partial view.
    void BeginReconciliationPass(ffindexstore::VolumeRowId id);
    void FinishReconciliationPass(ffindexstore::VolumeRowId id);
    bool IsReconciliationPassActive(ffindexstore::VolumeRowId id);

    // Converts the current projection state for one volume into the
    // existing directory-listing snapshot wire format (SnapshotFormat.h),
    // keyed by full reconstructed path under `rootPathPrefix` (e.g.
    // L"C:"), so it can be published through the already-established
    // double-buffered snapshot mechanism (tasks.md 3.5) without inventing
    // a new wire format for this change.
    std::map<std::wstring, ffprotocol::SnapshotDirectory> ExportDirectorySnapshot(
        ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix);

    // file-preview-and-properties §6.2 / storage-analysis §3.1: returns the
    // subtree aggregate for `parentFrn` from the in-memory projection if
    // the folder is known, or std::nullopt if it is not.
    std::optional<ffindexstore::Projection::FolderAggregate> GetFolderAggregate(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn);

private:
    std::mutex mutex_;
    ffindexstore::Store store_;
    ffindexstore::Projection projection_;
    std::string dbPathUtf8_; // remembered from Open() for RunStoreMaintenance's WAL-size check

    struct FileIdHash {
        size_t operator()(const ffindexstore::FileId& id) const noexcept {
            return std::hash<uint64_t>{}(id.low) ^ (std::hash<uint64_t>{}(id.high) << 1);
        }
    };
    std::unordered_map<ffindexstore::VolumeRowId, std::unordered_set<ffindexstore::FileId, FileIdHash>>
        reconciliationSeen_;
};

} // namespace ffengine
