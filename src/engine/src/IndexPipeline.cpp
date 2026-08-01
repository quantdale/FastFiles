#include "IndexPipeline.h"

namespace ffengine {

namespace {

// $STANDARD_INFORMATION never sets this, but VolumeScanner.cpp/
// MftParser.cpp already merge the record header's own directory flag in
// on the service side (see MftParser.cpp), so this is just the plain
// Win32 attribute bit to test against here.
constexpr uint32_t kFileAttributeDirectory = 0x00000010;
// winioctl.h's USN_REASON_FILE_DELETE -- redeclared here rather than
// pulled in via windows.h so this file (like the rest of ffindexstore/
// IndexPipeline) stays free of a direct Win32 dependency.
constexpr uint32_t kUsnReasonFileDelete = 0x00000200;

// Projection/EntryRecord use std::u16string (portable, matches the wire
// protocol's own char16_t records) while SnapshotFormat.h -- consumed
// only from Windows engine code -- uses std::wstring. On Windows the two
// are the same UTF-16 bit width but remain distinct C++ types, so an
// explicit per-code-unit copy is required at this boundary rather than a
// direct iterator-range assign/append.
std::wstring ToWString(std::u16string_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (char16_t ch : text) {
        result.push_back(static_cast<wchar_t>(ch));
    }
    return result;
}

ffindexstore::EntryRecord ToEntryRecord(const ffprotocol::MftRecordV1& record) {
    ffindexstore::EntryRecord entry;
    entry.id = ffindexstore::FileId{record.fixed.fileReferenceNumber, 0};
    entry.parentId = ffindexstore::FileId{record.fixed.parentFileReferenceNumber, 0};
    entry.name = record.fileName;
    entry.sizeBytes = record.fixed.sizeBytes;
    entry.creationTime = record.fixed.creationTime;
    entry.lastModifiedTime = record.fixed.lastModifiedTime;
    entry.lastAccessTime = record.fixed.lastAccessTime;
    entry.attributes = record.fixed.fileAttributes;
    return entry;
}

ffindexstore::EntryRecord ToEntryRecord(const ffprotocol::UsnDeltaV1& record) {
    ffindexstore::EntryRecord entry;
    entry.id = ffindexstore::FileId{record.fixed.fileReferenceNumber, 0};
    entry.parentId = ffindexstore::FileId{record.fixed.parentFileReferenceNumber, 0};
    entry.name = record.fileName;
    entry.sizeBytes = record.fixed.sizeBytes;
    entry.creationTime = 0; // USN deltas don't carry creation time when the MFT re-read fallback failed
    entry.lastModifiedTime = record.fixed.timestamp;
    entry.lastAccessTime = 0;
    entry.attributes = record.fixed.fileAttributes;
    return entry;
}

// task 1.6: the size-triggered forced-checkpoint threshold. ~4000 WAL
// frames at the default 4 KiB page size (~16 MB) -- past that, a passive
// checkpoint is escalated to a full RESTART checkpoint.
constexpr uint64_t kWalForceCheckpointThresholdBytes = 4000ULL * 4096;

} // namespace

bool IndexPipeline::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!store_.Open(dbPathUtf8, outIntegrityFailed)) {
        return false;
    }
    dbPathUtf8_ = dbPathUtf8;
    return true;
}

void IndexPipeline::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    store_.Close();
    dbPathUtf8_.clear();
}

void IndexPipeline::RunStoreMaintenance() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!store_.IsOpen()) {
        return;
    }
    store_.CheckpointPassive();
    store_.CheckpointIfWalExceeds(dbPathUtf8_, kWalForceCheckpointThresholdBytes);
}

void IndexPipeline::RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt) {
    std::vector<ffindexstore::VolumeRowId> volumeIds;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& meta : store_.GetAllVolumes()) {
            projection_.RebuildVolumeFromStore(store_, meta.rowId, meta.entryCount);
            volumeIds.push_back(meta.rowId);
        }
    }
    // Invoked outside the lock: a rebuild-completion callback typically
    // publishes a snapshot, which may itself call back into
    // ExportDirectorySnapshot -- avoids a self-deadlock on mutex_.
    for (auto id : volumeIds) {
        if (onVolumeRebuilt) {
            onVolumeRebuilt(id);
        }
    }
}

ffindexstore::VolumeRowId IndexPipeline::ResolveVolume(const ffindexstore::VolumeKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto id = store_.GetOrCreateVolume(key);
    return id.value_or(0);
}

std::optional<ffindexstore::VolumeMetadata> IndexPipeline::GetVolumeMetadata(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.GetVolumeMetadata(id);
}

std::vector<ffindexstore::VolumeMetadata> IndexPipeline::GetAllVolumes() {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.GetAllVolumes();
}

bool IndexPipeline::ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV1>& records) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ffindexstore::EntryChange> changes;
    changes.reserve(records.size());
    for (const auto& record : records) {
        changes.push_back({ffindexstore::EntryChangeKind::Upsert, ToEntryRecord(record)});
    }
    // design.md D4: commit to the durable store first; the projection is
    // only touched once that commit has actually succeeded.
    if (!store_.ApplyBatch(volumeId, changes)) {
        return false;
    }
    for (const auto& change : changes) {
        projection_.Upsert(volumeId, change.record);
    }

    auto reconciling = reconciliationSeen_.find(volumeId);
    if (reconciling != reconciliationSeen_.end()) {
        for (const auto& change : changes) {
            reconciling->second.insert(change.record.id);
        }
    }
    return true;
}

bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& records) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ffindexstore::EntryChange> changes;
    changes.reserve(records.size());
    for (const auto& record : records) {
        ffindexstore::EntryChange change;
        change.kind = (record.fixed.reason & kUsnReasonFileDelete) != 0 ? ffindexstore::EntryChangeKind::Remove
                                                                         : ffindexstore::EntryChangeKind::Upsert;
        change.record = ToEntryRecord(record);
        changes.push_back(std::move(change));
    }
    if (!store_.ApplyBatch(volumeId, changes)) {
        return false;
    }
    for (const auto& change : changes) {
        if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
            projection_.Upsert(volumeId, change.record);
        } else {
            projection_.Remove(volumeId, change.record.id);
        }
    }
    return true;
}

bool IndexPipeline::SetVolumeAvailable(ffindexstore::VolumeRowId id, bool available, uint64_t nowTimestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.SetVolumeAvailable(id, available, nowTimestamp);
}

bool IndexPipeline::SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor) {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.SetScanCursor(id, cursor);
}

bool IndexPipeline::MarkScanComplete(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.MarkScanComplete(id);
}

bool IndexPipeline::SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.SetJournalPosition(id, journalId, resumeUsn);
}

bool IndexPipeline::SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.SetLastReconciliationTime(id, nowTimestamp);
}

bool IndexPipeline::ForgetVolume(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!store_.ForgetVolume(id)) {
        return false;
    }
    reconciliationSeen_.erase(id);
    // Same commit-before-apply ordering as ingestion (design.md D4): the
    // projection's copy of this volume's entries is only dropped once the
    // durable rows are actually gone, so no stale entries keep being
    // exported/published for a forgotten volume.
    projection_.RemoveVolume(id);
    return true;
}

void IndexPipeline::BeginReconciliationPass(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    reconciliationSeen_[id].clear();
}

bool IndexPipeline::IsReconciliationPassActive(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return reconciliationSeen_.find(id) != reconciliationSeen_.end();
}

void IndexPipeline::FinishReconciliationPass(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = reconciliationSeen_.find(id);
    if (it == reconciliationSeen_.end()) {
        return;
    }
    const auto& seen = it->second;

    std::vector<ffindexstore::EntryChange> removals;
    for (const auto& existingId : store_.ListEntryIds(id)) {
        if (seen.find(existingId) == seen.end()) {
            ffindexstore::EntryRecord marker;
            marker.id = existingId;
            removals.push_back({ffindexstore::EntryChangeKind::Remove, marker});
        }
    }

    if (!removals.empty() && store_.ApplyBatch(id, removals)) {
        for (const auto& removal : removals) {
            projection_.Remove(id, removal.record.id);
        }
    }

    reconciliationSeen_.erase(it);
}

std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshot(
    ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::wstring, ffprotocol::SnapshotDirectory> result;

    projection_.ForEachEntry([&](const ffindexstore::EntryKey& key, const ffindexstore::ProjectionEntry& entry) {
        if (key.volumeRowId != id || (entry.attributes & kFileAttributeDirectory) == 0) {
            return; // only directories become a listing key; files are listed as their parent's entries below
        }

        // ReconstructPath's walk includes the volume-root entry's own
        // stored name (whatever $FILE_NAME record 5 happens to carry --
        // real NTFS volumes don't store a human-meaningful name there,
        // e.g. empty or "."), which is not what a UI wants to see in
        // place of the actual drive letter. So the root entry's own name
        // is always replaced with rootPathPrefix rather than appended to.
        auto pathResult = projection_.ReconstructPath(id, entry.frn);
        const std::wstring reconstructed = ToWString(pathResult.path);
        std::wstring fullPath;
        if (entry.parentFrn == entry.frn) {
            fullPath = rootPathPrefix; // this entry IS the volume root
        } else if (pathResult.reachedRoot) {
            const size_t firstSeparator = reconstructed.find(L'\\');
            fullPath = rootPathPrefix + L"\\"
                + (firstSeparator == std::wstring::npos ? reconstructed : reconstructed.substr(firstSeparator + 1));
        } else {
            // Defensive fallback for a broken/incomplete parent chain --
            // should never happen on a well-formed volume.
            fullPath = rootPathPrefix + L"\\" + reconstructed;
        }

        ffprotocol::SnapshotDirectory directory;
        directory.status = ffprotocol::DirectoryEnumerationStatus::Success;
        if (const auto* children = projection_.ChildIndices(id, entry.frn)) {
            directory.entries.reserve(children->size());
            for (uint32_t childIndex : *children) {
                const auto& child = projection_.EntryAt(childIndex);
                ffprotocol::SnapshotDirectoryEntry snapshotEntry;
                snapshotEntry.name = ToWString(projection_.Names().Get(child.nameId));
                snapshotEntry.isDirectory = (child.attributes & kFileAttributeDirectory) != 0;
                snapshotEntry.sizeBytes = child.sizeBytes;
                snapshotEntry.attributes = child.attributes;
                directory.entries.push_back(std::move(snapshotEntry));
            }
        }
        result.emplace(std::move(fullPath), std::move(directory));
    });

    return result;
}

} // namespace ffengine
