#include <cstdio>
#include <filesystem>
#include <string>

#include "IndexPipeline.h"
#include "../TestSupport.h"

using namespace fftest;
// This test deliberately exercises the session manager's callback path,
// rather than duplicating its resume-vs-reconcile decision in a helper.
#define private public
#include "VolumeSessionManager.h"
#undef private

namespace {
void AddSession(ffengine::VolumeSessionManager& manager, ffindexstore::VolumeRowId volumeId) {
    manager.sessionsByEphemeralId_.emplace(7, ffengine::VolumeSessionManager::VolumeSession{volumeId, L'C', 0, false});
}

ffprotocol::MftRecordV1 MakeMftRecord(uint64_t id, uint64_t parent, std::u16string name, uint32_t attrs, uint64_t size) {
    ffprotocol::MftRecordV1 r{};
    r.fixed.fileReferenceNumber = id;
    r.fixed.parentFileReferenceNumber = parent;
    r.fixed.fileAttributes = attrs;
    r.fixed.sizeBytes = size;
    r.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
    r.fileName = std::move(name);
    return r;
}

void TestMatchingJournalResumesWithoutReconciliation() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_journal_resume.db")), "pipeline opens for matching-journal test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 1;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.SetJournalPosition(volumeId, 41, 900), "persisted journal position is established");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 41, 1000);

    Check(!pipeline.IsReconciliationPassActive(volumeId), "matching journal identity resumes instead of reconciling");
    const auto metadata = pipeline.GetVolumeMetadata(volumeId);
    Check(metadata && metadata->journalId && *metadata->journalId == 41 && metadata->resumeUsn == 900,
          "matching journal preserves the persisted resume position");
}

void TestChangedJournalTriggersReconciliation() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_journal_discontinuity.db")), "pipeline opens for discontinuity test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 2;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.SetJournalPosition(volumeId, 41, 900), "persisted prior journal position is established");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 42, 1000);

    Check(pipeline.IsReconciliationPassActive(volumeId), "changed journal identity begins reconciliation rather than blind resume");
    const auto metadata = pipeline.GetVolumeMetadata(volumeId);
    Check(metadata && metadata->journalId && *metadata->journalId == 42 && metadata->resumeUsn == 1000,
          "changed journal resets the stored position to the new journal's current point");
}

void TestDisappearanceMarksUnavailableAndWithdrawsPublishedVolume() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_volume_disappearance.db")),
          "pipeline opens for volume-disappearance test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 3;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    ffindexstore::VolumeRowId callbackVolume = 0;
    wchar_t callbackDrive = L'\0';
    manager.SetVolumeUnavailableCallback([&](ffindexstore::VolumeRowId id, wchar_t driveLetter) {
        callbackVolume = id;
        callbackDrive = driveLetter;
    });

    manager.Start();
    connection.volumeListCallback_({});

    const auto metadata = pipeline.GetVolumeMetadata(volumeId);
    Check(metadata && !metadata->available, "a disappeared volume is retained but marked unavailable");
    Check(callbackVolume == volumeId && callbackDrive == L'C',
          "a disappeared volume requests withdrawal of its published drive snapshot");
}

void TestCollectVolumeStatusReportsReachableScannedVolumes() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_volume_status.db")), "pipeline opens for volume-status test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 4;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);
    pipeline.MarkScanComplete(volumeId);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});

    const auto records = manager.CollectVolumeStatus();
    Check(records.size() == 1, "one configured, enabled, tracked volume is reported");
    if (records.size() == 1) {
        Check(records[0].driveLetter == static_cast<uint8_t>(L'C'), "report addresses the volume by drive letter");
        Check((records[0].flags & ffprotocol::VolumeStatusReachable) != 0,
              "a tracked volume is reported reachable");
        Check((records[0].flags & ffprotocol::VolumeStatusScanning) == 0,
              "a completed scan reports no scanning condition");
    }

    pipeline.BeginReconciliationPass(volumeId);
    const auto duringReconciliation = manager.CollectVolumeStatus();
    Check(duringReconciliation.size() == 1, "reconciliation still reports the volume");
    if (duringReconciliation.size() == 1) {
        Check((duringReconciliation[0].flags & ffprotocol::VolumeStatusNeedsReconciliation) != 0,
              "an active reconciliation pass reports the needs-reconciliation condition");
        Check((duringReconciliation[0].flags & ffprotocol::VolumeStatusScanning) != 0,
              "an active reconciliation pass also reads as scanning (catch-up in progress)");
    }
}

void TestCollectVolumeStatusSkipsDisabledAndUntracked() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_volume_status_disabled.db")),
          "pipeline opens for disabled-volume-status test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 5;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);

    ffprotocol::VolumeSetting disabled;
    disabled.key = L"C:";
    disabled.enabled = false;
    ffprotocol::VolumeSetting enabledUntracked;
    enabledUntracked.key = L"D:";
    enabledUntracked.enabled = true;
    manager.ReloadConfiguration({disabled, enabledUntracked});

    const auto records = manager.CollectVolumeStatus();
    Check(records.size() == 1, "disabled volumes are not reported as index-health records");
    if (records.size() == 1) {
        Check(records[0].driveLetter == static_cast<uint8_t>(L'D'),
              "only the enabled volume appears in the report");
        Check((records[0].flags & ffprotocol::VolumeStatusReachable) == 0,
              "an enabled volume with no live session reads as unreachable");
    }

    ffprotocol::VolumeSetting enabledTracked;
    enabledTracked.key = L"C:";
    enabledTracked.enabled = true;
    // Note: the previous list's duplicate disabled C: entry is intentionally
    // replaced -- OnVolumeList's first-match lookup would otherwise gate on
    // the disabled entry and never recreate the session.
    manager.ReloadConfiguration({enabledTracked});
    // The earlier disable tore the live C: session down (task 2.5 live
    // re-evaluation); ReloadConfiguration re-enumerates when the
    // privileged connection is active -- drive that poll directly here so
    // the session re-enters the normal start path, no restart anywhere.
    manager.Start();
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{7}, L'C'}});
    const auto tracked = manager.CollectVolumeStatus();
    Check(tracked.size() == 1, "an enabled tracked volume is reported");
    if (tracked.size() == 1) {
        Check(tracked[0].driveLetter == static_cast<uint8_t>(L'C'),
              "the tracked volume is the one reported");
        Check((tracked[0].flags & ffprotocol::VolumeStatusReachable) != 0,
              "an enabled tracked volume is reported reachable");
        Check((tracked[0].flags & ffprotocol::VolumeStatusScanning) != 0,
              "a volume whose scan never completed reports the scanning condition");
    }
}

void TestDisableTearsDownSessionLive() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_disable_teardown.db")),
          "pipeline opens for disable-teardown test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 7;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    ffindexstore::VolumeRowId callbackVolume = 0;
    wchar_t callbackDrive = L'\0';
    manager.SetVolumeUnavailableCallback([&](ffindexstore::VolumeRowId id, wchar_t driveLetter) {
        callbackVolume = id;
        callbackDrive = driveLetter;
    });

    ffprotocol::VolumeSetting disabled;
    disabled.key = L"C:";
    disabled.enabled = false;
    manager.ReloadConfiguration({disabled});

    Check(manager.sessionsByEphemeralId_.empty(),
          "settings-and-appearance 9.2: a disabled volume's session is torn down immediately, no restart");
    Check(callbackVolume == volumeId && callbackDrive == L'C',
          "disabling withdraws the drive's published directories");

    ffprotocol::VolumeSetting enabled;
    enabled.key = L"C:";
    enabled.enabled = true;
    manager.ReloadConfiguration({enabled});
    Check(manager.sessionsByEphemeralId_.empty(),
          "re-enabling alone does not resurrect the session; the next EnumerateVolumes poll starts it");
    manager.Start();
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{7}, L'C'}});
    Check(manager.sessionsByEphemeralId_.size() == 1,
          "the re-enumeration poll restarts the re-enabled volume's session");
}

void TestPauseShownInStatusAndDropsBatchesWithoutAdvancing() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_pause.db")), "pipeline opens for pause test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 8;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});

    std::vector<ffprotocol::MftRecordV1> batch{
        MakeMftRecord(5, 5, u"", 0x10, 0),
        MakeMftRecord(100, 5, u"Users", 0x10, 0),
        MakeMftRecord(101, 100, u"notes.txt", 0, 42),
    };
    // Distinct FRNs per stage: with duplicate FRNs the store's
    // INSERT-OR-IGNORE re-apply is idempotent, which would make the
    // dropped-vs-applied assertions indistinguishable.
    std::vector<ffprotocol::MftRecordV1> pausedBatch{
        MakeMftRecord(200, 5, u"Paused", 0x10, 0),
        MakeMftRecord(201, 200, u"paused.txt", 0, 7),
    };
    std::vector<ffprotocol::MftRecordV1> resumedBatch{
        MakeMftRecord(300, 5, u"Resumed", 0x10, 0),
        MakeMftRecord(301, 300, u"resumed.txt", 0, 8),
    };
    manager.Start();
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{1, 2, 3}, batch);
    const auto entryCountBeforePause = pipeline.GetVolumeMetadata(volumeId)->entryCount;
    Check(entryCountBeforePause > 0, "an applied scan batch populates the index");

    manager.SetIndexingPaused(0, true); // global pause
    const auto paused = manager.CollectVolumeStatus();
    Check(paused.size() == 1, "the configured volume is still reported while paused");
    if (paused.size() == 1) {
        Check((paused[0].flags & ffprotocol::VolumeStatusPaused) != 0,
              "settings-and-appearance 9.1: pause is reflected back through the status report");
    }

    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{4, 5, 6}, pausedBatch);
    Check(pipeline.GetVolumeMetadata(volumeId)->entryCount == entryCountBeforePause,
          "batches received while paused are dropped without applying or advancing the cursor");

    manager.SetIndexingPaused(0, false); // global resume
    const auto resumed = manager.CollectVolumeStatus();
    Check(resumed.size() == 1 && (resumed[0].flags & ffprotocol::VolumeStatusPaused) == 0,
          "resume clears the paused status flag");
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{7, 8, 9}, resumedBatch);
    Check(pipeline.GetVolumeMetadata(volumeId)->entryCount > entryCountBeforePause,
          "batches apply again after resume -- resume continues from the stored position, not from zero");
}

void TestPendingDecisionForObservedUnselectedVolume() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_pending_decision.db")),
          "pipeline opens for pending-decision test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 9;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();

    ffprotocol::VolumeSetting selectedC;
    selectedC.key = L"C:";
    selectedC.enabled = true;
    manager.ReloadConfiguration({selectedC});

    // Observed poll with an unselected second volume.
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{7}, L'C'},
                                    ffprotocol::VolumeInfo{ffprotocol::VolumeId{8}, L'D'}});

    const auto records = manager.CollectVolumeStatus();
    bool sawDPending = false;
    for (const auto& record : records) {
        if (record.driveLetter == static_cast<uint8_t>(L'D')) {
            sawDPending = (record.flags & ffprotocol::VolumeStatusPendingDecision) != 0;
        }
        Check(!(record.driveLetter == static_cast<uint8_t>(L'C')
                && (record.flags & ffprotocol::VolumeStatusPendingDecision) != 0),
              "a selected volume is never pending-decision");
    }
    Check(sawDPending,
          "settings-and-appearance 9.3: an observed unselected volume reads as pending-decision");

    // Once persisted (the add-volume action), the pending record
    // disappears without needing a new observation.
    ffprotocol::VolumeSetting added;
    added.key = L"D:";
    added.enabled = true;
    manager.ReloadConfiguration({added});
    const auto afterAdd = manager.CollectVolumeStatus();
    bool sawDPendingAfterAdd = false;
    for (const auto& record : afterAdd) {
        if (record.driveLetter == static_cast<uint8_t>(L'D')) {
            sawDPendingAfterAdd = (record.flags & ffprotocol::VolumeStatusPendingDecision) != 0;
        }
    }
    Check(!sawDPendingAfterAdd,
          "adding the volume to the selection clears the pending-decision record");
}

} // namespace

int main() {
    TestMatchingJournalResumesWithoutReconciliation();
    TestChangedJournalTriggersReconciliation();
    TestDisappearanceMarksUnavailableAndWithdrawsPublishedVolume();
    TestCollectVolumeStatusReportsReachableScannedVolumes();
    TestCollectVolumeStatusSkipsDisabledAndUntracked();
    TestDisableTearsDownSessionLive();
    TestPauseShownInStatusAndDropsBatchesWithoutAdvancing();
    TestPendingDecisionForObservedUnselectedVolume();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
