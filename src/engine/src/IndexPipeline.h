#pragma once
#include <atomic>
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
#include "ffprotocol/Settings.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"
#include "ffsearch/Search.h"

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
    // nonblocking-indexed-read-plane 2.1: deterministic test seam used to
    // park query work at a named phase. Production callers leave the hook
    // empty; the fixed implementation invokes it only after long-running
    // work has released the mutation/lifecycle critical section.
    enum class SearchPhase : uint8_t {
        WorkStarted,
        Matching,
        ColdStreaming,
        DurableFallback,
        GenerationValidation,
    };
    struct SearchTestHooks {
        std::function<void(SearchPhase)> onPhase;
    };

    // Optional attribution seam for the explicit scalability benchmark. The
    // normal product path passes no timing object and remains behaviorally
    // unchanged; the fields describe the real SearchIndexed stages rather
    // than adding production logging.
    struct SearchTimings {
        bool detailedDiagnostics = false;
        bool projectionOnly = false;
        double sqliteMetadataMs = 0.0;
        double projectionTraversalMs = 0.0;
        double projectionEnumerationMs = 0.0;
        double namePoolLookupMs = 0.0;
        double nameViewConstructionMs = 0.0;
        double candidateMaterializationMs = 0.0;
        double lightweightCandidateMaterializationMs = 0.0;
        double lazyPathMaterializationMs = 0.0;
        double queryParseMs = 0.0;
        double matchFilterSortMs = 0.0;
        double predicateDispatchMs = 0.0;
        double substringMatchingMs = 0.0;
        double globMatchingMs = 0.0;
        double extensionMatchingMs = 0.0;
        double metadataMatchingMs = 0.0;
        double folderMatchingMs = 0.0;
        double matchedRowHandlingMs = 0.0;
        double responseMaterializationMs = 0.0;
        double totalMs = 0.0;
        size_t candidateCount = 0;
        size_t projectionEntriesVisited = 0;
        size_t projectionRowsExamined = 0;
        size_t namePoolLookups = 0;
        size_t emptyNamesRejected = 0;
        size_t nameViewsConstructed = 0;
        size_t candidateObjectsMaterialized = 0;
        size_t candidateNameCopies = 0;
        size_t candidatesVisited = 0;
        size_t queryPredicateEvaluations = 0;
        size_t queryTermEvaluations = 0;
        size_t caseInsensitiveComparisons = 0;
        size_t caseFoldOperations = 0;
        size_t substringSearches = 0;
        size_t globMatches = 0;
        size_t prefixChecks = 0;
        size_t extensionPredicates = 0;
        size_t metadataPredicates = 0;
        size_t earlyRejects = 0;
        size_t predicateMatches = 0;
        size_t pathNeededDecisions = 0;
        size_t pathLookups = 0;
        size_t matchedRows = 0;
        size_t resultCount = 0;
        size_t searchResultObjectsMaterialized = 0;
        size_t lazyPathMaterializationCount = 0;
        size_t filenameIndexCandidateCount = 0;
        bool filenameIndexUsed = false;
        // memory-bounded-indexed-scale 3.2: hot/cold attribution for the
        // bounded-projection search path. Cold rows stream from the durable
        // store; they never pass through a projection reload.
        size_t hotRowsExamined = 0;
        size_t coldRowsExamined = 0;
        size_t coldVolumesSearched = 0;
        double storeStreamMs = 0.0;
        bool coldSearchUsed = false;
        bool truncated = false;
        bool cancelled = false;
        uint64_t searchGeneration = 0;
        uint64_t queryVisibleEpoch = 0;
        uint64_t dependencySetEpoch = 0;
        size_t generationRestarts = 0;
        size_t relevantInvalidations = 0;
        size_t durableFallbackAttempts = 0;
        size_t durableFallbackRowsStreamed = 0;
        bool durableFallbackUsed = false;
        bool durableFallbackSucceeded = false;
        double stateLockWaitMs = 0.0;
        double maxStateLockHoldMs = 0.0;
        // indexed-search-zero-copy-hot-path 7.x: view-first lightweight capture.
        // Timing attribution is a non-overlapping partition:
        //  - lightweightCaptureMs = bounded name-buffer capture under mutex
        //  - viewMatchingMs = pure view predicate (MatchesView*) time outside lock, excluding survivor work
        //  - survivorMaterializationMs = survivor Upgrade (MakeProjectionCandidate + accumulator Add) outside lock
        size_t hotLightweightRowsCaptured = 0;
        size_t hotRowsRejectedBeforeMaterialization = 0;
        size_t viewPathFallbackRows = 0;
        double lightweightCaptureMs = 0.0;
        double viewMatchingMs = 0.0;
        double survivorMaterializationMs = 0.0;
        bool viewPathUsed = false;
    };

    struct ReadPlaneDiagnostics {
        size_t activeSearches = 0;
        size_t peakSearches = 0;
        size_t activeDurableReaders = 0;
        size_t peakDurableReaders = 0;
        uint64_t completedSearches = 0;
        uint64_t cancelledSearches = 0;
        uint64_t unavailableSearches = 0;
        uint64_t readerFailures = 0;
        uint64_t generationRestarts = 0;
        uint64_t queryVisibleEpoch = 0;
        uint64_t dependencySetEpoch = 0;
        uint64_t queryVisibleInvalidations = 0;
        uint64_t metadataOnlyUpdates = 0;
        uint64_t metadataOnlyInvalidationsAvoided = 0;
        uint64_t durableFallbacks = 0;
        uint64_t successfulDurableFallbacks = 0;
        uint64_t failedDurableFallbacks = 0;
        uint64_t durableFallbackRowsStreamed = 0;
    };

    // outIntegrityFailed (optional, task 1.7): set to true when the open
    // failed specifically because the existing file failed its integrity
    // check, so the caller can apply the rebuild-from-fresh-scan fallback
    // (delete the file and reopen) rather than treating it as a generic
    // I/O failure.
    bool Open(const std::string& dbPathUtf8, bool* outIntegrityFailed = nullptr);
    void Close();

    // nonblocking-indexed-read-plane 2.1: test-only phase injection. The
    // callback is never invoked when no hook is installed.
    void SetSearchTestHooks(SearchTestHooks hooks);

    // nonblocking-indexed-read-plane 3.4: portable fault injection for the
    // per-query durable reader. Production callers leave this empty; a
    // forced reader failure must surface as Unavailable rather than a partial
    // completed response.
    void SetReadSessionFaultInjector(ffindexstore::ReadSessionFaultInjector injector);
#ifdef FF_INDEX_PIPELINE_TESTING
    // rule-pruning-durability-and-reconciliation-failure-closure D3: the
    // deterministic fault seam names the secondary transaction boundary so a
    // regression test cannot accidentally exercise a different caller.
    enum class SecondaryPruningBoundary : uint8_t {
        MftRulePrune,
        UsnRulePrune,
        RebuildRulePrune,
        ReconciliationRemoval,
    };
    using SecondaryPruningFaultInjector = std::function<bool(
        SecondaryPruningBoundary, ffindexstore::VolumeRowId,
        const std::vector<ffindexstore::EntryChange>&)>;
    // Test-only: production builds do not expose or compile this mutable fault
    // surface. The callback is invoked while the pipeline mutation lock is held.
    void SetSecondaryPruningFaultInjector(SecondaryPruningFaultInjector injector);
#endif
    ReadPlaneDiagnostics GetReadPlaneDiagnostics() const noexcept;

    // task 1.6: scheduled WAL maintenance, intended to be called on a
    // low-frequency timer (the engine calls it from VolumeSessionManager's
    // ReconciliationSchedulerLoop poll). Runs a cheap passive checkpoint
    // every call and escalates to a forced checkpoint once the WAL has
    // grown past the threshold (design.md "Risks": unbounded WAL growth
    // under sustained write bursts). No-op if the store was never opened.
    void RunStoreMaintenance(bool userIdle = false, uint64_t vacuumIntervalHours = 24,
                             uint64_t vacuumSizeGrowthPercent = 10);

    // task 3.1/3.2: rebuilds every volume already known to the durable
    // store, invoking onVolumeRebuilt once per volume whose rebuild and rule
    // pruning both complete successfully. A volume whose pruning transaction
    // fails remains retryable and is deliberately not reported as complete.
    using VolumeRebuiltCallback = std::function<void(ffindexstore::VolumeRowId)>;
    void RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt);

    // close-independent-validation-gaps 4.6: initial and reconciliation
    // scans are bulk projection mutations. Their durable writes continue
    // normally, while the projection defers compaction and its optional
    // filename accelerator until the bulk scan is no longer active.
    void SetBulkScanActive(bool active);

    ffindexstore::VolumeRowId ResolveVolume(const ffindexstore::VolumeKey& key);
    std::optional<ffindexstore::VolumeMetadata> GetVolumeMetadata(ffindexstore::VolumeRowId id);
    std::vector<ffindexstore::VolumeMetadata> GetAllVolumes();

    // zero-touch-autonomous-engineering (settings-and-appearance 2.5 /
    // subtree gating): stores the per-volume include/exclude rules + drive-
    // letter prefix the ingestion pipeline uses to decide IsPathIncluded.
    // A volume with no rules (or not yet configured) includes everything,
    // preserving the behavior prior to this change. Called by
    // VolumeSessionManager whenever a session is established or the persisted
    // configuration changes (ReloadConfiguration), so rule changes take effect
    // on subsequent ingestion and the next reconciliation pass without an
    // engine restart.
    void SetVolumeRules(ffindexstore::VolumeRowId volumeId, const ffprotocol::VolumeSetting& setting);

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
    // allocated-size-storage-accuracy 5.3: V2 scan records (additive
    // allocated size) applied with the full-record mask, exactly like V1.
    bool ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV2>& records);
    // USN_REASON_FILE_DELETE-flagged records are applied as removals;
    // everything else is an upsert (tasks.md 6.1's engine-side consumer).
    bool ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& records);
    bool ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV2>& records);
    // allocated-size-storage-accuracy 5.3: V3 USN records (additive
    // allocated-size presence bit) applied with per-field presence masks.
    bool ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV3>& records);

private:
    // Shared commit-then-apply tail for scan batches (allocated-size-
    // storage-accuracy refactor: the V1/V2 overloads differ only in record
    // conversion, so the durable-commit/projection/reconciliation logic
    // lives here exactly once).
    bool ApplyMftBatchChanges(ffindexstore::VolumeRowId volumeId,
                              std::vector<ffindexstore::EntryChange>&& changes);
    // Shared commit-then-apply tail for USN batches.
    bool ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
                              std::vector<ffindexstore::EntryChange>&& changes);

#ifdef FF_INDEX_PIPELINE_TESTING
    bool ShouldInjectSecondaryPruningFailureLocked(
        SecondaryPruningBoundary boundary, ffindexstore::VolumeRowId volumeId,
        const std::vector<ffindexstore::EntryChange>& changes);
#endif

    ffprotocol::IndexedSearchResponse SearchIndexedReadPlane(
        const ffprotocol::IndexedSearchRequest& request,
        SearchTimings* timings, const std::function<bool()>& cancelled);

public:
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
    // index-durability-false-success-closure D3/D5: discard an in-flight
    // pass without finalizing -- no removals, no epoch change. Used when
    // the scan that was supplying the pass's seen-set turned out not to
    // have run to completion (ScanFailed, batch-apply failure, or pause),
    // so its partial coverage can never be mistaken for the whole volume.
    void AbortReconciliationPass(ffindexstore::VolumeRowId id);
    bool IsReconciliationPassActive(ffindexstore::VolumeRowId id);

    // Converts the current projection state for one volume into the
    // existing directory-listing snapshot wire format (SnapshotFormat.h),
    // keyed by full reconstructed path under `rootPathPrefix` (e.g.
    // L"C:"), so it can be published through the already-established
    // committed-generation snapshot mechanism (tasks.md 3.5) without
    // inventing a new wire format for this change.
    std::map<std::wstring, ffprotocol::SnapshotDirectory> ExportDirectorySnapshot(
        ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix);

    // memory-bounded-indexed-scale 5.1: bulk indexed publication (startup
    // rebuild, scan-complete) exports at most maxDirectories entries in the
    // map's deterministic path-sorted order -- i.e., the lexicographically
    // shallowest/top-of-tree directories survive. Deeper directories remain
    // durable and re-materialize through the normal request path, so published
    // bytes scale with the browse working set rather than the whole tree.
    static constexpr size_t kInitialIndexedExportDirectories = 4096;
    std::map<std::wstring, ffprotocol::SnapshotDirectory> ExportDirectorySnapshotBounded(
        ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix,
        size_t maxDirectories);

    // file-preview-and-properties §6.2 / storage-analysis §3.1: returns the
    // subtree aggregate for `parentFrn` from the in-memory projection if
    // the folder is known, or std::nullopt if it is not.
    std::optional<ffindexstore::Projection::FolderAggregate> GetFolderAggregate(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn);

    // memory-bounded-indexed-scale 3.x: bounded-projection seams. The budget
    // override applies synchronously and strictly (like
    // Projection::SetMaximumMemoryBytes), so tests -- and any future settings
    // surface -- can force deterministic eviction. The diagnostics/volume
    // residency accessors back observability without exposing the projection.
    void SetProjectionMemoryBudget(size_t maximumBytes);
    ffindexstore::Projection::MemoryDiagnostics GetProjectionMemoryDiagnostics() const;
    bool IsVolumeEvicted(ffindexstore::VolumeRowId volumeId) const;
    // memory-bounded-indexed-scale 5.x: explicit policy hook dropping one
    // volume's resident rows (durable data untouched). Also the deterministic
    // forced-eviction seam for the bounded-scale test suite.
    void UnloadVolumeFromProjection(ffindexstore::VolumeRowId volumeId);

    // Executes a bounded query against the durable in-memory projection. The
    // returned candidates carry reconstructed parent folders so the UI can
    // navigate a result without requiring the entire index over IPC. The
    // projection lock is held while copying lightweight candidate data;
    // matching and sorting happen after that copy, and lazy folder
    // reconstruction reacquires the lock only for path-dependent survivors.
    using SearchCancellation = std::function<bool()>;
    ffprotocol::IndexedSearchResponse SearchIndexed(const ffprotocol::IndexedSearchRequest& request,
                                                    SearchTimings* timings = nullptr,
                                                    const SearchCancellation& cancelled = {});

private:
    enum class QueryMutationReason : uint8_t {
        EntryMutation,
        VolumeConfiguration,
        VolumeVisibility,
        Rebuild,
        Forget,
        ProjectionResidency,
        Lifecycle,
    };

    struct SearchDependency {
        ffindexstore::VolumeRowId volumeId = 0;
        uint64_t dataEpoch = 0;
        uint64_t configurationEpoch = 0;
    };

    static constexpr size_t kHotCandidateChunkRows = 256;
    // indexed-search-zero-copy-hot-path: batch size for the lightweight
    // view-first capture chunk (mirrors kHotCandidateChunkRows).
    static constexpr size_t kViewCandidateChunkRows = kHotCandidateChunkRows;
    static constexpr size_t kMaxGenerationRestarts = 3;
    static constexpr size_t kMaxDurableFallbackConfigurationRetries = 2;
    static constexpr size_t kMaximumFilenameIndexCandidates = 16 * 1024;

    void AdvanceQueryVisibleEpochLocked(ffindexstore::VolumeRowId volumeId,
                                        QueryMutationReason reason,
                                        bool dependencySetChanged = false);
    void RecordMetadataOnlyUpdateLocked();
    static bool VolumeSettingsEqual(const ffprotocol::VolumeSetting& left,
                                    const ffprotocol::VolumeSetting& right);

    mutable std::mutex mutex_;
    ffindexstore::Store store_;
    ffindexstore::Projection projection_;
    SearchTestHooks searchTestHooks_;
    ffindexstore::ReadSessionFaultInjector readSessionFaultInjector_;
#ifdef FF_INDEX_PIPELINE_TESTING
    SecondaryPruningFaultInjector secondaryPruningFaultInjector_;
#endif
    std::atomic<uint64_t> queryVisibleEpoch_{1};
    std::atomic<uint64_t> dependencySetEpoch_{1};
    std::unordered_map<ffindexstore::VolumeRowId, uint64_t> volumeDataEpochs_;
    std::unordered_map<ffindexstore::VolumeRowId, uint64_t> volumeConfigurationEpochs_;
    std::atomic<size_t> activeSearches_{0};
    std::atomic<size_t> peakSearches_{0};
    std::atomic<size_t> activeDurableReaders_{0};
    std::atomic<size_t> peakDurableReaders_{0};
    std::atomic<uint64_t> completedSearches_{0};
    std::atomic<uint64_t> cancelledSearches_{0};
    std::atomic<uint64_t> unavailableSearches_{0};
    std::atomic<uint64_t> readerFailures_{0};
    std::atomic<uint64_t> generationRestarts_{0};
    std::atomic<uint64_t> queryVisibleInvalidations_{0};
    std::atomic<uint64_t> metadataOnlyUpdates_{0};
    std::atomic<uint64_t> metadataOnlyInvalidationsAvoided_{0};
    std::atomic<uint64_t> durableFallbacks_{0};
    std::atomic<uint64_t> successfulDurableFallbacks_{0};
    std::atomic<uint64_t> failedDurableFallbacks_{0};
    std::atomic<uint64_t> durableFallbackRowsStreamed_{0};
    std::atomic<uint64_t> lifecycleEpoch_{1};

    // zero-touch-autonomous-engineering (subtree gating): per-volume rules +
    // drive-letter prefix (VolumeSetting.key, e.g. "C:") used to evaluate
    // ffprotocol::IsPathIncluded against reconstructed canonical paths.
    std::unordered_map<ffindexstore::VolumeRowId, ffprotocol::VolumeSetting> volumeRules_;

    // Returns true if the entry should be kept (included) under the volume's
    // configured rules. Caller MUST hold mutex_. Returns true (include) when
    // no rules are configured for the volume (preserves pre-change behavior),
    // or when the entry's parent chain cannot yet be resolved in the
    // projection (deferred: decided on the next reconciliation pass rather
    // than risk dropping a record whose inclusion is genuinely undecided).
    bool IsEntryIncludedLocked(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId frn);

    // address-engineering-audit-findings 5.1: keep planner statistics fresh
    // without adding ANALYZE work to every small journal update. The counter
    // is retained when ANALYZE fails so a later ingestion batch retries it.
    static constexpr uint64_t kAnalyzeRecordThreshold = 10'000;
    void MaybeAnalyzeAfterBulkInsertLocked(size_t recordCount);
    void MaybeReportProjectionMemoryPressureLocked();
    uint64_t recordsSinceAnalyze_ = 0;
    bool projectionMemoryWarningIssued_ = false;

    struct FileIdHash {
        size_t operator()(const ffindexstore::FileId& id) const noexcept {
            return std::hash<uint64_t>{}(id.low) ^ (std::hash<uint64_t>{}(id.high) << 1);
        }
    };
    std::unordered_map<ffindexstore::VolumeRowId, std::unordered_set<ffindexstore::FileId, FileIdHash>>
        reconciliationSeen_;
};

} // namespace ffengine
