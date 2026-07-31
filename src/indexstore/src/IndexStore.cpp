#include "ffindexstore/IndexStore.h"

#include <algorithm>

namespace ffindexstore {

bool IndexStore::Open(const std::wstring& databaseFilePath) {
    return store_.Open(databaseFilePath);
}

void IndexStore::Close() {
    store_.Close();
    projection_.Clear();
}

void IndexStore::RebuildProjectionFromStore(const std::function<void(DurableVolumeId)>& onVolumeRebuilt) {
    projection_.Clear();

    const std::vector<VolumeRecord> volumes = store_.AllVolumes();

    uint64_t totalEntryCount = 0;
    for (const auto& volume : volumes) {
        totalEntryCount += volume.entryCount;
    }
    projection_.Reserve(static_cast<size_t>(totalEntryCount));

    for (const auto& volume : volumes) {
        store_.StreamEntries(volume.id, [this](const IngestEntry& entry) { projection_.ApplyOne(entry); });
        // Task 3.2: publish this volume's own snapshot generation as soon
        // as its rebuild completes, rather than gating on every volume.
        if (onVolumeRebuilt) {
            onVolumeRebuilt(volume.id);
        }
    }
}

bool IndexStore::IngestBatch(DurableVolumeId volumeId, const std::vector<IngestEntry>& batch,
                              const std::function<void()>& onProjectionUpdated, int maxRetries) {
    bool committed = false;
    for (int attempt = 0; attempt < maxRetries && !committed; ++attempt) {
        committed = store_.CommitBatch(volumeId, batch);
    }
    if (!committed) {
        // Task 3.4/D4: the projection is never updated for an uncommitted
        // batch -- the caller (engine) is expected to surface this and
        // may retry the whole batch again later; the durable store remains
        // the single source of truth the projection can always be rebuilt
        // from.
        return false;
    }

    projection_.Apply(batch);
    if (onProjectionUpdated) {
        onProjectionUpdated();
    }
    return true;
}

DurableVolumeId IndexStore::ResolveVolume(const VolumeIdentity& identity, wchar_t driveLetterHint, uint64_t nowFileTime) {
    return store_.GetOrCreateVolume(identity, driveLetterHint, nowFileTime);
}

void IndexStore::MarkAbsentVolumesUnavailable(const std::vector<DurableVolumeId>& stillPresentIds, uint64_t nowFileTime) {
    for (const auto& volume : store_.AllVolumes()) {
        if (!volume.available) {
            continue;
        }
        const bool stillPresent =
            std::find(stillPresentIds.begin(), stillPresentIds.end(), volume.id) != stillPresentIds.end();
        if (!stillPresent) {
            // Task 7.2: mark unavailable with a last-seen timestamp;
            // entries are untouched (no delete, no cascade to other
            // volumes).
            store_.MarkVolumeUnavailable(volume.id, nowFileTime);
        }
    }
}

bool IndexStore::TryResumeJournal(DurableVolumeId id, uint64_t reportedJournalId, uint64_t& outResumeUsn) {
    auto record = store_.GetVolume(id);
    if (!record || !record->journalId || !record->resumeUsn) {
        return false; // no persisted journal state yet -- caller must OpenUsnJournal from scratch
    }
    if (*record->journalId != reportedJournalId) {
        // D6: the journal was deleted/recreated since we last saw this
        // volume -- incremental resume is unsafe; fall back to
        // reconciliation instead of either silently missing changes or an
        // unconditional full rescan.
        store_.SetNeedsReconciliation(id, true);
        return false;
    }
    outResumeUsn = *record->resumeUsn;
    return true;
}

void IndexStore::MarkNeedsReconciliation(DurableVolumeId id) {
    store_.SetNeedsReconciliation(id, true);
}

bool IndexStore::ForgetVolume(DurableVolumeId id) {
    if (!store_.ForgetVolume(id)) {
        return false;
    }
    projection_.RemoveVolume(id);
    return true;
}

size_t IndexStore::ReconcileVolume(DurableVolumeId id, const std::vector<IngestEntry>& groundTruth, uint64_t nowFileTime,
                                    const std::function<void()>& onProjectionUpdated) {
    std::vector<FileId128> groundTruthKeys;
    groundTruthKeys.reserve(groundTruth.size());
    for (const auto& entry : groundTruth) {
        groundTruthKeys.push_back(entry.key.fileReferenceNumber);
    }

    // Task 8.1: entries persisted for this volume with no counterpart in
    // the fresh ground-truth read -- a missed delete.
    const std::vector<EntryKey> staleKeys = store_.FindStaleEntries(id, groundTruthKeys);

    std::vector<IngestEntry> combinedBatch = groundTruth; // additions + stale-field updates, all as upserts
    combinedBatch.reserve(combinedBatch.size() + staleKeys.size());
    for (const auto& key : staleKeys) {
        IngestEntry removal;
        removal.op = IngestOp::Remove;
        removal.key = key;
        combinedBatch.push_back(removal);
    }

    if (!combinedBatch.empty()) {
        // Task 8.4: this reuses/updates existing (volume, FRN) rows via
        // the same upsert path ingestion always uses -- never a
        // delete-and-recreate of the volume's whole entry set.
        if (!IngestBatch(id, combinedBatch, onProjectionUpdated)) {
            return 0;
        }
    }

    store_.SetLastReconciliationTime(id, nowFileTime);
    return combinedBatch.size();
}

void IndexStore::MaintenanceTick() {
    store_.CheckpointIfWalExceeds(kWalForceCheckpointThresholdPages);
}

} // namespace ffindexstore
