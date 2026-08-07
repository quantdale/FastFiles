#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
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
    };

    void OnVolumeList(std::vector<ffprotocol::VolumeInfo> volumes);
    void OnScanBatch(ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor, std::vector<ffprotocol::MftRecordV1> records);
    void OnScanComplete(ffprotocol::VolumeId volumeId);
    void OnJournalOpened(ffprotocol::VolumeId volumeId, uint64_t journalId, uint64_t currentUsn);
    void OnJournalBatch(ffprotocol::VolumeId volumeId, uint64_t latestUsn, std::vector<ffprotocol::UsnDeltaV1> records);
    void OnJournalResumeInvalid(ffprotocol::VolumeId volumeId);

    // Issues StartVolumeScan (with the persisted cursor if the initial
    // scan never finished, task 6.3) and OpenUsnJournal (with the
    // persisted ResumeUsn) for a newly-seen or reconnected volume.
    void StartOrResumeVolume(ffprotocol::VolumeId ephemeralId, const VolumeSession& session);
    // tasks.md 7.6/D6: a from-scratch StartVolumeScan whose completion
    // (via IndexPipeline's reconciliation-pass tracking) self-corrects
    // missed events, rather than an unconditional full rescan discarding
    // existing rows.
    void TriggerReconciliation(ffprotocol::VolumeId ephemeralId, ffindexstore::VolumeRowId durableId);
    void RepublishSnapshot(ffindexstore::VolumeRowId durableId, wchar_t driveLetter);
    // True when the volume's indexing is currently paused (globally or by
    // letter). Callers must hold mutex_.
    bool IsPausedLocked(wchar_t driveLetter) const;
    // Re-issues StartVolumeScan/OpenUsnJournal for every session that is
    // no longer paused (called on resume). No-op if the volume's pause
    // state is still in effect.
    void ResumeAffectedSessions(const std::vector<wchar_t>& affectedLetters);

    void ReconciliationSchedulerLoop();

    IndexPipeline& pipeline_;
    PrivilegedConnection& connection_;
    SnapshotReadyCallback onSnapshotReady_;
    VolumeUnavailableCallback onVolumeUnavailable_;

    std::mutex mutex_;
    std::map<uint32_t, VolumeSession> sessionsByEphemeralId_; // keyed by ffprotocol::VolumeId::value
    std::vector<ffprotocol::VolumeSetting> configuredVolumes_;
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
};

} // namespace ffengine
