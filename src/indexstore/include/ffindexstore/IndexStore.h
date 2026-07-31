#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ffindexstore/DurableStore.h"
#include "ffindexstore/Projection.h"

namespace ffindexstore {

// Orchestrates DurableStore (crash-safe source of truth) and Projection
// (compact in-memory view), enforcing the ingestion pipeline ordering
// design.md D4 depends on: commit to the durable store first, and only
// once that succeeds, apply the same batch to the projection (tasks.md
// section 3). Owned by FastFilesEngine; the engine's ingestion/reconcile/
// lifecycle code talks to the index exclusively through this type rather
// than touching DurableStore or Projection directly.
class IndexStore {
public:
    bool Open(const std::wstring& databaseFilePath);
    void Close();

    bool LastIntegrityCheckPassed() const { return store_.LastIntegrityCheckPassed(); }

    // Task 3.1/3.2: rebuilds the projection from the durable store, one
    // volume at a time, invoking `onVolumeRebuilt` immediately after each
    // volume's rows are loaded -- so the caller can publish that volume's
    // own snapshot generation without waiting for every volume to finish.
    void RebuildProjectionFromStore(const std::function<void(DurableVolumeId)>& onVolumeRebuilt);

    // Ingests one batch through the pipeline (task 3.3/3.4): commits to
    // the durable store (retrying up to `maxRetries` times on failure)
    // and, only on success, applies the batch to the projection and
    // invokes `onProjectionUpdated` (the caller's cue to bump/publish a
    // new snapshot generation). Returns false if every retry failed --
    // the projection is guaranteed untouched in that case.
    bool IngestBatch(DurableVolumeId volumeId, const std::vector<IngestEntry>& batch,
                      const std::function<void()>& onProjectionUpdated, int maxRetries = 3);

    DurableStore& Store() noexcept { return store_; }
    const DurableStore& Store() const noexcept { return store_; }
    Projection& View() noexcept { return projection_; }
    const Projection& View() const noexcept { return projection_; }

    // --- Volume lifecycle (section 7) ---

    // Maps a durable (guid, serial) identity to its local id, creating one
    // if unseen, and marks it available/last-seen (task 7.1).
    DurableVolumeId ResolveVolume(const VolumeIdentity& identity, wchar_t driveLetterHint, uint64_t nowFileTime);

    // Marks every currently-available volume NOT present in
    // `stillPresentIds` (this cycle's EnumerateVolumes result) unavailable
    // -- entries are retained, never deleted (task 7.2/7.3).
    void MarkAbsentVolumesUnavailable(const std::vector<DurableVolumeId>& stillPresentIds, uint64_t nowFileTime);

    // D6/task 7.5: true (with outResumeUsn set) if the reported journal
    // identity matches what was persisted, meaning incremental resume is
    // safe. False means either no prior journal state exists, or the
    // identity no longer matches -- in the latter case this call also
    // marks the volume as needing reconciliation (task 7.6).
    bool TryResumeJournal(DurableVolumeId id, uint64_t reportedJournalId, uint64_t& outResumeUsn);

    // Task 7.6's second trigger: the persisted resume position turned out
    // to be outside the journal's retained range once the service actually
    // attempted the read. Distinct from TryResumeJournal because that
    // condition can only be discovered by attempting FSCTL_READ_USN_JOURNAL,
    // not by comparing identities up front.
    void MarkNeedsReconciliation(DurableVolumeId id);

    // Task 7.4: explicit user action only. Removes the volume's durable
    // rows and its projection entries.
    bool ForgetVolume(DurableVolumeId id);

    // --- Reconciliation (section 8) ---

    // Compares `groundTruth` (a fresh full enumeration of the volume,
    // however the caller obtained it) against the durable store: entries
    // in the store with no counterpart in `groundTruth` are removed
    // (task 8.1's "no on-disk counterpart" case), and every ground-truth
    // entry is upserted (covers both "missing from the index" and "stale
    // field" cases in one pass) -- reusing/updating existing rows rather
    // than deleting and recreating the volume's entire entry set (task
    // 8.4). Returns the number of entries touched (added + removed +
    // updated).
    size_t ReconcileVolume(DurableVolumeId id, const std::vector<IngestEntry>& groundTruth, uint64_t nowFileTime,
                            const std::function<void()>& onProjectionUpdated);

    // Task 1.6: periodic passive checkpoint plus a size-triggered forced
    // checkpoint if the WAL has grown past the threshold. Intended to be
    // called on a low-frequency timer.
    void MaintenanceTick();

private:
    static constexpr int kWalForceCheckpointThresholdPages = 4000; // ~16 MB of WAL frames at the default 4 KiB page size

    DurableStore store_;
    Projection projection_;
};

} // namespace ffindexstore
