#include "IndexPipeline.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <limits>
#include <utility>

#include "ffprotocol/Diagnostics.h"
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

std::u16string ToU16String(std::wstring_view text) {
    std::u16string result;
    result.reserve(text.size());
    for (wchar_t ch : text) {
        result.push_back(static_cast<char16_t>(ch));
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
                                 std::wstring reconstructed,
                                 const std::function<bool()>& cancelled = {}) {
    for (size_t depth = 0; depth <= projection.EntryCount(); ++depth) {
        if (cancelled && cancelled()) return {};
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

bool MaterializeCandidateFolder(const ffindexstore::Projection& projection,
                                const std::unordered_map<ffindexstore::VolumeRowId, std::wstring>& prefixes,
                                ffsearch::Candidate& candidate,
                                const std::function<bool()>& cancelled = {}) {
    if (cancelled && cancelled()) return false;
    const auto prefixIt = prefixes.find(static_cast<ffindexstore::VolumeRowId>(candidate.volumeId));
    if (prefixIt == prefixes.end()) return false;
    const auto* entry = projection.Find(static_cast<ffindexstore::VolumeRowId>(candidate.volumeId),
                                        ffindexstore::FileId{candidate.id, 0});
    if (entry == nullptr) return false;
    if (entry->parentFrn == entry->frn) {
        // The root candidate intentionally keeps the same empty folder that
        // the eager path used; its user-facing root is stored in name.
        return true;
    }
    const auto parentPath = projection.ReconstructPathUntil(
        static_cast<ffindexstore::VolumeRowId>(candidate.volumeId), entry->parentFrn, cancelled);
    if (!parentPath.reachedRoot) return false;
    const std::wstring reconstructedParent = StripVolumeRootName(
        projection, static_cast<ffindexstore::VolumeRowId>(candidate.volumeId), entry->parentFrn,
        ToWString(parentPath.path), cancelled);
    candidate.folder = prefixIt->second + L"\\" + reconstructedParent;
    return true;
}

// nonblocking-indexed-read-plane 4.3: durable parent-chain materialization
// never touches the writer connection and never requires the pipeline mutex.
bool MaterializeCandidateFolderDurable(
    ffindexstore::StoreReadSession& readSession,
    const std::unordered_map<ffindexstore::VolumeRowId, std::wstring>& prefixes,
    ffsearch::Candidate& candidate, const std::function<bool()>& cancelled = {}) {
    const auto prefixIt = prefixes.find(static_cast<ffindexstore::VolumeRowId>(candidate.volumeId));
    if (prefixIt == prefixes.end()) return false;
    const ffindexstore::VolumeRowId volumeId = static_cast<ffindexstore::VolumeRowId>(candidate.volumeId);
    const auto entry = readSession.FindEntry(volumeId, ffindexstore::FileId{candidate.id, 0});
    if (!entry.has_value()) return false;
    if (entry->parentId == entry->id) {
        // The root candidate intentionally keeps the same empty folder that
        // the hot path uses; its user-facing root lives in name.
        return true;
    }
    constexpr size_t kMaxColdWalkDepth = 4096;
    std::vector<std::u16string> segmentsReversed;
    ffindexstore::FileId current = entry->parentId;
    bool reachedRoot = false;
    for (size_t depth = 0; depth < kMaxColdWalkDepth; ++depth) {
        if (cancelled && cancelled()) return false;
        const auto parent = readSession.FindEntry(volumeId, current);
        if (!parent.has_value()) break;
        if (parent->parentId == parent->id) {
            reachedRoot = true;
            break;
        }
        segmentsReversed.push_back(parent->name);
        current = parent->parentId;
    }
    if (!reachedRoot) return false;
    std::wstring folder = prefixIt->second;
    for (auto it = segmentsReversed.rbegin(); it != segmentsReversed.rend(); ++it) {
        folder += L"\\";
        folder += ToWString(*it);
    }
    candidate.folder = std::move(folder);
    return true;
}

ffsearch::Candidate MakeProjectionCandidate(const ffindexstore::Projection& projection,
                                            const ffindexstore::ProjectionEntry& entry,
                                            const std::wstring& prefix) {
    ffsearch::Candidate candidate;
    candidate.volumeId = static_cast<int64_t>(entry.volumeRowId);
    candidate.id = entry.frn.low;
    candidate.parentId = entry.parentFrn.low;
    candidate.sizeBytes = entry.sizeBytes;
    candidate.modifiedTime = entry.lastModifiedTime;
    candidate.createdTime = entry.creationTime;
    candidate.isDirectory = (entry.attributes & kFileAttributeDirectory) != 0;
    candidate.persistentIndexed = true;
    candidate.allocatedSizeBytes = entry.allocatedSizeBytes;
    candidate.allocatedSizeKnown = entry.allocatedSizeKnown;
    if (entry.parentFrn == entry.frn) {
        candidate.name = prefix + L"\\";
    } else {
        candidate.name = ToWString(projection.Names().Get(entry.nameId));
    }
    return candidate;
}

ffsearch::Candidate MakeDurableCandidate(const ffindexstore::EntryRecord& record,
                                         ffindexstore::VolumeRowId volumeId,
                                         const std::wstring& prefix) {
    ffsearch::Candidate candidate;
    candidate.volumeId = static_cast<int64_t>(volumeId);
    candidate.id = record.id.low;
    candidate.parentId = record.parentId.low;
    candidate.sizeBytes = record.sizeBytes;
    candidate.modifiedTime = record.lastModifiedTime;
    candidate.createdTime = record.creationTime;
    candidate.isDirectory = (record.attributes & kFileAttributeDirectory) != 0;
    candidate.persistentIndexed = true;
    candidate.allocatedSizeBytes = record.allocatedSizeBytes;
    candidate.allocatedSizeKnown = record.allocatedSizeKnown;
    candidate.name = record.parentId == record.id ? prefix + L"\\" : ToWString(record.name);
    return candidate;
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
    // allocated-size-storage-accuracy 5.3: a V1 record predates the field;
    // allocated size stays unknown (never fabricated).
    entry.allocatedSizeKnown = false;
    return entry;
}

ffindexstore::EntryRecord ToEntryRecord(const ffprotocol::MftRecordV2& record) {
    ffindexstore::EntryRecord entry;
    entry.id = ffindexstore::FileId{record.fixed.fileReferenceNumber, 0};
    entry.parentId = ffindexstore::FileId{record.fixed.parentFileReferenceNumber, 0};
    entry.name = record.fileName;
    entry.sizeBytes = record.fixed.sizeBytes;
    entry.allocatedSizeBytes = record.fixed.allocatedSizeBytes;
    // A parsed $FILE_NAME always yields both cached sizes, so a V2 scan
    // record's allocated size is authoritative by construction.
    entry.allocatedSizeKnown = true;
    entry.creationTime = record.fixed.creationTime;
    entry.lastModifiedTime = record.fixed.lastModifiedTime;
    entry.lastAccessTime = record.fixed.lastAccessTime;
    entry.attributes = record.fixed.fileAttributes;
    return entry;
}

ffindexstore::EntryRecord ToEntryRecord(const ffprotocol::UsnDeltaV2& record) {
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

ffindexstore::EntryRecord ToEntryRecord(const ffprotocol::UsnDeltaV3& record) {
    ffindexstore::EntryRecord entry;
    entry.id = ffindexstore::FileId{record.fixed.fileReferenceNumber, 0};
    entry.parentId = ffindexstore::FileId{record.fixed.parentFileReferenceNumber, 0};
    entry.name = record.fileName;
    entry.sizeBytes = record.fixed.sizeBytes;
    // allocated-size-storage-accuracy 5.3: the presence bit is authoritative
    // -- the service sets it only after successful MFT re-enrichment, so an
    // absent bit here means genuinely unknown, never a value to derive.
    entry.allocatedSizeKnown = (record.fixed.fieldMask & ffprotocol::kUsnFieldAllocatedSizeBytes) != 0;
    entry.allocatedSizeBytes = entry.allocatedSizeKnown ? record.fixed.allocatedSizeBytes : 0;
    entry.creationTime = record.fixed.creationTime;
    entry.lastModifiedTime = record.fixed.lastModifiedTime;
    entry.lastAccessTime = record.fixed.lastAccessTime;
    entry.attributes = record.fixed.fileAttributes;
    return entry;
}

std::optional<ffindexstore::EntryFieldMask> ToEntryFieldMask(ffprotocol::UsnDeltaFieldMask fields) {
    if ((fields & ~ffprotocol::kUsnFieldMaskAll) != 0) return std::nullopt;
    ffindexstore::EntryFieldMask result = 0;
    if ((fields & ffprotocol::kUsnFieldParentId) != 0) result |= ffindexstore::kEntryFieldParentId;
    if ((fields & ffprotocol::kUsnFieldName) != 0) result |= ffindexstore::kEntryFieldName;
    if ((fields & ffprotocol::kUsnFieldSizeBytes) != 0) result |= ffindexstore::kEntryFieldSizeBytes;
    if ((fields & ffprotocol::kUsnFieldCreationTime) != 0) result |= ffindexstore::kEntryFieldCreationTime;
    if ((fields & ffprotocol::kUsnFieldLastModifiedTime) != 0) result |= ffindexstore::kEntryFieldLastModifiedTime;
    if ((fields & ffprotocol::kUsnFieldLastAccessTime) != 0) result |= ffindexstore::kEntryFieldLastAccessTime;
    if ((fields & ffprotocol::kUsnFieldAttributes) != 0) result |= ffindexstore::kEntryFieldAttributes;
    // allocated-size-storage-accuracy 5.3: one-to-one wire-bit to
    // store-bit mapping; absence leaves the durable/projection value
    // untouched (UpsertPartial's preserve semantics).
    if ((fields & ffprotocol::kUsnFieldAllocatedSizeBytes) != 0) result |= ffindexstore::kEntryFieldAllocatedSizeBytes;
    return result;
}

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

} // namespace

bool IndexPipeline::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed) {
    lifecycleEpoch_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lock(mutex_);
    recordsSinceAnalyze_ = 0;
    projectionMemoryWarningIssued_ = false;
    if (!store_.Open(dbPathUtf8, outIntegrityFailed)) {
        return false;
    }
    ++generation_;
    return true;
}

void IndexPipeline::Close() {
    lifecycleEpoch_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lock(mutex_);
    store_.Close();
    ++generation_;
    recordsSinceAnalyze_ = 0;
    projectionMemoryWarningIssued_ = false;
}

void IndexPipeline::SetSearchTestHooks(SearchTestHooks hooks) {
    std::lock_guard<std::mutex> lock(mutex_);
    searchTestHooks_ = std::move(hooks);
}

void IndexPipeline::SetReadSessionFaultInjector(ffindexstore::ReadSessionFaultInjector injector) {
    std::lock_guard<std::mutex> lock(mutex_);
    readSessionFaultInjector_ = std::move(injector);
}

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
    return result;
}

void IndexPipeline::SetBulkScanActive(const bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    projection_.SetBulkScanActive(active);
    ++generation_;
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
            std::chrono::system_clock::now().time_since_epoch()).count());
        store_.VacuumIfDue(true, nowEpochSeconds, vacuumIntervalHours, vacuumSizeGrowthPercent);
    }
}

void IndexPipeline::MaybeAnalyzeAfterBulkInsertLocked(size_t recordCount) {
    if (recordCount == 0) {
        return;
    }
    const uint64_t count = static_cast<uint64_t>(recordCount);
    if (recordsSinceAnalyze_ > std::numeric_limits<uint64_t>::max() - count) {
        recordsSinceAnalyze_ = kAnalyzeRecordThreshold;
    } else {
        recordsSinceAnalyze_ += count;
    }
    if (recordsSinceAnalyze_ < kAnalyzeRecordThreshold) {
        return;
    }
    // address-engineering-audit-findings 5.1: ANALYZE runs only after the
    // durable batch commits. A failed maintenance operation never makes an
    // already-committed ingestion look failed; retaining the threshold makes
    // the next batch retry the statistics refresh.
    if (store_.Analyze()) {
        recordsSinceAnalyze_ %= kAnalyzeRecordThreshold;
    }
}

void IndexPipeline::MaybeReportProjectionMemoryPressureLocked() {
    const bool pressure = projection_.IsMemoryPressureHigh();
    if (pressure && !projectionMemoryWarningIssued_) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                      ffprotocol::DiagnosticCode::FFDIAG_PROJECTION_MEMORY_PRESSURE,
                                      {}, {}, L"projection-memory-pressure", kErrorNotEnoughMemory,
                                      projection_.EstimatedMemoryBytes()});
        projectionMemoryWarningIssued_ = true;
    } else if (!pressure) {
        projectionMemoryWarningIssued_ = false;
    }
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
        MaybeReportProjectionMemoryPressureLocked();
        ++generation_;
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
    if (id.has_value()) ++generation_;
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
    ++generation_;
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
    std::vector<ffindexstore::EntryChange> changes;
    changes.reserve(records.size());
    for (const auto& record : records) {
        changes.push_back({ffindexstore::EntryChangeKind::Upsert, ToEntryRecord(record)});
    }
    return ApplyMftBatchChanges(volumeId, std::move(changes));
}

bool IndexPipeline::ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV2>& records) {
    // allocated-size-storage-accuracy 5.3: identical to the V1 path; only
    // the record conversion differs (allocated size authoritative).
    std::vector<ffindexstore::EntryChange> changes;
    changes.reserve(records.size());
    for (const auto& record : records) {
        changes.push_back({ffindexstore::EntryChangeKind::Upsert, ToEntryRecord(record)});
    }
    return ApplyMftBatchChanges(volumeId, std::move(changes));
}

bool IndexPipeline::ApplyMftBatchChanges(ffindexstore::VolumeRowId volumeId,
                                         std::vector<ffindexstore::EntryChange>&& changes) {
    std::lock_guard<std::mutex> lock(mutex_);
    // design.md D4: commit to the durable store first; the projection is
    // only touched once that commit has actually succeeded.
    if (!store_.ApplyBatch(volumeId, changes)) {
        return false;
    }
    MaybeAnalyzeAfterBulkInsertLocked(changes.size());
    for (const auto& change : changes) {
        projection_.Upsert(volumeId, change.record);
    }
    MaybeReportProjectionMemoryPressureLocked();

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
    // close-independent-validation-gaps 4.6: compact at the batch boundary,
    // never once per record. CompactStorage rebuilds every projection index;
    // invoking it from Projection::Upsert turns a large volume scan into an
    // O(N) compaction storm once the bounded memory policy is reached.
    projection_.EnforceMemoryLimitNow();
    MaybeReportProjectionMemoryPressureLocked();
    ++generation_;
    return true;
}

bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& records) {
    std::vector<ffprotocol::UsnDeltaV2> upgraded;
    upgraded.reserve(records.size());
    for (const auto& record : records) upgraded.push_back(ffprotocol::UpgradeUsnDeltaV1(record));
    return ApplyUsnBatch(volumeId, upgraded);
}

bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV2>& records) {
    std::vector<ffindexstore::EntryChange> changes;
    changes.reserve(records.size());
    for (const auto& record : records) {
        ffindexstore::EntryChange change;
        change.kind = (record.fixed.reason & kUsnReasonFileDelete) != 0 ? ffindexstore::EntryChangeKind::Remove
                                                                         : ffindexstore::EntryChangeKind::Upsert;
        change.record = ToEntryRecord(record);
        if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
            const auto fields = ToEntryFieldMask(record.fixed.fieldMask);
            if (!fields) return false;
            change.fields = *fields;
        }
        changes.push_back(std::move(change));
    }
    return ApplyUsnBatchChanges(volumeId, std::move(changes));
}

bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV3>& records) {
    // allocated-size-storage-accuracy 5.3: identical to the V2 path; only
    // the record conversion differs (allocated-size presence bit maps to
    // kEntryFieldAllocatedSizeBytes, so absence preserves stored values).
    std::vector<ffindexstore::EntryChange> changes;
    changes.reserve(records.size());
    for (const auto& record : records) {
        ffindexstore::EntryChange change;
        change.kind = (record.fixed.reason & kUsnReasonFileDelete) != 0 ? ffindexstore::EntryChangeKind::Remove
                                                                         : ffindexstore::EntryChangeKind::Upsert;
        change.record = ToEntryRecord(record);
        if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
            const auto fields = ToEntryFieldMask(record.fixed.fieldMask);
            if (!fields) return false;
            change.fields = *fields;
        }
        changes.push_back(std::move(change));
    }
    return ApplyUsnBatchChanges(volumeId, std::move(changes));
}

bool IndexPipeline::ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
                                         std::vector<ffindexstore::EntryChange>&& changes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!store_.ApplyBatch(volumeId, changes)) {
        return false;
    }
    size_t upsertCount = 0;
    for (const auto& change : changes) {
        if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
            ++upsertCount;
        }
    }
    MaybeAnalyzeAfterBulkInsertLocked(upsertCount);
    for (const auto& change : changes) {
        if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
            projection_.UpsertPartial(volumeId, change.record, change.fields);
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
    projection_.EnforceMemoryLimitNow();
    MaybeReportProjectionMemoryPressureLocked();
    ++generation_;
    return true;
}

bool IndexPipeline::SetVolumeAvailable(ffindexstore::VolumeRowId id, bool available, uint64_t nowTimestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.SetVolumeAvailable(id, available, nowTimestamp);
    if (result) ++generation_;
    return result;
}

bool IndexPipeline::SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.SetScanCursor(id, cursor);
    if (result) ++generation_;
    return result;
}

bool IndexPipeline::MarkScanComplete(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.MarkScanComplete(id);
    if (result) ++generation_;
    return result;
}

bool IndexPipeline::SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.SetJournalPosition(id, journalId, resumeUsn);
    if (result) ++generation_;
    return result;
}

bool IndexPipeline::SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.SetLastReconciliationTime(id, nowTimestamp);
    if (result) ++generation_;
    return result;
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
    ++generation_;
    return true;
}

void IndexPipeline::BeginReconciliationPass(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    reconciliationSeen_[id].clear();
    ++generation_;
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
    ++generation_;
}

std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshot(
    ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    projection_.ReloadEvictedVolume(store_, id);
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
                // allocated-size-storage-accuracy 5.3/D6: carry the
                // projection's explicit known state into the snapshot.
                snapshotEntry.allocatedSizeBytes = child.allocatedSizeBytes;
                snapshotEntry.allocatedSizeKnown = child.allocatedSizeKnown;
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

std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshotBounded(
    ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix, size_t maxDirectories) {
    auto result = ExportDirectorySnapshot(id, rootPathPrefix);
    if (result.size() > maxDirectories) {
        // std::map iterates in case-insensitive path order; keeping the first
        // N keys retains the lexicographically shallowest/top-of-tree
        // directories deterministically. Deeper directories stay durable and
        // re-materialize through the normal request/watch path.
        auto firstDropped = result.begin();
        std::advance(firstDropped, static_cast<std::ptrdiff_t>(maxDirectories));
        result.erase(firstDropped, result.end());
    }
    return result;
}

std::optional<ffindexstore::Projection::FolderAggregate> IndexPipeline::GetFolderAggregate(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn) {
    std::unique_ptr<ffindexstore::StoreReadSession> reader;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!projection_.NeedsReload(volumeId)) {
            return projection_.GetFolderAggregate(volumeId, parentFrn);
        }
        // nonblocking-indexed-read-plane 3.3: a cold aggregate gets its own
        // WAL snapshot, so a recursive CTE cannot hold the writer pipeline
        // mutex while a broad evicted subtree is being summed.
        reader = store_.OpenReadSession({readSessionFaultInjector_});
    }
    if (!reader) {
        readerFailures_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    CounterLease aggregateReaderLease(activeDurableReaders_, peakDurableReaders_);
    (void)aggregateReaderLease;
    if (!reader->FindEntry(volumeId, parentFrn).has_value()) {
        if (reader->Failed()) readerFailures_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    const auto durable = reader->GetFolderAggregate(volumeId, parentFrn);
    if (reader->Failed()) {
        readerFailures_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    ffindexstore::Projection::FolderAggregate result;
    result.itemCount = durable.itemCount;
    result.totalSizeBytes = durable.totalSizeBytes;
    result.totalAllocatedSizeBytes = durable.totalAllocatedSizeBytes;
    result.allocatedBytesKnownCount = durable.allocatedBytesKnownCount;
    result.allocatedBytesTotalCount = durable.allocatedBytesTotalCount;
    result.complete = durable.allocatedBytesKnownCount == durable.allocatedBytesTotalCount;
    return result;
}

void IndexPipeline::SetProjectionMemoryBudget(const size_t maximumBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    projection_.SetMaximumMemoryBytes(maximumBytes);
    ++generation_;
}

ffindexstore::Projection::MemoryDiagnostics IndexPipeline::GetProjectionMemoryDiagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return projection_.GetMemoryDiagnostics();
}

bool IndexPipeline::IsVolumeEvicted(const ffindexstore::VolumeRowId volumeId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return projection_.NeedsReload(volumeId);
}

void IndexPipeline::UnloadVolumeFromProjection(const ffindexstore::VolumeRowId volumeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    projection_.EvictVolume(volumeId);
    ++generation_;
}

ffprotocol::IndexedSearchResponse IndexPipeline::SearchIndexedReadPlane(
    const ffprotocol::IndexedSearchRequest& request, SearchTimings* timings,
    const std::function<bool()>& cancelled) {
    using Clock = std::chrono::steady_clock;
    const auto totalStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    if (timings != nullptr) timings->cancelled = false;
    const bool detailedDiagnostics = timings != nullptr && timings->detailedDiagnostics;
    const bool projectionOnly = timings != nullptr && timings->projectionOnly;
    const auto setTotal = [&] {
        if (timings != nullptr) {
            timings->totalMs = std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
        }
    };
    ffprotocol::IndexedSearchResponse result;
    result.requestId = request.requestId;
    const uint64_t searchLifecycleEpoch = lifecycleEpoch_.load(std::memory_order_acquire);
    const auto isCancelled = [this, &cancelled, searchLifecycleEpoch] {
        return lifecycleEpoch_.load(std::memory_order_acquire) != searchLifecycleEpoch ||
               (cancelled && cancelled());
    };
    const auto finishCancelled = [&] {
        result.status = ffprotocol::IndexedSearchStatus::Cancelled;
        result.candidates.clear();
        cancelledSearches_.fetch_add(1, std::memory_order_relaxed);
        if (timings != nullptr) timings->cancelled = true;
        setTotal();
    };
    if (request.requestId == 0 || !ffprotocol::IsIndexedSearchScopeValid(request.scope) ||
        !ffprotocol::IsIndexedSearchSortValid(request.sortField) || request.query.empty() ||
        request.query.size() > ffprotocol::kMaxIndexedSearchQueryLengthChars ||
        request.scopePath.size() > ffprotocol::kMaxPathLengthChars || request.maxResults == 0) {
        result.status = ffprotocol::IndexedSearchStatus::InvalidRequest;
        setTotal();
        return result;
    }
    CounterLease activeSearchLease(activeSearches_, peakSearches_);
    (void)activeSearchLease;
    if (isCancelled()) {
        finishCancelled();
        return result;
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
    searchRequest.chunkSize = kHotCandidateChunkRows;
    if (timings != nullptr) {
        timings->queryParseMs =
            std::chrono::duration<double, std::milli>(Clock::now() - queryStart).count();
    }

    struct SearchContext {
        uint64_t generation = 0;
        std::unordered_map<ffindexstore::VolumeRowId, std::wstring> prefixes;
        std::unordered_set<ffindexstore::VolumeRowId> coldVolumes;
        std::vector<ffindexstore::VolumeRowId> orderedCold;
        std::unique_ptr<ffindexstore::StoreReadSession> reader;
        ffindexstore::ProjectionEntryCursor cursor;
        std::optional<std::vector<uint32_t>> filenameCandidates;
        std::function<void(SearchPhase)> phaseHook;
    };

    ffsearch::SearchResponse search;
    ffsearch::SearchDiagnostics searchDiagnostics;
    size_t generationRestarts = 0;
    size_t projectionRowsExamined = 0;
    size_t projectionEntriesVisited = 0;
    size_t namePoolLookups = 0;
    size_t emptyNamesRejected = 0;
    size_t nameViewsConstructed = 0;
    size_t candidateObjectsMaterialized = 0;
    size_t candidateNameCopies = 0;
    size_t coldRowsExamined = 0;
    size_t coldVolumesSearched = 0;
    double storeStreamMs = 0.0;
    double lazyPathMaterializationMs = 0.0;
    double candidateNameMaterializationMs = 0.0;
    double lockWaitMs = 0.0;
    double maxLockHoldMs = 0.0;
    bool searchedColdVolumes = false;
    bool unavailable = false;
    bool completed = false;

    for (size_t restart = 0; restart <= kMaxGenerationRestarts; ++restart) {
        if (isCancelled()) {
            finishCancelled();
            return result;
        }

        SearchContext context;
        bool contextReady = false;
        {
            const auto waitStart = Clock::now();
            std::unique_lock<std::mutex> lock(mutex_);
            lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
            const auto holdStart = Clock::now();
            if (!store_.IsOpen()) {
                unavailable = true;
            } else {
                context.generation = generation_;
                context.phaseHook = searchTestHooks_.onPhase;
                context.prefixes.reserve(volumeRules_.size() + 4);
                std::unordered_set<ffindexstore::VolumeRowId> disabledVolumes;
                disabledVolumes.reserve(volumeRules_.size());
                for (const auto& [volumeId, setting] : volumeRules_) {
                    if (!setting.enabled) {
                        disabledVolumes.insert(volumeId);
                        continue;
                    }
                    if (const std::wstring prefix = NormalizeVolumePrefix(setting.key); !prefix.empty()) {
                        context.prefixes.emplace(volumeId, prefix);
                    }
                }
                const auto metadataStart = timings != nullptr ? Clock::now() : Clock::time_point{};
                const auto metadata = store_.GetAllVolumesWithoutEntryCounts();
                if (timings != nullptr) {
                    timings->sqliteMetadataMs +=
                        std::chrono::duration<double, std::milli>(Clock::now() - metadataStart).count();
                }
                for (const auto& metadataEntry : metadata) {
                    if (!metadataEntry.available || disabledVolumes.contains(metadataEntry.rowId) ||
                        context.prefixes.contains(metadataEntry.rowId)) {
                        continue;
                    }
                    const wchar_t driveLetter = ResolveDriveLetterForVolumeKey(metadataEntry.key);
                    if (driveLetter != L'\0') {
                        std::wstring prefix;
                        prefix.push_back(static_cast<wchar_t>(towupper(driveLetter)));
                        prefix.push_back(L':');
                        context.prefixes.emplace(metadataEntry.rowId, std::move(prefix));
                    }
                }
                if (context.prefixes.empty()) {
                    unavailable = true;
                } else {
                    for (const auto& prefixEntry : context.prefixes) {
                        if (projection_.NeedsReload(prefixEntry.first)) {
                            context.coldVolumes.insert(prefixEntry.first);
                        }
                    }
                    context.orderedCold.assign(context.coldVolumes.begin(), context.coldVolumes.end());
                    std::sort(context.orderedCold.begin(), context.orderedCold.end());
                    searchedColdVolumes = !context.orderedCold.empty();

                    // Every query gets one independent snapshot. This keeps
                    // hot fallback path reads consistent with the bounded
                    // capture and prevents a cold stream from using the
                    // writer Store connection.
                    context.reader = store_.OpenReadSession({readSessionFaultInjector_});
                    if (!context.reader) {
                        readerFailures_.fetch_add(1, std::memory_order_relaxed);
                        unavailable = true;
                    } else {
                        context.cursor = projection_.BeginEntryCursor();
                        const bool filenameEligible = searchRequest.query.simpleLiteralName &&
                            searchRequest.query.simpleLiteralFolded.size() >= 3 && context.coldVolumes.empty();
                        if (filenameEligible) {
                            context.filenameCandidates = projection_.FilenameCandidateIndices(
                                searchRequest.query.simpleLiteralFolded, kMaximumFilenameIndexCandidates);
                        }
                        if (timings != nullptr) {
                            timings->filenameIndexUsed = context.filenameCandidates.has_value();
                            timings->filenameIndexCandidateCount = context.filenameCandidates.has_value()
                                ? context.filenameCandidates->size() : 0;
                        }
                        contextReady = true;
                    }
                }
            }
            const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
            maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
            lock.unlock();
        }
        if (unavailable || !contextReady) break;

        CounterLease durableReaderLease(activeDurableReaders_, peakDurableReaders_);
        (void)durableReaderLease;

        if (context.phaseHook) context.phaseHook(SearchPhase::WorkStarted);

        bool stale = false;
        bool streamFailed = false;
        bool matchingHookCalled = false;
        bool coldHookCalled = false;
        const auto markStaleIfChanged = [&] {
            const auto waitStart = Clock::now();
            std::unique_lock<std::mutex> lock(mutex_);
            lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
            const auto holdStart = Clock::now();
            const bool changed = generation_ != context.generation;
            const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
            maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
            lock.unlock();
            if (changed) stale = true;
        };
        const auto materialize = [&](ffsearch::Candidate& candidate) {
            if (isCancelled() || stale) return false;
            const auto volumeId = static_cast<ffindexstore::VolumeRowId>(candidate.volumeId);
            if (context.coldVolumes.contains(volumeId)) {
                const bool materialized = context.reader != nullptr && MaterializeCandidateFolderDurable(
                    *context.reader, context.prefixes, candidate, isCancelled);
                if (!materialized && context.reader != nullptr && context.reader->Failed()) {
                    streamFailed = true;
                } else if (!materialized && !isCancelled() && !stale) {
                    // A durable row whose parent chain cannot be reconstructed
                    // is not a complete response candidate. Do not silently
                    // omit it from a supposedly completed cold search.
                    streamFailed = true;
                }
                return materialized;
            }

            bool materialized = false;
            {
                const auto waitStart = Clock::now();
                std::unique_lock<std::mutex> lock(mutex_);
                lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
                const auto holdStart = Clock::now();
                if (generation_ == context.generation) {
                    materialized = MaterializeCandidateFolder(projection_, context.prefixes, candidate, isCancelled);
                } else {
                    stale = true;
                }
                const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
                maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
                lock.unlock();
            }
            if (materialized || stale || isCancelled()) return materialized;
            // A hot candidate can have an unavailable parent chain during a
            // partial rebuild. Resolve that bounded fallback from the same
            // query snapshot, outside the pipeline mutex.
            const bool durableMaterialized = context.reader != nullptr && MaterializeCandidateFolderDurable(
                *context.reader, context.prefixes, candidate, isCancelled);
            if (!durableMaterialized && context.reader != nullptr && context.reader->Failed()) {
                streamFailed = true;
            } else if (!durableMaterialized && !isCancelled() && !stale) {
                streamFailed = true;
            }
            return durableMaterialized;
        };

        const auto searchStart = Clock::now();
        ffsearch::SearchAccumulator accumulator(searchRequest, isCancelled,
                                                 (detailedDiagnostics || projectionOnly)
                                                     ? &searchDiagnostics : nullptr);
        bool hotExhausted = false;
        size_t filenameIndex = 0;
        while (!hotExhausted && !stale && !isCancelled()) {
            std::vector<ffsearch::Candidate> chunk;
            chunk.reserve(kHotCandidateChunkRows);
            {
                const auto waitStart = Clock::now();
                std::unique_lock<std::mutex> lock(mutex_);
                lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
                const auto holdStart = Clock::now();
                if (generation_ != context.generation) {
                    stale = true;
                } else {
                    while (chunk.size() < kHotCandidateChunkRows) {
                        const ffindexstore::ProjectionEntry* entry = nullptr;
                        if (context.filenameCandidates.has_value()) {
                            if (filenameIndex >= context.filenameCandidates->size()) {
                                hotExhausted = true;
                                break;
                            }
                            entry = &projection_.EntryAt((*context.filenameCandidates)[filenameIndex++]);
                        } else if (!context.cursor.Next(entry)) {
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
                const bool streamed = context.reader->ForEachEntryUntil(coldVolumeId,
                    [&](const ffindexstore::EntryRecord& record) {
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
                    });
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
                unavailable = true;
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
            timings->searchGeneration = context.generation;
            timings->generationRestarts = generationRestarts;
            timings->projectionTraversalMs =
                std::chrono::duration<double, std::milli>(Clock::now() - searchStart).count();
        }
        break;
    }

    if (unavailable || !completed) {
        unavailableSearches_.fetch_add(1, std::memory_order_relaxed);
        result.status = ffprotocol::IndexedSearchStatus::Unavailable;
        setTotal();
        return result;
    }
    if (isCancelled() || search.cancelled) {
        finishCancelled();
        return result;
    }
    if (timings != nullptr) {
        timings->stateLockWaitMs = lockWaitMs;
        timings->maxStateLockHoldMs = maxLockHoldMs;
        timings->candidateMaterializationMs = candidateNameMaterializationMs + lazyPathMaterializationMs;
        timings->lightweightCandidateMaterializationMs = candidateNameMaterializationMs;
        timings->lazyPathMaterializationMs = lazyPathMaterializationMs;
        timings->candidateCount = projectionRowsExamined + coldRowsExamined;
        timings->projectionEntriesVisited = projectionEntriesVisited;
        timings->projectionRowsExamined = projectionRowsExamined;
        timings->namePoolLookups = namePoolLookups;
        timings->nameViewsConstructed = nameViewsConstructed;
        timings->hotRowsExamined = projectionRowsExamined;
        timings->coldRowsExamined = coldRowsExamined;
        timings->coldVolumesSearched = coldVolumesSearched;
        timings->coldSearchUsed = searchedColdVolumes;
        timings->storeStreamMs = storeStreamMs;
        timings->candidateObjectsMaterialized = candidateObjectsMaterialized;
        timings->candidateNameCopies = candidateNameCopies;
        timings->candidatesVisited = search.candidatesVisited;
        timings->queryPredicateEvaluations = searchDiagnostics.match.predicateEvaluations;
        timings->queryTermEvaluations = searchDiagnostics.match.termEvaluations;
        timings->caseInsensitiveComparisons = searchDiagnostics.match.caseInsensitiveComparisons;
        timings->caseFoldOperations = searchDiagnostics.match.caseFoldOperations;
        timings->substringSearches = searchDiagnostics.match.substringSearches;
        timings->globMatches = searchDiagnostics.match.globMatches;
        timings->prefixChecks = searchDiagnostics.match.prefixChecks;
        timings->extensionPredicates = searchDiagnostics.match.extensionPredicates;
        timings->metadataPredicates = searchDiagnostics.match.metadataPredicates;
        timings->earlyRejects = searchDiagnostics.match.earlyRejects;
        timings->predicateMatches = searchDiagnostics.match.predicateMatches;
        timings->pathNeededDecisions = searchDiagnostics.pathNeededDecisions;
        timings->pathLookups = searchDiagnostics.pathLookups;
        timings->matchedRows = searchDiagnostics.matchedRows;
        timings->predicateDispatchMs = searchDiagnostics.match.predicateDispatchMs;
        timings->substringMatchingMs = searchDiagnostics.match.substringMatchingMs;
        timings->globMatchingMs = searchDiagnostics.match.globMatchingMs;
        timings->resultCount = search.results.size();
        timings->searchResultObjectsMaterialized = search.results.size();
        timings->truncated = search.truncated;
    }
    result.status = ffprotocol::IndexedSearchStatus::Completed;
    result.truncated = search.truncated;
    result.candidates.reserve(search.results.size());
    const auto responseStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    for (const auto& matched : search.results) {
        if (isCancelled()) {
            finishCancelled();
            return result;
        }
        const auto& candidate = matched.candidate;
        result.candidates.push_back({candidate.name, candidate.folder, candidate.sizeBytes,
                                     candidate.modifiedTime, candidate.isDirectory,
                                     candidate.createdTime, candidate.volumeId, candidate.id,
                                     candidate.parentId, candidate.persistentIndexed,
                                     candidate.allocatedSizeBytes, candidate.allocatedSizeKnown});
    }
    if (isCancelled()) {
        finishCancelled();
        return result;
    }
    completedSearches_.fetch_add(1, std::memory_order_relaxed);
    if (timings != nullptr) {
        timings->responseMaterializationMs =
            std::chrono::duration<double, std::milli>(Clock::now() - responseStart).count();
        setTotal();
    }
    return result;
}

ffprotocol::IndexedSearchResponse IndexPipeline::SearchIndexed(
    const ffprotocol::IndexedSearchRequest& request, SearchTimings* timings,
    const SearchCancellation& cancelled) {
    return SearchIndexedReadPlane(request, timings, cancelled);
}

} // namespace ffengine
