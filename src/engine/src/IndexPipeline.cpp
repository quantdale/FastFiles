#include "IndexPipeline.h"

#include <algorithm>
#include <chrono>
#include <cwctype>

#include "VolumeIdentity.h"

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

std::wstring NormalizeVolumePrefix(std::wstring value) {
    if (value.size() < 2 || value[1] != L':') {
        return {};
    }
    value.resize(2);
    value[0] = static_cast<wchar_t>(towupper(value[0]));
    return value;
}

// Projection::ReconstructPath includes the volume-root entry's stored name.
// The UI path model replaces that synthetic component with the mounted volume
// prefix, so remove only the exact root component and preserve every real
// descendant.  The walk is bounded by the projection's parent chain and is
// defensive against a malformed/cyclic chain.
std::wstring StripVolumeRootName(const ffindexstore::Projection& projection,
                                 ffindexstore::VolumeRowId volumeId,
                                 ffindexstore::FileId current,
                                 std::wstring reconstructed) {
    for (size_t depth = 0; depth <= projection.EntryCount(); ++depth) {
        const auto* currentEntry = projection.Find(volumeId, current);
        if (currentEntry == nullptr) break;
        if (currentEntry->parentFrn == currentEntry->frn) {
            const std::wstring rootName = ToWString(projection.Names().Get(currentEntry->nameId));
            if (rootName.empty()) return reconstructed;
            if (reconstructed == rootName) return {};
            if (reconstructed.size() > rootName.size() &&
                reconstructed[rootName.size()] == L'\\' &&
                _wcsnicmp(reconstructed.c_str(), rootName.c_str(), rootName.size()) == 0) {
                reconstructed.erase(0, rootName.size() + 1);
            }
            return reconstructed;
        }
        current = currentEntry->parentFrn;
    }
    return reconstructed;
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
    return true;
}

void IndexPipeline::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    store_.Close();
}

void IndexPipeline::RunStoreMaintenance() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!store_.IsOpen()) {
        return;
    }
    store_.CheckpointPassive();
    store_.CheckpointIfWalExceeds(kWalForceCheckpointThresholdBytes);
}

void IndexPipeline::RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt) {
    std::vector<ffindexstore::VolumeRowId> volumeIds;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& meta : store_.GetAllVolumes()) {
            projection_.RebuildVolumeFromStore(store_, meta.rowId, meta.entryCount);
            // zero-touch-autonomous-engineering (subtree gating): after the
            // projection is rebuilt from the durable store (parent chains
            // fully present), prune any excluded subtrees from store +
            // projection so the rebuilt index honors the configured rules.
            // This mirrors the ingestion prune and the reconciliation catch-
            // all, ensuring rules are honored on every equivalent ingestion
            // path (initial scan, USN deltas, reconciliation, rebuild).
            if (volumeRules_.find(meta.rowId) != volumeRules_.end()) {
                std::vector<ffindexstore::EntryChange> ruleExcluded;
                store_.ForEachEntry(meta.rowId, [&](const ffindexstore::EntryRecord& record) {
                    if (!IsEntryIncludedLocked(meta.rowId, record.id)) {
                        ruleExcluded.push_back({ffindexstore::EntryChangeKind::Remove, record});
                    }
                });
                if (!ruleExcluded.empty() && store_.ApplyBatch(meta.rowId, ruleExcluded)) {
                    for (const auto& removal : ruleExcluded) {
                        projection_.Remove(meta.rowId, removal.record.id);
                    }
                }
            }
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

void IndexPipeline::SetVolumeRules(ffindexstore::VolumeRowId volumeId, const ffprotocol::VolumeSetting& setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    volumeRules_[volumeId] = setting;
}

bool IndexPipeline::IsEntryIncludedLocked(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId frn) {
    const auto ruleIt = volumeRules_.find(volumeId);
    if (ruleIt == volumeRules_.end()) {
        return true; // no rules configured for this volume -> include everything
    }
    const auto& setting = ruleIt->second;
    // No rules and enabled -> include everything (preserves pre-change behavior).
    if (setting.rules.empty()) {
        return setting.enabled;
    }
    // Reconstruct the canonical path from the FRN parent chain. The path comes
    // back as a backslash-joined name string WITHOUT the drive-letter prefix
    // (ExportDirectorySnapshot prepends rootPathPrefix separately); IsPathIncluded
    // matches against full canonical paths like "C:\Work", so prepend the
    // volume's key (e.g. "C:") + a separator.
    const auto pathResult = projection_.ReconstructPath(volumeId, frn);
    if (!pathResult.reachedRoot) {
        // Parent chain not yet resolvable (e.g. parent arrives in a later
        // batch). Defer: include for now and let the reconciliation pass
        // decide once the chain resolves -- never silently drop an undecided
        // record (design.md "decide-on-reconciliation fallback").
        return true;
    }
    std::wstring prefix = setting.key;
    if (!prefix.empty() && prefix.back() != L'\\') {
        prefix.push_back(L'\\');
    }
    std::wstring canonical = prefix + ToWString(pathResult.path);
    return ffprotocol::IsPathIncluded(setting, canonical);
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
            // zero-touch-autonomous-engineering (subtree gating): only mark an
            // entry "observed" by the reconciliation pass when its configured
            // rules include it, so a previously-included-but-now-excluded
            // entry is reconciled away by FinishReconciliationPass. Records
            // whose path cannot yet be resolved are marked observed (deferred)
            // to preserve the existing orphan-tolerant reconciliation behavior.
            if (IsEntryIncludedLocked(volumeId, change.record.id)) {
                reconciling->second.insert(change.record.id);
            }
        }
    }

    // zero-touch-autonomous-engineering (subtree gating): prune any record in
    // this batch whose parent chain now resolves and which the configured
    // rules exclude. Records whose chain did not resolve are left in place
    // and decided on the next reconciliation pass (IsEntryIncludedLocked
    // returns true for them). Removals are committed to the durable store
    // then the projection, preserving the commit-before-apply invariant.
    std::vector<ffindexstore::EntryChange> ruleExcluded;
    for (const auto& change : changes) {
        if (!IsEntryIncludedLocked(volumeId, change.record.id)) {
            ffindexstore::EntryChange removal{ffindexstore::EntryChangeKind::Remove, change.record};
            ruleExcluded.push_back(removal);
        }
    }
    if (!ruleExcluded.empty() && store_.ApplyBatch(volumeId, ruleExcluded)) {
        for (const auto& removal : ruleExcluded) {
            projection_.Remove(volumeId, removal.record.id);
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

    // zero-touch-autonomous-engineering (subtree gating): prune upserted
    // USN records whose parent chain now resolves and which the configured
    // rules exclude (deletes are already no-ops against excluded subtrees).
    // Deferred records are decided on the next reconciliation pass.
    std::vector<ffindexstore::EntryChange> ruleExcluded;
    for (const auto& change : changes) {
        if (change.kind == ffindexstore::EntryChangeKind::Upsert && !IsEntryIncludedLocked(volumeId, change.record.id)) {
            ffindexstore::EntryChange removal{ffindexstore::EntryChangeKind::Remove, change.record};
            ruleExcluded.push_back(removal);
        }
    }
    if (!ruleExcluded.empty() && store_.ApplyBatch(volumeId, ruleExcluded)) {
        for (const auto& removal : ruleExcluded) {
            projection_.Remove(volumeId, removal.record.id);
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
        // zero-touch-autonomous-engineering (subtree gating): remove an entry
        // when it was not observed by this pass OR when the configured rules
        // now exclude it. The latter is the catch-all that prunes records
        // deferred during ingestion (whose parent chain resolved only later)
        // and any previously-included-but-now-excluded subtree, so rule
        // changes take full effect on the next reconciliation without a restart.
        const bool notSeen = seen.find(existingId) == seen.end();
        const bool excluded = !IsEntryIncludedLocked(id, existingId);
        if (notSeen || excluded) {
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
            // ReconstructPath includes the root entry's on-disk name at the
            // front of the string.  That name is commonly empty, but valid
            // volumes can expose a non-empty synthetic root name (for example
            // ".").  Strip exactly that root component; stripping through the
            // first separator would incorrectly turn Users\\Alice into Alice.
            const std::wstring relative = StripVolumeRootName(projection_, id, entry.frn, reconstructed);
            fullPath = rootPathPrefix;
            if (!relative.empty()) fullPath += L"\\" + relative;
        } else {
            // Defensive fallback for a broken/incomplete parent chain --
            // should never happen on a well-formed volume.
            fullPath = rootPathPrefix + L"\\" + reconstructed;
        }

        ffprotocol::SnapshotDirectory directory;
        directory.status = ffprotocol::DirectoryEnumerationStatus::Success;
        directory.volumeRowId = entry.volumeRowId;
        directory.directoryIdLow = entry.frn.low;
        directory.directoryIdHigh = entry.frn.high;
        directory.parentIdLow = entry.parentFrn.low;
        directory.parentIdHigh = entry.parentFrn.high;
        if (auto children = projection_.ChildIndices(id, entry.frn); !children.empty()) {
            directory.entries.reserve(children.size());
            for (uint32_t childIndex : children) {
                const auto& child = projection_.EntryAt(childIndex);
                ffprotocol::SnapshotDirectoryEntry snapshotEntry;
                snapshotEntry.name = ToWString(projection_.Names().Get(child.nameId));
                snapshotEntry.isDirectory = (child.attributes & kFileAttributeDirectory) != 0;
                snapshotEntry.sizeBytes = child.sizeBytes;
                snapshotEntry.attributes = child.attributes;
                snapshotEntry.creationTime = child.creationTime;
                snapshotEntry.lastModifiedTime = child.lastModifiedTime;
                snapshotEntry.volumeRowId = child.volumeRowId;
                snapshotEntry.fileIdLow = child.frn.low;
                snapshotEntry.fileIdHigh = child.frn.high;
                snapshotEntry.parentIdLow = child.parentFrn.low;
                snapshotEntry.parentIdHigh = child.parentFrn.high;
                directory.entries.push_back(std::move(snapshotEntry));
            }
        }
        result.emplace(std::move(fullPath), std::move(directory));
    });

    return result;
}

std::optional<ffindexstore::Projection::FolderAggregate> IndexPipeline::GetFolderAggregate(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn) {
    std::lock_guard<std::mutex> lock(mutex_);
    return projection_.GetFolderAggregate(volumeId, parentFrn);
}

ffprotocol::IndexedSearchResponse IndexPipeline::SearchIndexed(const ffprotocol::IndexedSearchRequest& request,
                                                               SearchTimings* timings) {
    using Clock = std::chrono::steady_clock;
    const auto totalStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    const auto setTotal = [&] {
        if (timings != nullptr) {
            timings->totalMs = std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
        }
    };
    ffprotocol::IndexedSearchResponse result;
    result.requestId = request.requestId;
    if (request.requestId == 0 || !ffprotocol::IsIndexedSearchScopeValid(request.scope) ||
        !ffprotocol::IsIndexedSearchSortValid(request.sortField) || request.query.empty() ||
        request.query.size() > ffprotocol::kMaxIndexedSearchQueryLengthChars ||
        request.scopePath.size() > ffprotocol::kMaxPathLengthChars || request.maxResults == 0) {
        result.status = ffprotocol::IndexedSearchStatus::InvalidRequest;
        setTotal();
        return result;
    }

    std::vector<ffsearch::Candidate> candidates;
    std::chrono::steady_clock::time_point candidateStart{};
    {
        // Copy only the candidate fields needed by the query. Matching and
        // sorting are intentionally outside this lock so a broad query cannot
        // block ingestion or snapshot publication while a UI pipe is waiting.
        std::lock_guard<std::mutex> lock(mutex_);
        if (!store_.IsOpen()) {
            result.status = ffprotocol::IndexedSearchStatus::Unavailable;
            setTotal();
            return result;
        }

        std::unordered_map<ffindexstore::VolumeRowId, std::wstring> prefixes;
        prefixes.reserve(volumeRules_.size() + 4);
        std::unordered_set<ffindexstore::VolumeRowId> disabledVolumes;
        disabledVolumes.reserve(volumeRules_.size());
        for (const auto& [volumeId, setting] : volumeRules_) {
            if (!setting.enabled) {
                disabledVolumes.insert(volumeId);
                continue;
            }
            if (const std::wstring prefix = NormalizeVolumePrefix(setting.key); !prefix.empty()) {
                prefixes.emplace(volumeId, prefix);
            }
        }
        const auto sqliteStart = timings != nullptr ? Clock::now() : Clock::time_point{};
        const auto metadata = store_.GetAllVolumes();
        if (timings != nullptr) {
            timings->sqliteMetadataMs =
                std::chrono::duration<double, std::milli>(Clock::now() - sqliteStart).count();
        }
        for (const auto& metadataEntry : metadata) {
            if (!metadataEntry.available || disabledVolumes.contains(metadataEntry.rowId) || prefixes.contains(metadataEntry.rowId)) {
                continue;
            }
            const wchar_t driveLetter = ResolveDriveLetterForVolumeKey(metadataEntry.key);
            if (driveLetter != L'\0') {
                std::wstring prefix;
                prefix.push_back(static_cast<wchar_t>(towupper(driveLetter)));
                prefix.push_back(L':');
                prefixes.emplace(metadataEntry.rowId, std::move(prefix));
            }
        }
        if (prefixes.empty()) {
            result.status = ffprotocol::IndexedSearchStatus::Unavailable;
            setTotal();
            return result;
        }

        candidateStart = timings != nullptr ? Clock::now() : Clock::time_point{};
        candidates.reserve(projection_.EntryCount());
        projection_.ForEachEntry([&](const ffindexstore::EntryKey& key,
                                      const ffindexstore::ProjectionEntry& entry) {
            const auto prefixIt = prefixes.find(key.volumeRowId);
            if (prefixIt == prefixes.end()) {
                return;
            }
            const std::wstring rootPath = prefixIt->second + L"\\";
            ffsearch::Candidate candidate;
            candidate.volumeId = static_cast<int64_t>(key.volumeRowId);
            candidate.id = entry.frn.low;
            candidate.parentId = entry.parentFrn.low;
            candidate.sizeBytes = entry.sizeBytes;
            candidate.modifiedTime = entry.lastModifiedTime;
            candidate.createdTime = entry.creationTime;
            candidate.isDirectory = (entry.attributes & kFileAttributeDirectory) != 0;
            candidate.persistentIndexed = true;

            if (entry.parentFrn == entry.frn) {
                // The root's on-disk name is not user-facing. Use the mounted
                // drive root exactly as the snapshot/UI path model does.
                candidate.name = rootPath;
            } else {
                const auto parentPath = projection_.ReconstructPath(key.volumeRowId, entry.parentFrn);
                if (!parentPath.reachedRoot) {
                    return; // do not expose a candidate with an untrustworthy path
                }
                const std::wstring reconstructedParent = StripVolumeRootName(
                    projection_, key.volumeRowId, entry.parentFrn, ToWString(parentPath.path));
                // After the synthetic root component is stripped, a direct
                // child is "Users" and a deeper parent is "Users\\Alice".
                // Keep the complete relative path here:
                // dropping everything through the first separator would turn
                // a nested result into C:\\Alice instead of C:\\Users\\Alice.
                candidate.folder = rootPath + reconstructedParent;
                candidate.name = ToWString(projection_.Names().Get(entry.nameId));
                if (candidate.name.empty()) {
                    return;
                }
            }
            candidates.push_back(std::move(candidate));
        });
    }
    if (timings != nullptr) {
        timings->candidateMaterializationMs =
            std::chrono::duration<double, std::milli>(Clock::now() - candidateStart).count();
        timings->candidateCount = candidates.size();
    }

    const auto queryStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    ffsearch::SearchRequest searchRequest;
    searchRequest.query = ffsearch::ParseQuery(request.query, ffsearch::FilterRegistry::WithDefaults());
    searchRequest.scope = static_cast<ffsearch::SearchScope>(request.scope);
    searchRequest.scopePath = request.scopePath;
    searchRequest.sortField = static_cast<ffsearch::SortField>(request.sortField);
    searchRequest.descending = request.descending;
    searchRequest.maxResults = (std::min)(static_cast<size_t>(request.maxResults),
                                          static_cast<size_t>(ffprotocol::kMaxIndexedSearchResultCount));
    searchRequest.persistentOnly = true;
    if (timings != nullptr) {
        timings->queryParseMs =
            std::chrono::duration<double, std::milli>(Clock::now() - queryStart).count();
    }

    const auto searchStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    const auto search = ffsearch::ExecuteSearch(candidates, searchRequest);
    if (timings != nullptr) {
        timings->matchFilterSortMs =
            std::chrono::duration<double, std::milli>(Clock::now() - searchStart).count();
        timings->candidatesVisited = search.candidatesVisited;
        timings->resultCount = search.results.size();
        timings->truncated = search.truncated;
    }
    result.status = ffprotocol::IndexedSearchStatus::Completed;
    result.truncated = search.truncated;
    result.candidates.reserve(search.results.size());
    const auto responseStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    for (const auto& matched : search.results) {
        const auto& candidate = matched.candidate;
        result.candidates.push_back({candidate.name, candidate.folder, candidate.sizeBytes,
                                     candidate.modifiedTime, candidate.isDirectory,
                                     candidate.createdTime, candidate.volumeId, candidate.id,
                                     candidate.parentId, candidate.persistentIndexed});
    }
    if (timings != nullptr) {
        timings->responseMaterializationMs =
            std::chrono::duration<double, std::milli>(Clock::now() - responseStart).count();
        setTotal();
    }
    return result;
}

} // namespace ffengine
