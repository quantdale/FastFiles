#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "ffindexstore/IndexStore.h"

#include "PrivilegedConnection.h"

namespace ffengine {

// Owns the engine's use of the privileged connection for real indexing
// (tasks.md sections 6-8): issues EnumerateVolumes/StartVolumeScan/
// OpenUsnJournal over PrivilegedConnection, receives the service's
// streamed batches via its frame handler, and drives them through
// ffindexstore::IndexStore per the required commit-then-apply ordering
// (D4). Also owns periodic reconciliation scheduling (section 8) and
// volume-availability tracking (section 7).
//
// All IndexStore access from this class is serialized by `mutex_` --
// frames arrive on PrivilegedConnection's internal reader thread, while
// reconciliation runs on this class's own timer thread; IndexStore itself
// is not internally synchronized (design.md: callers serialize their own
// write path), so this is the one place that discipline is enforced for
// the whole engine.
class IngestionPipeline {
public:
    ~IngestionPipeline();

    // onIndexUpdated is invoked (from whichever thread just applied a
    // batch to the projection) so the caller can republish the snapshot /
    // notify UI clients -- mirrors SnapshotPublisher's existing
    // republish-on-change pattern.
    void Start(PrivilegedConnection& connection, ffindexstore::IndexStore& indexStore, std::function<void()> onIndexUpdated);
    void Stop();

    // Wired to PrivilegedConnection's state-change callback.
    void OnConnectionStateChanged(ConnectionState state);

private:
    void HandleFrame(uint16_t messageType, const std::vector<uint8_t>& payload);
    void HandleVolumeList(const std::vector<uint8_t>& payload);
    void HandleScanBatch(const std::vector<uint8_t>& payload);
    void HandleScanComplete(const std::vector<uint8_t>& payload);
    void HandleJournalOpened(const std::vector<uint8_t>& payload);
    void HandleUsnBatch(const std::vector<uint8_t>& payload);
    void HandleJournalResumeInvalid(const std::vector<uint8_t>& payload);

    // Requires mutex_ already held by the caller.
    void StartOrResumeVolumeLocked(ffprotocol::VolumeId ephemeralId, ffindexstore::DurableVolumeId durableId, wchar_t driveLetter);
    void StartReconciliationSweepLocked(ffindexstore::DurableVolumeId durableId);

    void ReconciliationLoop();

    PrivilegedConnection* connection_ = nullptr;
    ffindexstore::IndexStore* indexStore_ = nullptr;
    std::function<void()> onIndexUpdated_;

    std::mutex mutex_;
    std::map<uint32_t, ffindexstore::DurableVolumeId> ephemeralToDurable_;
    std::map<ffindexstore::DurableVolumeId, wchar_t> driveLetterByDurable_;

    // Reconciliation is implemented as another full StartVolumeScan whose
    // resulting batches are buffered here and diffed against the durable
    // store on ScanComplete (design.md: reconciliation compares against
    // "a fresh scan"), rather than incrementally ingested -- see
    // IngestionPipeline.cpp for the tradeoff this makes (design.md's
    // reconciliation cadence/pacing is an explicit open question).
    std::map<ffindexstore::DurableVolumeId, std::vector<ffindexstore::IngestEntry>> reconciliationBuffers_;

    std::atomic<bool> privilegedActive_{false};
    std::atomic<bool> running_{false};
    std::thread reconciliationThread_;
    std::condition_variable reconciliationCv_;
};

} // namespace ffengine
