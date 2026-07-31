#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "ffindexstore/Identity.h"
#include "ffprotocol/Commands.h"
#include "ffprotocol/SnapshotFormat.h"

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

private:
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

    void ReconciliationSchedulerLoop();

    IndexPipeline& pipeline_;
    PrivilegedConnection& connection_;
    SnapshotReadyCallback onSnapshotReady_;

    std::mutex mutex_;
    std::map<uint32_t, VolumeSession> sessionsByEphemeralId_; // keyed by ffprotocol::VolumeId::value

    std::atomic<bool> active_{false};
    std::atomic<bool> running_{false};
    std::thread reconciliationThread_;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
};

} // namespace ffengine
