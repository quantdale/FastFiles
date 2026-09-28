#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#include "ffindexstore/Identity.h"
#include "ffprotocol/Commands.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/Settings.h"
#include "ffprotocol/UiProtocol.h"

#include "IndexPipeline.h"
#include "PrivilegedConnection.h"

namespace ffengine {

// index-storage-and-scanning tasks.md sections 6/7/8: the orchestrator
// that turns "the privileged connection is Active" into real scanning,
// journaling, and reconciliation. Registers itself as PrivilegedConnection's
// scan/journal callback target, maps each ephemeral (connection-scoped)
// VolumeId to the durable identity IndexPipeline persists against (task
// 7.1), decides Start-vs-Resume-vs-Reconcile per volume (D6), and
// schedules periodic reconciliation sweeps for available volumes only
// (task 8.3/8.5).
class VolumeSessionManager {
public:
    VolumeSessionManager(IndexPipeline& pipeline, PrivilegedConnection& connection);
    ~VolumeSessionManager();

    // Wires this instance's handlers into `connection` (must be called
    // before connection.Start()) and starts the reconciliation-scheduling
    // background thread.
    void Start();
    void Stop();

    // Drives whether volume sessions are (re)issued and whether
    // reconciliation scheduling runs at all (task 8.5: never while
    // degraded). Call from the same StateChangeCallback wired to
    // PrivilegedConnection.
    void OnConnectionStateChanged(ConnectionState state);

    // Invoked (on whatever thread applied a batch/rebuild) with the
    // volume's up-to-date directory snapshot data, for the caller to
    // publish through SnapshotPublisher (tasks.md 3.5).
    using SnapshotReadyCallback =
        std::function<void(ffindexstore::VolumeRowId, std::map<std::wstring, ffprotocol::SnapshotDirectory>)>;
    void SetSnapshotReadyCallback(SnapshotReadyCallback callback) { onSnapshotReady_ = std::move(callback); }
    using VolumeUnavailableCallback = std::function<void(ffindexstore::VolumeRowId, wchar_t)>;
    void SetVolumeUnavailableCallback(VolumeUnavailableCallback callback) {
        onVolumeUnavailable_ = std::move(callback);
    }
    void ReloadConfiguration(std::vector<ffprotocol::VolumeSetting> volumes);

    // settings-and-appearance §9.1: pause/resume indexing, global
    // (scope == 0) or per-volume (uppercase drive letter), driven by the
    // UI's control-plane SetIndexingPaused message (design.md D9). Pause
    // is transient engine state, never persisted: while paused the engine
    // stops issuing new scan/reconciliation work and stops applying
    // inbound scan/journal batches (leaving the stored cursor/USN
    // untouched), so resume continues from the last-applied position
    // without restarting from zero. State is reflected back to the UI
    // exclusively through CollectVolumeStatus's VolumeStatusPaused flag.
    void SetIndexingPaused(uint8_t scope, bool paused);

    // settings-and-appearance §7.3: reports the current per-volume
    // condition flags for every enabled configured volume the engine
    // knows about (reachable / scanning / needs-reconciliation).
    // PartiallyIndexed is not reported because per-subtree completion
    // progress does not exist in the store today; the derived headline
    // therefore spans the states the engine can truthfully observe.
    // Called on-demand from the UI seam; no new persisted state.
    std::vector<ffprotocol::VolumeStatusRecord> CollectVolumeStatus();

private:
    // Tracks an active volume session from the privileged service. Maps the ephemeral
    // ffprotocol::VolumeId (connection-scoped identifier) to the durable volumeRowId that
    // IndexPipeline persists across reconnections. The journalId identifies the USN journal
    // state on the volume, and journalIdKnown gates resume-vs-restart logic after
    // disconnection and reconnection.
    struct VolumeSession {
        ffindexstore::VolumeRowId durableId = 0;
        wchar_t driveLetter = L'\0';
        uint64_t journalId = 0;
        bool journalIdKnown = false;
        bool journalOpenRequested = false;
        // close-independent-validation-gaps 4.6: an initial or
        // reconciliation scan keeps the durable projection current in
        // batches, but defers the expensive complete snapshot export until
        // ScanComplete. Journal-only updates remain publishable once the
        // scan has finished.
        bool scanInProgress = false;
        // index-durability-false-success-closure D2: a scan whose batches
        // all applied but whose completion would otherwise be taken at
        // face value must stay non-authoritative if any batch failed to
        // apply (the cursor is frozen at the last-applied position, so the
        // scan demonstrably did not cover everything since).
        bool scanBatchFailed = false;
        // True while the in-flight scan on this session is the one started
        // by TriggerReconciliation, so OnScanComplete knows whether the
        // authoritative completion is allowed to finalize the durable
        // reconciliation pass (a mid-scan pass start must never finish it).
        bool reconciliationScan = false;
        // A reconciliation request arrived while a scan was in flight; it
        // is issued when that scan next completes authoritatively (D4),
        // never by racing a second BeginReconciliationPass against it.
        bool deferredReconciliation = false;
        // scan-generation-integrity-and-residual-certification-closure 3.4:
        // a coverage-hole replacement is provisional until the {2,3}
        // ScanAttemptStarted barrier arrives. Before that barrier, queued
        // batches and terminals belong to the superseded worker or are
        // otherwise unordered, so they receive no index authority.
        bool awaitingScanAttemptStarted = false;
    };

    // A bounded retry deadline and its kind, shared by the delivered-failure
    // retry queue, the durable reissue floor, and the retirement path.
    using RetryDeadline = std::chrono::steady_clock::time_point;
    struct FailedScanRetry {
        RetryDeadline due;
        bool reconciliation = false;
    };

    void OnVolumeList(std::vector<ffprotocol::VolumeInfo> volumes);
    void OnScanBatch(ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor, std::vector<ffprotocol::MftRecordV1> records);
    // allocated-size-storage-accuracy 5.2: additive V2 scan records
    // (allocated size) applied through the same pipeline path.
    void OnScanBatchV2(ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor,
                       std::vector<ffprotocol::MftRecordV2> records);
    void OnScanComplete(ffprotocol::VolumeId volumeId);
    // index-durability-false-success-closure D1/D2: the ScanFailed terminal
    // -- the scan did not run to completion, so nothing derived from it is
    // authoritative: no MarkScanComplete, no pass finalization.
    void OnScanFailed(ffprotocol::VolumeId volumeId);
    // The negotiated service-to-engine ordering barrier written after the
    // old worker joins and before this replacement worker starts.
    void OnScanAttemptStarted(ffprotocol::VolumeId volumeId);
    void OnJournalOpened(ffprotocol::VolumeId volumeId, uint64_t journalId, uint64_t currentUsn);
    void OnJournalBatch(ffprotocol::VolumeId volumeId, uint64_t latestUsn, std::vector<ffprotocol::UsnDeltaV2> records);
    // allocated-size-storage-accuracy 5.2: additive V3 journal records.
    void OnJournalBatchV3(ffprotocol::VolumeId volumeId, uint64_t latestUsn, std::vector<ffprotocol::UsnDeltaV3> records);
    void OnJournalResumeInvalid(ffprotocol::VolumeId volumeId);

    // allocated-size-storage-accuracy 5.2 refactor: shared pause-check /
    // commit-before-cursor-advance lifecycle for scan and journal batches
    // (the V1/V2 and V2/V3 record overloads differ only in conversion).
    void ApplyScanBatch(ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor,
                        const std::function<bool(ffindexstore::VolumeRowId)>& apply);
    void ApplyJournalBatch(ffprotocol::VolumeId volumeId, uint64_t latestUsn,
                           const std::function<bool(ffindexstore::VolumeRowId)>& apply);

    // Issues StartVolumeScan (with the persisted cursor if the initial
    // scan never finished, task 6.3) and OpenUsnJournal (with the
    // persisted ResumeUsn) for a newly-seen or reconnected volume.
    bool StartOrResumeVolume(ffprotocol::VolumeId ephemeralId, const VolumeSession& session);
    // tasks.md 7.6/D6: a from-scratch StartVolumeScan whose completion
    // (via IndexPipeline's reconciliation-pass tracking) self-corrects
    // missed events, rather than an unconditional full rescan discarding
    // existing rows. An already-owned retry slot always defers this call:
    // the slot issues at its bounded due time and carries exactly one
    // intent, so no caller may shorten the five-second cadence.
    bool TriggerReconciliation(ffprotocol::VolumeId ephemeralId, ffindexstore::VolumeRowId durableId);
    // Caller holds mutex_. Consumes and returns the pending reissue floor for a
    // volume whose previous scan ended in connection loss. The caller decides
    // whether an unexpired floor defers the reissue, whether an expired floor
    // reissues immediately, or whether an already-indexed volume transfers the
    // intent onto the replacement connection's own retry slot at the same
    // durable due time.
    std::optional<FailedScanRetry> TakeScanReissueFloorLocked(ffindexstore::VolumeRowId durableId);
    // retry-ownership-and-focus-escape-closure D2: the
    // legal owner of one durable volume's retry intent. Exactly one applies at
    // a time -- the in-flight attempt, the connection-scoped queue, the
    // scheduler's in-hand dispatch, or the durable reconnect floor -- and every
    // transition runs under both mutex_ and wakeMutex_ so no observer can see
    // the intent unowned.
    enum class RetryOwnership : uint8_t {
        None,
        ActiveAttempt,
        EphemeralQueued,
        SchedulerDispatching,
        DurableFloor,
    };
    // Caller holds both mutex_ and wakeMutex_.
    RetryOwnership RetryOwnerStateLocked(ffindexstore::VolumeRowId durableId) const;
    // Caller holds both locks. Merges a bounded retry onto the durable floor,
    // keeping the later deadline and the owed kind.
    void MergeDurableFloorLocked(ffindexstore::VolumeRowId durableId, const FailedScanRetry& retry);
    // Caller holds both locks. Moves a live session's retry intent (in-flight
    // attempt, queued entry, or in-hand dispatch) onto the durable floor and
    // clears the connection-scoped queue entry.
    void TransferSessionRetryToFloorLocked(uint32_t ephemeralValue, const VolumeSession& session,
                                           std::chrono::steady_clock::time_point at);
    // Caller holds both locks. Installs the single next owner for a terminal
    // that could not grant authority: the live queue while the connection is
    // current, otherwise the durable floor. Never inserts a dead ephemeral id.
    void InstallNonAuthoritativeRetryLocked(uint32_t ephemeralValue, ffindexstore::VolumeRowId durableId,
                                            std::chrono::steady_clock::time_point due, bool reconciliation);
    // Caller holds both locks. Claims the in-flight attempt for the session,
    // consuming whichever other owner was installed for the volume.
    void ClaimActiveAttemptLocked(uint32_t ephemeralValue, ffindexstore::VolumeRowId durableId,
                                  bool reconciliation);
    // Caller holds both locks. Clears every retry owner for the volume: the
    // intent is satisfied (authoritative completion) or the engine is stopping.
    void ClearRetryOwnerLocked(ffindexstore::VolumeRowId durableId, uint32_t ephemeralValue);
    // Caller holds both locks. A retry admission whose submission failed
    // (the connection generation was discarded) restores the single bounded
    // retry owner at the five-second floor instead of leaving a claim with no
    // legal submission or dropping the intent.
    void RearmFailedAdmissionLocked(uint32_t ephemeralValue, ffindexstore::VolumeRowId durableId,
                                    bool reconciliation);
    void RepublishSnapshot(ffindexstore::VolumeRowId durableId, wchar_t driveLetter, bool force = false);
    // True when the volume's indexing is currently paused (globally or by
    // letter). Callers must hold mutex_.
    bool IsPausedLocked(wchar_t driveLetter) const;
    // Re-issues StartVolumeScan/OpenUsnJournal for every session that is
    // no longer paused (called on resume). No-op if the volume's pause
    // state is still in effect.
    void ResumeAffectedSessions(const std::vector<wchar_t>& affectedLetters);

    void ReconciliationSchedulerLoop();
    void RefreshProjectionScanState();
    void SendSessionTeardown(ffprotocol::VolumeId ephemeralId, const VolumeSession& session);
    bool RequestJournalOpen(ffprotocol::VolumeId ephemeralId, uint64_t resumeUsn,
                            ffindexstore::VolumeRowId expectedDurableId);

    IndexPipeline& pipeline_;
    PrivilegedConnection& connection_;
    SnapshotReadyCallback onSnapshotReady_;
    VolumeUnavailableCallback onVolumeUnavailable_;

    std::mutex mutex_;
    std::mutex requestMutex_;
    std::map<uint32_t, VolumeSession> sessionsByEphemeralId_; // keyed by ffprotocol::VolumeId::value
    std::vector<ffprotocol::VolumeSetting> configuredVolumes_;
    // close-independent-validation-gaps 4.6: serializing the full directory
    // snapshot for every MFT/USN batch starves the ingestion path and lets a
    // reader race a rapidly reused shared-memory slot. Durable ingestion is
    // still batch-by-batch; UI publication is coalesced and the completion
    // path forces the final view.
    std::map<ffindexstore::VolumeRowId, std::chrono::steady_clock::time_point> lastSnapshotPublish_;
    // settings-and-appearance §9.3/D9: drive letters the engine has
    // observed (last EnumerateVolumes poll) that have no matching entry
    // in the persisted volume selection -- surfaced as pending-decision
    // through CollectVolumeStatus, not tracked as separate state.
    std::set<wchar_t> observedLetters_;
    std::atomic<bool> paused_{false}; // global pause (scope == 0)
    std::set<wchar_t> pausedVolumes_; // per-volume pause (guarded by mutex_)

    std::atomic<bool> active_{false};
    std::atomic<bool> running_{false};
    std::thread reconciliationThread_;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
    // Guarded by wakeMutex_. Delivered ScanFailed outcomes retry without a
    // finite attempt cap, once per volume per five-second cadence, and without
    // spawning an unowned thread. Disconnect and Stop clear ephemeral entries.
    std::map<uint32_t, FailedScanRetry> failedScanRetries_;
    // Guarded by mutex_ AND wakeMutex_ (all retry-owner transitions take the
    // pair in that order). A due retry that has left the queue is still owned
    // here until it is admitted or re-queued, so a disconnect can transfer it
    // instead of dropping the local scheduler copy.
    std::map<ffindexstore::VolumeRowId, FailedScanRetry> schedulerDispatchRetries_;
    uint64_t scanRetryGeneration_ = 0; // guarded by wakeMutex_
    // legacy-protocol-correctness-and-raw-volume-cancellation-closure D3/8.6:
    // per durable volume, a floor before the next scan reissue after a
    // connection ended without any terminal (the unreportable legacy failure
    // aborts its connection, so no ScanFailed can arm the retry queue). Keyed
    // by durable identity because the connection-scoped sessions are cleared
    // on that transition, and honored by both the resume and reconciliation
    // issue paths so the documented five-second failed-scan cadence is
    // preserved instead of spinning reconnect -> rescan -> teardown. Guarded
    // by mutex_; decayed entries are erased when consulted.
    std::map<ffindexstore::VolumeRowId, FailedScanRetry> scanReissueFloors_;

    // retry-ownership-and-focus-escape-closure D3: the
    // seam standing in for the window between the scheduler taking ownership
    // of a due retry and adopting it. It must only run with neither mutex_
    // nor wakeMutex_ held, so a test can disconnect exactly there and prove
    // the intent still has an owner. Null in production.
    std::function<void()> scanRetryDispatchSeam_;
};

} // namespace ffengine
