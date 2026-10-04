#include "IndexPipeline.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <limits>
#include <utility>

#include "ffprotocol/Diagnostics.h"
#include "ffsearch/Path.h"
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
// only from Windows engine code -- uses std::wstring. On Windows both are
// 16-bit UTF-16 code units; reinterpret-based range construction lets the
// string traits perform one bulk copy instead of a per-code-unit loop.
// This runs for every candidate row of every search chunk while holding the
// pipeline mutex, so the copy strategy is lock-hold-time sensitive.
std::wstring ToWString(std::u16string_view text) {
    return std::wstring(reinterpret_cast<const wchar_t*>(text.data()), text.size());
}

std::u16string ToU16String(std::wstring_view text) {
    return std::u16string(reinterpret_cast<const char16_t*>(text.data()), text.size());
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

// indexed-search-zero-copy-hot-path: a lifetime-safe, owner-less snapshot of
// one projection row captured while the pipeline mutex is held. The name is
// NOT stored as a view into NamePool (arena reallocation during Intern or a
// whole-pool replacement in CompactStorage would dangle it); only a stable
// NameId plus the name bytes copied into the owning chunk's contiguous buffer
// travel with the row. A borrowed CandidateView built from those bytes is valid
// only until the chunk (and therefore the buffer) is destroyed, which is why
// matching runs within the same bounded block that owns the buffer.
struct LightweightHotRow {
    ffindexstore::VolumeRowId volumeRowId = 0;
    ffindexstore::FileId frn{};
    ffindexstore::FileId parentFrn{};
    uint64_t sizeBytes = 0;
    uint64_t modifiedTime = 0;
    uint64_t createdTime = 0;
    uint64_t allocatedSizeBytes = 0;
    bool isDirectory = false;
    bool allocatedSizeKnown = false;
    ffindexstore::NameId nameId = 0;
    size_t nameOffset = 0;
    size_t nameLength = 0;
    bool nameIsAscii = false;
};

// indexed-search-zero-copy-hot-path: builds an owned Candidate from a
// lifetime-bounded lightweight row. The name is copied once from the
// search-owned chunk name buffer (not re-fetched from the mutable NamePool),
// so the candidate survives the projection lock release that follows capture.
ffsearch::Candidate MakeProjectionCandidateFromLightweight(
    const LightweightHotRow& row, std::u16string_view name, const std::wstring& prefix) {
    ffsearch::Candidate candidate;
    candidate.volumeId = static_cast<int64_t>(row.volumeRowId);
    candidate.id = row.frn.low;
    candidate.parentId = row.parentFrn.low;
    candidate.sizeBytes = row.sizeBytes;
    candidate.modifiedTime = row.modifiedTime;
    candidate.createdTime = row.createdTime;
    candidate.isDirectory = row.isDirectory;
    candidate.persistentIndexed = true;
    candidate.allocatedSizeBytes = row.allocatedSizeBytes;
    candidate.allocatedSizeKnown = row.allocatedSizeKnown;
    if (row.parentFrn == row.frn) {
        candidate.name = prefix + L"\\";
    } else {
        candidate.name = ToWString(name);
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

std::wstring CandidateCanonicalPath(const ffsearch::Candidate& candidate) {
    if (candidate.folder.empty()) return candidate.name;
    if (candidate.name.empty()) return candidate.folder;
    if (candidate.folder.back() == L'\\') return candidate.folder + candidate.name;
    return candidate.folder + L"\\" + candidate.name;
}

bool CandidateIncludedByRules(
    const std::unordered_map<ffindexstore::VolumeRowId, ffprotocol::VolumeSetting>& rules,
    const ffsearch::Candidate& candidate) {
    const auto it = rules.find(static_cast<ffindexstore::VolumeRowId>(candidate.volumeId));
    return it == rules.end() || ffprotocol::IsPathIncluded(it->second, CandidateCanonicalPath(candidate));
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

bool IndexPipeline::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed) {
    lifecycleEpoch_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lock(mutex_);
    recordsSinceAnalyze_ = 0;
    projectionMemoryWarningIssued_ = false;
    if (!store_.Open(dbPathUtf8, outIntegrityFailed)) {
        return false;
    }
    volumeDataEpochs_.clear();
    volumeConfigurationEpochs_.clear();
    AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::Lifecycle, true);
    return true;
}

void IndexPipeline::Close() {
    lifecycleEpoch_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lock(mutex_);
    store_.Close();
    volumeDataEpochs_.clear();
    volumeConfigurationEpochs_.clear();
    AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::Lifecycle, true);
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

#ifdef FF_INDEX_PIPELINE_TESTING
void IndexPipeline::SetSecondaryPruningFaultInjector(SecondaryPruningFaultInjector injector) {
    std::lock_guard<std::mutex> lock(mutex_);
    secondaryPruningFaultInjector_ = std::move(injector);
}

bool IndexPipeline::ShouldInjectSecondaryPruningFailureLocked(
    const SecondaryPruningBoundary boundary, const ffindexstore::VolumeRowId volumeId,
    const std::vector<ffindexstore::EntryChange>& changes) {
    return secondaryPruningFaultInjector_ &&
        secondaryPruningFaultInjector_(boundary, volumeId, changes);
}
#endif

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
            std::chrono::system_clock::now().time_since_epoch()).count());
        store_.VacuumIfDue(true, nowEpochSeconds, vacuumIntervalHours, vacuumSizeGrowthPercent);
    }
    RecordMetadataOnlyUpdateLocked();
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
    RecordMetadataOnlyUpdateLocked();
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
            bool rebuildSucceeded =
                projection_.RebuildVolumeFromStore(store_, meta.rowId, meta.entryCount);
            // zero-touch-autonomous-engineering (subtree gating): after the
            // projection is rebuilt from the durable store (parent chains
            // fully present), prune any excluded subtrees from store +
            // projection so the rebuilt index honors the configured rules.
            // This mirrors the ingestion prune and the reconciliation catch-
            // all, ensuring rules are honored on every equivalent ingestion
            // path (initial scan, USN deltas, reconciliation, rebuild).
            if (rebuildSucceeded && volumeRules_.find(meta.rowId) != volumeRules_.end()) {
                std::vector<ffindexstore::EntryChange> ruleExcluded;
                const bool ruleReadSucceeded = store_.ForEachEntry(meta.rowId, [&](const ffindexstore::EntryRecord& record) {
                    if (!IsEntryIncludedLocked(meta.rowId, record.id)) {
                        ruleExcluded.push_back({ffindexstore::EntryChangeKind::Remove, record});
                    }
                });
                if (!ruleReadSucceeded) {
                    // completion-integrity-hardening-and-release-certification 4.1.2:
                    // a failed second durable read cannot be treated as a
                    // complete rule-prune pass, even if it yielded a prefix.
                    rebuildSucceeded = false;
                } else if (!ruleExcluded.empty()) {
#ifdef FF_INDEX_PIPELINE_TESTING
                    const bool injectedFailure = ShouldInjectSecondaryPruningFailureLocked(
                        SecondaryPruningBoundary::RebuildRulePrune, meta.rowId, ruleExcluded);
#else
                    const bool injectedFailure = false;
#endif
                    bool secondaryApplied = false;
                    if (!injectedFailure) {
                        secondaryApplied = store_.ApplyBatch(meta.rowId, ruleExcluded);
                    }
                    if (secondaryApplied) {
                        for (const auto& removal : ruleExcluded) {
                            projection_.Remove(meta.rowId, removal.record.id);
                        }
                    } else {
                        // The projection was rebuilt from the unchanged durable
                        // rows, so retaining them is consistent. Do not publish
                        // this volume as a successful rebuild; the next rebuild
                        // will re-evaluate the same derived rule obligation.
                        rebuildSucceeded = false;
                    }
                }
            }
            if (rebuildSucceeded) {
                volumeIds.push_back(meta.rowId);
            }
        }
        MaybeReportProjectionMemoryPressureLocked();
        for (const auto volumeId : volumeIds) {
            AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::Rebuild);
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
    bool created = false;
    auto id = store_.GetOrCreateVolume(key, &created);
    if (id.has_value() && created) {
        AdvanceQueryVisibleEpochLocked(*id, QueryMutationReason::VolumeConfiguration, true);
    }
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
    const auto previous = volumeRules_.find(volumeId);
    if (previous != volumeRules_.end() && VolumeSettingsEqual(previous->second, setting)) {
        return;
    }
    volumeRules_[volumeId] = setting;
    AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::VolumeConfiguration, true);
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

    // Optimization: fuse the two rule-evaluation passes and cache the
    // inclusion result per record so each entry's ReconstructPath walk
    // runs at most once. When no rules are configured for this volume the
    // gating is a no-op (preserves pre-change behavior without any walks).
    const bool hasRulesForVolume = volumeRules_.find(volumeId) != volumeRules_.end();
    auto reconciling = reconciliationSeen_.find(volumeId);
    std::vector<ffindexstore::EntryChange> ruleExcluded;
    if (hasRulesForVolume) {
        ruleExcluded.reserve(changes.size() / 8 + 1);
        for (const auto& change : changes) {
            const bool included = IsEntryIncludedLocked(volumeId, change.record.id);
            if (reconciling != reconciliationSeen_.end() && included) {
                reconciling->second.insert(change.record.id);
            }
            if (!included) {
                ruleExcluded.push_back({ffindexstore::EntryChangeKind::Remove, change.record});
            }
        }
    } else if (reconciling != reconciliationSeen_.end()) {
        for (const auto& change : changes) {
            reconciling->second.insert(change.record.id);
        }
    }
    if (!ruleExcluded.empty()) {
#ifdef FF_INDEX_PIPELINE_TESTING
        const bool injectedFailure = ShouldInjectSecondaryPruningFailureLocked(
            SecondaryPruningBoundary::MftRulePrune, volumeId, ruleExcluded);
#else
        const bool injectedFailure = false;
#endif
        bool secondaryApplied = false;
        if (!injectedFailure) {
            secondaryApplied = store_.ApplyBatch(volumeId, ruleExcluded);
        }
        if (secondaryApplied) {
            for (const auto& removal : ruleExcluded) {
                projection_.Remove(volumeId, removal.record.id);
            }
        } else {
            // Secondary pruning failed: durable and projection remain consistent (both retain the excluded rows),
            // but do not report success. Caller can retry the batch; the next attempt will re-evaluate pruning.
            projection_.EnforceMemoryLimitNow();
            MaybeReportProjectionMemoryPressureLocked();
            return false;
        }
    }
    // invoking it from Projection::Upsert turns a large volume scan into an
    // O(N) compaction storm once the bounded memory policy is reached.
    projection_.EnforceMemoryLimitNow();
    MaybeReportProjectionMemoryPressureLocked();
    if (!changes.empty()) {
        AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::EntryMutation);
    }
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

    // Optimization: same fused single-walk evaluation as the MFT
    // path, but only Upsert USN records are gated (deletes are no-ops).
    // Skips all ReconstructPath walks when no rules exist for the volume.
    const bool hasRulesForUsnVolume = volumeRules_.find(volumeId) != volumeRules_.end();
    // index-durability-false-success-closure D3: while a reconciliation
    // pass is running, journal-batch upserts are "observed" exactly like
    // scan records -- otherwise a file created between the pass's scan
    // start and Finish is unseen, removed as stale, and never re-observed
    // (its USN event was already consumed). Rule-excluded upserts stay out
    // of the seen-set, matching the MFT path so Finish still prunes them.
    auto reconcilingUsn = reconciliationSeen_.find(volumeId);
    std::vector<ffindexstore::EntryChange> ruleExcluded;
    if (hasRulesForUsnVolume) {
        ruleExcluded.reserve(changes.size() / 8 + 1);
        for (const auto& change : changes) {
            if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
                if (IsEntryIncludedLocked(volumeId, change.record.id)) {
                    if (reconcilingUsn != reconciliationSeen_.end()) {
                        reconcilingUsn->second.insert(change.record.id);
                    }
                } else {
                    ruleExcluded.push_back({ffindexstore::EntryChangeKind::Remove, change.record});
                }
            }
        }
    } else if (reconcilingUsn != reconciliationSeen_.end()) {
        for (const auto& change : changes) {
            if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
                reconcilingUsn->second.insert(change.record.id);
            }
        }
    }
    if (!ruleExcluded.empty()) {
#ifdef FF_INDEX_PIPELINE_TESTING
        const bool injectedFailure = ShouldInjectSecondaryPruningFailureLocked(
            SecondaryPruningBoundary::UsnRulePrune, volumeId, ruleExcluded);
#else
        const bool injectedFailure = false;
#endif
        bool secondaryApplied = false;
        if (!injectedFailure) {
            secondaryApplied = store_.ApplyBatch(volumeId, ruleExcluded);
        }
        if (secondaryApplied) {
            for (const auto& removal : ruleExcluded) {
                projection_.Remove(volumeId, removal.record.id);
            }
        } else {
            projection_.EnforceMemoryLimitNow();
            MaybeReportProjectionMemoryPressureLocked();
            return false;
        }
    }
    projection_.EnforceMemoryLimitNow();
    MaybeReportProjectionMemoryPressureLocked();
    if (!changes.empty()) {
        AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::EntryMutation);
    }
    return true;
}

bool IndexPipeline::SetVolumeAvailable(ffindexstore::VolumeRowId id, bool available, uint64_t nowTimestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool visibilityChanged = false;
    const bool result = store_.SetVolumeAvailable(id, available, nowTimestamp, &visibilityChanged);
    if (result) {
        if (visibilityChanged) {
            AdvanceQueryVisibleEpochLocked(id, QueryMutationReason::VolumeVisibility, true);
        } else {
            RecordMetadataOnlyUpdateLocked();
        }
    }
    return result;
}

bool IndexPipeline::SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.SetScanCursor(id, cursor);
    if (result) RecordMetadataOnlyUpdateLocked();
    return result;
}

bool IndexPipeline::MarkScanComplete(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.MarkScanComplete(id);
    if (result) RecordMetadataOnlyUpdateLocked();
    return result;
}

bool IndexPipeline::SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.SetJournalPosition(id, journalId, resumeUsn);
    if (result) RecordMetadataOnlyUpdateLocked();
    return result;
}

bool IndexPipeline::SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = store_.SetLastReconciliationTime(id, nowTimestamp);
    if (result) RecordMetadataOnlyUpdateLocked();
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
    AdvanceQueryVisibleEpochLocked(id, QueryMutationReason::Forget, true);
    return true;
}

void IndexPipeline::BeginReconciliationPass(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    reconciliationSeen_[id].clear();
    RecordMetadataOnlyUpdateLocked();
}

bool IndexPipeline::IsReconciliationPassActive(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return reconciliationSeen_.find(id) != reconciliationSeen_.end();
}

void IndexPipeline::AbortReconciliationPass(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reconciliationSeen_.erase(id) != 0) {
        RecordMetadataOnlyUpdateLocked();
    }
}

void IndexPipeline::FinishReconciliationPass(ffindexstore::VolumeRowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = reconciliationSeen_.find(id);
    if (it == reconciliationSeen_.end()) {
        return;
    }
    const auto& seen = it->second;

    const bool hasRulesForReconcile = volumeRules_.find(id) != volumeRules_.end();
    std::vector<ffindexstore::EntryChange> removals;
    for (const auto& existingId : store_.ListEntryIds(id)) {
        const bool notSeen = seen.find(existingId) == seen.end();
        const bool excluded = hasRulesForReconcile && !IsEntryIncludedLocked(id, existingId);
        if (notSeen || excluded) {
            ffindexstore::EntryRecord marker;
            marker.id = existingId;
            removals.push_back({ffindexstore::EntryChangeKind::Remove, marker});
        }
    }

    bool searchableMutation = false;
    bool secondaryApplied = true;
    if (!removals.empty()) {
#ifdef FF_INDEX_PIPELINE_TESTING
        const bool injectedFailure = ShouldInjectSecondaryPruningFailureLocked(
            SecondaryPruningBoundary::ReconciliationRemoval, id, removals);
#else
        const bool injectedFailure = false;
#endif
        if (!injectedFailure) {
            secondaryApplied = store_.ApplyBatch(id, removals);
        } else {
            secondaryApplied = false;
        }
        if (secondaryApplied) {
            for (const auto& removal : removals) {
                projection_.Remove(id, removal.record.id);
            }
            searchableMutation = true;
        }
    }

    if (secondaryApplied || removals.empty()) {
        reconciliationSeen_.erase(it);
    } else {
        // Secondary durable removal failed: retain the current pass's obligation
        // so the next reconciliation or retry still owes the same deletions.
        // Do not advance searchable epoch; the failure is not a searchable mutation.
        searchableMutation = false;
    }

    if (searchableMutation) {
        AdvanceQueryVisibleEpochLocked(id, QueryMutationReason::EntryMutation);
    } else {
        RecordMetadataOnlyUpdateLocked();
    }
}

ffprotocol::PooledSnapshotMap IndexPipeline::ExportDirectorySnapshotBoundedPooled(
    ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix, size_t maxDirectories) {
    if (maxDirectories == 0) {
        return {};
    }
    // Bounded publication keeps only the lexicographically smallest N
    // directories (shallowest/top-of-tree) deterministically. To avoid
    // building the full N=200k map then discarding the tail, insert with
    // an in-flight bound: each insertion that would exceed maxDirectories
    // evicts the current largest key (map's rbegin), so final size never
    // exceeds maxDirectories and total work is O(D log max) not O(D log D).
    //
    // performance-closure-and-multisurface-acceleration 6.1: this is the
    // single export core. Entry names land in per-directory pools
    // (PooledSnapshotDirectoryBuilder) instead of per-entry owning
    // std::wstring allocations; the owning exports below are converting
    // wrappers so the traversal and the bounded-eviction semantics exist
    // exactly once.
    std::lock_guard<std::mutex> lock(mutex_);
    if (!projection_.ReloadEvictedVolume(store_, id)) return {};
    ffprotocol::PooledSnapshotMap result;
    projection_.ForEachEntry([&](const ffindexstore::EntryKey& key, const ffindexstore::ProjectionEntry& entry) {
        if (key.volumeRowId != id || (entry.attributes & kFileAttributeDirectory) == 0) {
            return;
        }
        auto pathResult = projection_.ReconstructPath(id, entry.frn);
        const std::wstring reconstructed = ToWString(pathResult.path);
        std::wstring fullPath;
        if (entry.parentFrn == entry.frn) {
            fullPath = rootPathPrefix;
        } else if (pathResult.reachedRoot) {
            const std::wstring relative = StripVolumeRootName(projection_, id, entry.frn, reconstructed);
            fullPath = rootPathPrefix;
            if (!relative.empty()) fullPath += L"\\" + relative;
        } else {
            fullPath = rootPathPrefix + L"\\" + reconstructed;
        }
        ffprotocol::PooledSnapshotDirectoryBuilder builder;
        builder.SetStatus(ffprotocol::DirectoryEnumerationStatus::Success);
        builder.SetIdentity(entry.volumeRowId, entry.frn.low, entry.frn.high,
                            entry.parentFrn.low, entry.parentFrn.high);
        if (std::span<const uint32_t> children = projection_.ChildIndices(id, entry.frn); !children.empty()) {
            for (uint32_t childIndex : children) {
                const auto& child = projection_.EntryAt(childIndex);
                // allocated-size-storage-accuracy 5.3/D6: carry the
                // projection's explicit known state into the snapshot.
                builder.AddEntry(
                    projection_.Names().Get(child.nameId),
                    (child.attributes & kFileAttributeDirectory) != 0,
                    child.sizeBytes,
                    child.allocatedSizeBytes,
                    child.allocatedSizeKnown,
                    child.attributes,
                    child.creationTime,
                    child.lastModifiedTime,
                    child.volumeRowId,
                    child.frn.low,
                    child.frn.high,
                    child.parentFrn.low,
                    child.parentFrn.high);
            }
        }
        result.emplace(std::move(fullPath), std::move(builder).Build());
        if (result.size() > maxDirectories) {
            auto largest = std::prev(result.end());
            result.erase(largest);
        }
    });
    return result;
}

namespace {

std::map<std::wstring, ffprotocol::SnapshotDirectory> ToOwningSnapshotMap(
    const ffprotocol::PooledSnapshotMap& pooled) {
    std::map<std::wstring, ffprotocol::SnapshotDirectory> result;
    for (const auto& [path, directory] : pooled) {
        ffprotocol::SnapshotDirectory owned;
        owned.status = directory.status;
        owned.volumeRowId = directory.volumeRowId;
        owned.directoryIdLow = directory.directoryIdLow;
        owned.directoryIdHigh = directory.directoryIdHigh;
        owned.parentIdLow = directory.parentIdLow;
        owned.parentIdHigh = directory.parentIdHigh;
        owned.entries.reserve(directory.entries.size());
        for (const auto& entry : directory.entries) {
            ffprotocol::SnapshotDirectoryEntry ownedEntry;
            ownedEntry.name = std::wstring(entry.name);
            ownedEntry.isDirectory = entry.isDirectory;
            ownedEntry.sizeBytes = entry.sizeBytes;
            ownedEntry.allocatedSizeBytes = entry.allocatedSizeBytes;
            ownedEntry.allocatedSizeKnown = entry.allocatedSizeKnown;
            ownedEntry.attributes = entry.attributes;
            ownedEntry.creationTime = entry.creationTime;
            ownedEntry.lastModifiedTime = entry.lastModifiedTime;
            ownedEntry.volumeRowId = entry.volumeRowId;
            ownedEntry.fileIdLow = entry.fileIdLow;
            ownedEntry.fileIdHigh = entry.fileIdHigh;
            ownedEntry.parentIdLow = entry.parentIdLow;
            ownedEntry.parentIdHigh = entry.parentIdHigh;
            owned.entries.push_back(std::move(ownedEntry));
        }
        result.emplace(path, std::move(owned));
    }
    return result;
}

}  // namespace

std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshot(
    ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix) {
    return ToOwningSnapshotMap(ExportDirectorySnapshotBoundedPooled(
        id, rootPathPrefix, (std::numeric_limits<size_t>::max)()));
}

std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshotBounded(
    ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix, size_t maxDirectories) {
    return ToOwningSnapshotMap(ExportDirectorySnapshotBoundedPooled(id, rootPathPrefix, maxDirectories));
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
    std::unordered_map<ffindexstore::VolumeRowId, bool> before;
    for (const auto& metadata : store_.GetAllVolumesWithoutEntryCounts()) {
        before.emplace(metadata.rowId, projection_.NeedsReload(metadata.rowId));
    }
    projection_.SetMaximumMemoryBytes(maximumBytes);
    bool residencyChanged = false;
    for (const auto& metadata : store_.GetAllVolumesWithoutEntryCounts()) {
        const bool after = projection_.NeedsReload(metadata.rowId);
        const auto beforeIt = before.find(metadata.rowId);
        if (beforeIt == before.end() || beforeIt->second != after) {
            residencyChanged = true;
            AdvanceQueryVisibleEpochLocked(metadata.rowId, QueryMutationReason::ProjectionResidency);
        }
    }
    if (!residencyChanged) RecordMetadataOnlyUpdateLocked();
}

ffindexstore::Projection::MemoryDiagnostics IndexPipeline::GetProjectionMemoryDiagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return projection_.GetMemoryDiagnostics();
}

// performance-closure-and-multisurface-acceleration 8.1: read-only
// projection memory decomposition for the memory benchmark. The lock is the
// same one every other projection read takes; nothing here mutates state.
ffindexstore::Projection::MemoryBreakdown IndexPipeline::GetProjectionMemoryBreakdown() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return projection_.GetMemoryBreakdown();
}

bool IndexPipeline::IsVolumeEvicted(const ffindexstore::VolumeRowId volumeId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return projection_.NeedsReload(volumeId);
}

void IndexPipeline::UnloadVolumeFromProjection(const ffindexstore::VolumeRowId volumeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool wasEvicted = projection_.NeedsReload(volumeId);
    projection_.EvictVolume(volumeId);
    if (!wasEvicted && projection_.NeedsReload(volumeId)) {
        AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::ProjectionResidency);
    } else {
        RecordMetadataOnlyUpdateLocked();
    }
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
        uint64_t queryVisibleEpoch = 0;
        uint64_t dependencySetEpoch = 0;
        bool allVolumes = false;
        bool ruleFilterActive = false;
        std::unordered_map<ffindexstore::VolumeRowId, std::wstring> prefixes;
        std::unordered_map<ffindexstore::VolumeRowId, ffprotocol::VolumeSetting> rules;
        std::vector<SearchDependency> dependencies;
        std::vector<ffindexstore::VolumeRowId> searchableVolumes;
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
    bool durableFallbackNeeded = false;
    bool durableFallbackUsed = false;
    bool durableFallbackSucceeded = false;
    size_t durableFallbackAttempts = 0;
    size_t durableFallbackRowsStreamed = 0;
    size_t relevantInvalidations = 0;
    // indexed-search-zero-copy-hot-path: bounded view-first capture counters.
    size_t hotLightweightRowsCaptured = 0;
    size_t hotRowsRejectedBeforeMaterialization = 0;
    size_t viewPathFallbackRows = 0;
    double lightweightCaptureMs = 0.0;
    double viewMatchingMs = 0.0;
    double survivorMaterializationMs = 0.0;
    bool viewPathUsed = false;
    // search-availability-under-index-churn D1/D2/D5: capture only the
    // searchable volumes relevant to this request, together with their
    // epochs, rules, residency, and one independent WAL snapshot.
    const auto captureContext = [&](SearchContext& context, const bool prepareProjection) {
        const auto waitStart = Clock::now();
        std::unique_lock<std::mutex> lock(mutex_);
        lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
        const auto holdStart = Clock::now();
        bool ready = false;
        if (!store_.IsOpen()) {
            unavailable = true;
        } else {
            context.queryVisibleEpoch = queryVisibleEpoch_.load(std::memory_order_acquire);
            context.dependencySetEpoch = dependencySetEpoch_.load(std::memory_order_acquire);
            context.allVolumes = searchRequest.scope == ffsearch::SearchScope::AllIndexedLocations;
            context.phaseHook = searchTestHooks_.onPhase;

            std::unordered_map<ffindexstore::VolumeRowId, std::wstring> configuredPrefixes;
            std::unordered_set<ffindexstore::VolumeRowId> disabledVolumes;
            configuredPrefixes.reserve(volumeRules_.size() + 4);
            disabledVolumes.reserve(volumeRules_.size());
            for (const auto& [volumeId, setting] : volumeRules_) {
                if (!setting.enabled) {
                    disabledVolumes.insert(volumeId);
                    continue;
                }
                if (const std::wstring prefix = NormalizeVolumePrefix(setting.key); !prefix.empty()) {
                    configuredPrefixes.emplace(volumeId, prefix);
                }
            }

            const auto metadataStart = timings != nullptr ? Clock::now() : Clock::time_point{};
            const auto metadata = store_.GetAllVolumesWithoutEntryCounts();
            if (timings != nullptr) {
                timings->sqliteMetadataMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - metadataStart).count();
            }
            context.prefixes.reserve(metadata.size());
            context.rules.reserve(metadata.size());
            context.searchableVolumes.reserve(metadata.size());
            for (const auto& metadataEntry : metadata) {
                // Availability is operational lifecycle state: indexed rows
                // remain queryable while a volume is offline so search can
                // provide retained results. Explicit VolumeSetting.enabled
                // remains the query-visibility switch.
                if (disabledVolumes.contains(metadataEntry.rowId)) continue;
                std::wstring prefix;
                const auto configuredPrefix = configuredPrefixes.find(metadataEntry.rowId);
                if (configuredPrefix != configuredPrefixes.end()) {
                    prefix = configuredPrefix->second;
                } else {
                    const wchar_t driveLetter = ResolveDriveLetterForVolumeKey(metadataEntry.key);
                    if (driveLetter != L'\0') {
                        prefix.push_back(static_cast<wchar_t>(towupper(driveLetter)));
                        prefix.push_back(L':');
                    }
                }
                if (prefix.empty() || (!context.allVolumes &&
                                       !ffsearch::SameVolume(searchRequest.scopePath, prefix))) {
                    continue;
                }
                context.prefixes.emplace(metadataEntry.rowId, std::move(prefix));
                const auto ruleIt = volumeRules_.find(metadataEntry.rowId);
                if (ruleIt != volumeRules_.end()) {
                    context.rules.emplace(metadataEntry.rowId, ruleIt->second);
                    if (!ruleIt->second.rules.empty()) context.ruleFilterActive = true;
                }
                context.searchableVolumes.push_back(metadataEntry.rowId);
            }
            std::sort(context.searchableVolumes.begin(), context.searchableVolumes.end());
            if (context.searchableVolumes.empty()) {
                unavailable = true;
            } else {
                context.dependencies.reserve(context.searchableVolumes.size());
                for (const auto volumeId : context.searchableVolumes) {
                    const auto dataIt = volumeDataEpochs_.find(volumeId);
                    const auto configurationIt = volumeConfigurationEpochs_.find(volumeId);
                    context.dependencies.push_back({
                        volumeId,
                        dataIt == volumeDataEpochs_.end() ? 0 : dataIt->second,
                        configurationIt == volumeConfigurationEpochs_.end() ? 0 : configurationIt->second});
                    if (prepareProjection && projection_.NeedsReload(volumeId)) {
                        context.coldVolumes.insert(volumeId);
                    }
                }
                context.orderedCold.assign(context.coldVolumes.begin(), context.coldVolumes.end());
                std::sort(context.orderedCold.begin(), context.orderedCold.end());
                searchedColdVolumes = searchedColdVolumes || !context.orderedCold.empty();
                context.reader = store_.OpenReadSession({readSessionFaultInjector_});
                if (!context.reader) {
                    readerFailures_.fetch_add(1, std::memory_order_relaxed);
                    unavailable = true;
                } else {
                    if (prepareProjection) {
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
                    }
                    ready = true;
                }
            }
        }
        const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
        maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
        lock.unlock();
        return ready;
    };

    const auto isContextCurrentLocked = [&](const SearchContext& context, const bool includeDataEpochs) {
        if (!store_.IsOpen()) return false;
        if (context.allVolumes && dependencySetEpoch_.load(std::memory_order_acquire) !=
                                     context.dependencySetEpoch) return false;
        for (const auto& dependency : context.dependencies) {
            const auto dataIt = volumeDataEpochs_.find(dependency.volumeId);
            const auto configurationIt = volumeConfigurationEpochs_.find(dependency.volumeId);
            const uint64_t dataEpoch = dataIt == volumeDataEpochs_.end() ? 0 : dataIt->second;
            const uint64_t configurationEpoch =
                configurationIt == volumeConfigurationEpochs_.end() ? 0 : configurationIt->second;
            if (configurationEpoch != dependency.configurationEpoch ||
                (includeDataEpochs && dataEpoch != dependency.dataEpoch)) {
                return false;
            }
        }
        return true;
    };

    for (size_t restart = 0; restart <= kMaxGenerationRestarts; ++restart) {
        if (isCancelled()) {
            finishCancelled();
            return result;
        }

        SearchContext context;
        bool contextReady = false;
        contextReady = captureContext(context, true);
        if (unavailable || !contextReady) break;

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
            const bool changed = !isContextCurrentLocked(context, true);
            const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
            maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
            lock.unlock();
            if (changed) {
                stale = true;
                ++relevantInvalidations;
            }
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
                if (isContextCurrentLocked(context, true)) {
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
        // indexed-search-zero-copy-hot-path: for view-compatible queries, capture
        // a lightweight row (scalars + stable NameId + name bytes in a chunk-
        // owned buffer) under the mutex and evaluate the cheap name/metadata
        // view predicate outside it. Rejected rows never get an owned Candidate
        // or a per-row name copy. Non-view-compatible or projectionOnly runs use
        // the canonical owned-candidate path untouched (survivors still go
        // through the identical accumulator.Add, so result semantics are unchanged).
        const bool useViewHotPath = !projectionOnly && searchRequest.query.viewCompatible;
        const bool folderNeededForView =
            (searchRequest.scope != ffsearch::SearchScope::AllIndexedLocations) ||
            searchRequest.query.requiresFolder || searchRequest.candidateFilterRequiresFolder;
        while (!hotExhausted && !stale && !isCancelled()) {
            if (useViewHotPath) {
                std::vector<LightweightHotRow> rows;
                rows.reserve(kHotCandidateChunkRows);
                std::u16string nameBuffer;
                {
                    const auto waitStart = Clock::now();
                    std::unique_lock<std::mutex> lock(mutex_);
                    lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
                    const auto holdStart = Clock::now();
                    if (!isContextCurrentLocked(context, true)) {
                        stale = true;
                    } else {
                        const auto captureStart = Clock::now();
                        while (rows.size() < kHotCandidateChunkRows) {
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
                            LightweightHotRow row;
                            row.volumeRowId = entry->volumeRowId;
                            row.frn = entry->frn;
                            row.parentFrn = entry->parentFrn;
                            row.sizeBytes = entry->sizeBytes;
                            row.modifiedTime = entry->lastModifiedTime;
                            row.createdTime = entry->creationTime;
                            row.isDirectory = (entry->attributes & kFileAttributeDirectory) != 0;
                            row.allocatedSizeBytes = entry->allocatedSizeBytes;
                            row.allocatedSizeKnown = entry->allocatedSizeKnown;
                            row.nameId = entry->nameId;
                            row.nameIsAscii = entry->nameIsAscii;
                            if (entry->parentFrn != entry->frn) {
                                ++namePoolLookups;
                                const std::u16string_view name = projection_.Names().Get(entry->nameId);
                                if (name.empty()) {
                                    // Checked before the append: an empty
                                    // interned name may carry a null data()
                                    // pointer, and appending it is formally
                                    // undefined even for a zero count.
                                    if (detailedDiagnostics) ++emptyNamesRejected;
                                    continue;
                                }
                                row.nameOffset = nameBuffer.size();
                                row.nameLength = name.size();
                                nameBuffer.append(name.data(), name.size());
                            } else {
                                row.nameOffset = nameBuffer.size();
                                row.nameLength = 0;
                            }
                            ++nameViewsConstructed;
                            rows.push_back(std::move(row));
                        }
                        lightweightCaptureMs += std::chrono::duration<double, std::milli>(
                            Clock::now() - captureStart).count();
                        hotLightweightRowsCaptured += rows.size();
                        viewPathUsed = true;
                    }
                    const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
                    maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
                    lock.unlock();
                }
                if (!matchingHookCalled) {
                    if (context.phaseHook) context.phaseHook(SearchPhase::Matching);
                    matchingHookCalled = true;
                }
                if (!projectionOnly) {
                    double chunkPureViewMs = 0.0;
                    for (const auto& row : rows) {
                        if (stale || isCancelled() || accumulator.Cancelled()) break;
                        const std::u16string_view name =
                            (row.parentFrn == row.frn)
                                ? std::u16string_view()
                                : std::u16string_view(nameBuffer).substr(row.nameOffset, row.nameLength);
                        ffsearch::CandidateView view;
                        view.name = name;
                        view.sizeBytes = row.sizeBytes;
                        view.modifiedTime = row.modifiedTime;
                        view.isDirectory = row.isDirectory;
                        view.createdTime = row.createdTime;
                        view.volumeId = static_cast<int64_t>(row.volumeRowId);
                        view.id = row.frn.low;
                        view.parentId = row.parentFrn.low;
                        view.persistentIndexed = true;
                        view.nameAsciiLower = false;
                        view.allocatedSizeBytes = row.allocatedSizeBytes;
                        view.allocatedSizeKnown = row.allocatedSizeKnown;
                        view.nameIsAscii = row.nameIsAscii;
                        const auto viewPredStart = Clock::now();
                        const bool viewAccepted = folderNeededForView
                            ? searchRequest.query.MatchesViewWithoutFolder(view, nullptr)
                            : searchRequest.query.MatchesView(view, nullptr);
                        chunkPureViewMs += std::chrono::duration<double, std::milli>(
                            Clock::now() - viewPredStart).count();
                        if (!viewAccepted) {
                            ++hotRowsRejectedBeforeMaterialization;
                            continue;
                        }
                        // performance-closure-and-multisurface-acceleration 7.3:
                        // when the view evaluated the FULL predicate set (no
                        // folder-dependent evaluation), a strictly worse
                        // primary key can be admitted out before any
                        // materialization, and Add need not re-evaluate the
                        // query predicates on the owned candidate.
                        //
                        // completion-review R1: a volume-root row's view name is
                        // EMPTY while its materialized candidate uses the display
                        // name `prefix + "\\"` (e.g. "Z:\\"). The admission
                        // compares view names, so it must never judge a root row --
                        // a descending filename search otherwise discards the root
                        // that correctly wins. Root rows also take the full Add
                        // path so no view-based skip applies to them at all.
                        const bool fullViewEvaluation = !folderNeededForView;
                        const bool isVolumeRoot = (row.parentFrn == row.frn);
                        if (fullViewEvaluation && !isVolumeRoot && !accumulator.AdmitView(view)) {
                            ++hotRowsRejectedBeforeMaterialization;
                            continue;
                        }
                        const auto survivorStart = Clock::now();
                        const auto& prefix = context.prefixes.at(row.volumeRowId);
                        ffsearch::Candidate candidate =
                            MakeProjectionCandidateFromLightweight(row, name, prefix);
                        ++candidateNameCopies;
                        const bool continueSearch = (fullViewEvaluation && !isVolumeRoot)
                            ? accumulator.AddViewMatched(std::move(candidate), materialize)
                            : accumulator.Add(std::move(candidate), materialize);
                        if (!continueSearch) break;
                        ++candidateObjectsMaterialized;
                        survivorMaterializationMs += std::chrono::duration<double, std::milli>(
                            Clock::now() - survivorStart).count();
                        if (stale || isCancelled() || accumulator.Cancelled()) break;
                    }
                    viewMatchingMs += chunkPureViewMs;
                }
                if (stale || isCancelled() || accumulator.Cancelled() || streamFailed) break;
            } else {
                std::vector<ffsearch::Candidate> chunk;
                chunk.reserve(kHotCandidateChunkRows);
                {
                    const auto waitStart = Clock::now();
                    std::unique_lock<std::mutex> lock(mutex_);
                    lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
                    const auto holdStart = Clock::now();
                    if (!isContextCurrentLocked(context, true)) {
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
                const bool streamed = context.reader->ForEachEntryUntil(volumeId,
                    [&](const ffindexstore::EntryRecord& record) {
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
                    });
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
            completed = true;
            durableFallbackSucceeded = true;
            successfulDurableFallbacks_.fetch_add(1, std::memory_order_relaxed);
            recordFallbackTimings(context.queryVisibleEpoch, context.dependencySetEpoch);
            if (timings != nullptr) {
                timings->projectionTraversalMs =
                    std::chrono::duration<double, std::milli>(Clock::now() - searchStart).count();
            }
            break;
        }
        if (!durableFallbackSucceeded && !isCancelled()) {
            failedDurableFallbacks_.fetch_add(1, std::memory_order_relaxed);
            unavailable = true;
        }
        if (timings != nullptr) {
            recordFallbackTimings();
            timings->projectionTraversalMs =
                std::chrono::duration<double, std::milli>(Clock::now() - fallbackStart).count();
        }
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
        timings->relevantInvalidations = relevantInvalidations;
        timings->durableFallbackUsed = durableFallbackUsed;
        timings->durableFallbackAttempts = durableFallbackAttempts;
        timings->durableFallbackRowsStreamed = durableFallbackRowsStreamed;
        timings->durableFallbackSucceeded = durableFallbackSucceeded;
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
        // indexed-search-zero-copy-hot-path: view-first capture attribution.
        timings->hotLightweightRowsCaptured = hotLightweightRowsCaptured;
        timings->hotRowsRejectedBeforeMaterialization = hotRowsRejectedBeforeMaterialization;
        timings->viewPathFallbackRows = viewPathFallbackRows;
        timings->lightweightCaptureMs = lightweightCaptureMs;
        timings->viewMatchingMs = viewMatchingMs;
        timings->survivorMaterializationMs = survivorMaterializationMs;
        timings->viewPathUsed = viewPathUsed;
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
