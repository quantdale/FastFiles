#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <windows.h>

#include "ffipc/PipeFraming.h"
#include "ffsetup/Identifiers.h"
#include "sqlite3.h"
#include "IndexPipeline.h"
#include "../TestSupport.h"

using namespace fftest;
// This test deliberately exercises the session manager's callback path,
// rather than duplicating its resume-vs-reconcile decision in a helper.
#define private public
#include "VolumeSessionManager.h"
#undef private

namespace {

struct PipeCapture {
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;

    ~PipeCapture() {
        if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
        if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
    }
};

std::unique_ptr<PipeCapture> CreatePipeCapture() {
    const std::wstring name = L"\\\\.\\pipe\\FastFilesVolumeSessionTest-" +
                              std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64());
    auto capture = std::make_unique<PipeCapture>();
    capture->server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                       1, 4096, 4096, 0, nullptr);
    if (capture->server == INVALID_HANDLE_VALUE) return nullptr;
    capture->client = CreateFileW(name.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (capture->client == INVALID_HANDLE_VALUE) return nullptr;
    if (ConnectNamedPipe(capture->server, nullptr) == FALSE && GetLastError() != ERROR_PIPE_CONNECTED) {
        return nullptr;
    }
    return capture;
}

bool ConnectOverlappedPipeWithTimeout(HANDLE pipe, DWORD timeoutMilliseconds) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) return false;
    const BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    if (connected != FALSE) {
        CloseHandle(overlapped.hEvent);
        return true;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED) {
        CloseHandle(overlapped.hEvent);
        return true;
    }
    if (error != ERROR_IO_PENDING) {
        CloseHandle(overlapped.hEvent);
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(overlapped.hEvent, timeoutMilliseconds);
    if (waitResult != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &overlapped);
        WaitForSingleObject(overlapped.hEvent, 2000);
        DWORD ignored = 0;
        GetOverlappedResult(pipe, &overlapped, &ignored, FALSE);
        CloseHandle(overlapped.hEvent);
        return false;
    }
    DWORD ignored = 0;
    const bool result = GetOverlappedResult(pipe, &overlapped, &ignored, FALSE) != FALSE;
    CloseHandle(overlapped.hEvent);
    return result;
}

bool WriteOneOverlappedByte(HANDLE pipe, uint8_t value) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) return false;
    DWORD bytesWritten = 0;
    const BOOL completed = WriteFile(pipe, &value, sizeof(value), &bytesWritten, &overlapped);
    bool success = completed != FALSE;
    if (!success && GetLastError() == ERROR_IO_PENDING) {
        success = WaitForSingleObject(overlapped.hEvent, 2000) == WAIT_OBJECT_0
            && GetOverlappedResult(pipe, &overlapped, &bytesWritten, FALSE) != FALSE;
    }
    CloseHandle(overlapped.hEvent);
    return success && bytesWritten == sizeof(value);
}

std::optional<uint16_t> ReadCapturedMessage(HANDLE client) {
    const auto frame = ffipc::ReadFrame(client);
    if (!frame) return std::nullopt;
    return frame->header.messageType;
}

void AttachCaptureServer(ffengine::PrivilegedConnection& connection, PipeCapture& capture) {
    connection.activePipe_ = std::make_shared<ffengine::PrivilegedConnection::OwnedPipe>(capture.server);
    capture.server = INVALID_HANDLE_VALUE;
}

bool HasCapturedBytes(HANDLE client) {
    DWORD available = 0;
    const bool peeked = PeekNamedPipe(client, nullptr, 0, nullptr, &available, nullptr) != FALSE;
    return peeked && available != 0;
}

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
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "volume disappearance creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    manager.active_ = true;
    manager.sessionsByEphemeralId_.at(7).journalOpenRequested = true;
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
    const auto stopMessage = ReadCapturedMessage(capture->client);
    const auto closeMessage = ReadCapturedMessage(capture->client);
    Check(stopMessage == static_cast<uint16_t>(ffprotocol::MessageType::StopVolumeScan),
          "a disappeared volume explicitly stops its service scan");
    Check(closeMessage == static_cast<uint16_t>(ffprotocol::MessageType::CloseUsnJournal),
          "a disappeared volume explicitly closes its open journal");
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
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "disable teardown creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    manager.active_ = true;
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
    Check(ReadCapturedMessage(capture->client) == static_cast<uint16_t>(ffprotocol::MessageType::StopVolumeScan),
          "disabling a session explicitly stops its service scan");
    Check(ReadCapturedMessage(capture->client) == static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes),
          "disabling a session refreshes the service volume list");
    Check(!HasCapturedBytes(capture->client),
          "disabling a session without a journal acknowledgement does not send an invalid CloseUsnJournal");

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

void TestStopClosesOwnedJournalBeforeConnectionShutdown() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_journal_shutdown.db")),
          "pipeline opens for journal-shutdown test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 82;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "journal shutdown creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    {
        ffengine::VolumeSessionManager manager(pipeline, connection);
        AddSession(manager, volumeId);
        manager.sessionsByEphemeralId_.at(7).journalOpenRequested = true;
        manager.active_ = true;
        manager.Start();
        manager.Stop();
    }
    Check(ReadCapturedMessage(capture->client) == static_cast<uint16_t>(ffprotocol::MessageType::StopVolumeScan),
          "manager shutdown stops each owned service scan before connection teardown");
    Check(ReadCapturedMessage(capture->client) == static_cast<uint16_t>(ffprotocol::MessageType::CloseUsnJournal),
          "manager shutdown closes each acknowledged journal before connection teardown");
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

void TestInitialScanBatchesDoNotRepeatWholeSnapshotExports() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_snapshot_scan_throttle.db")),
          "pipeline opens for scan snapshot-throttle test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 81;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    size_t snapshotCount = 0;
    manager.SetSnapshotReadyCallback(
        [&](ffindexstore::VolumeRowId, std::map<std::wstring, ffprotocol::SnapshotDirectory> directories) {
            ++snapshotCount;
            Check(!directories.empty(), "the completion export contains the indexed directory tree");
        });
    manager.Start();
    manager.sessionsByEphemeralId_.at(7).scanInProgress = true;

    const std::vector<ffprotocol::MftRecordV1> batch{
        MakeMftRecord(5, 5, u"", 0x10, 0),
        MakeMftRecord(100, 5, u"ScanRoot", 0x10, 0),
        MakeMftRecord(101, 100, u"during-scan.txt", 0, 17),
    };
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{1, 2, 3}, batch);
    Check(pipeline.GetVolumeMetadata(volumeId)->entryCount == 3,
          "scan batches still commit to the durable projection while publication is deferred");
    Check(snapshotCount == 0, "an in-progress scan batch does not export the complete snapshot");

    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(snapshotCount == 1, "ScanComplete forces one complete snapshot export");
    Check(!manager.sessionsByEphemeralId_.at(7).scanInProgress,
          "ScanComplete clears the publication suppression state");
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

// index-durability-false-success-closure 6.1: ScanFailed is the service's
// explicit "this scan did not run to completion". It must leave every
// derived authority where it was: prior completion state, cadence clock,
// and -- critically -- it must discard rather than finalize the
// reconciliation pass whose seen-set the failed scan only partially fed.
void TestScanFailedLeavesPriorIndexAuthorityIntact() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_scan_failed_authority.db")),
          "pipeline opens for scan-failed authority test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 61;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.MarkScanComplete(volumeId), "prior completion state is established");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.reconciliationScan = true;
    pipeline.BeginReconciliationPass(volumeId);

    connection.scanFailedCallback_(ffprotocol::VolumeId{7});

    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "ScanFailed discards the reconciliation pass instead of finalizing a partial seen-set");
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "ScanFailed leaves the prior completion state intact (PA-001 index survives)");
    Check(pipeline.GetVolumeMetadata(volumeId)->lastReconciliationTime == 0,
          "ScanFailed leaves the reconciliation cadence clock untouched -- the failure is retried");
    Check(!session.scanInProgress && !session.reconciliationScan && !session.scanBatchFailed,
          "ScanFailed clears the in-flight scan state");

    // A duplicate terminal for the same finished scan is refused outright.
    connection.scanFailedCallback_(ffprotocol::VolumeId{7});
    Check(!session.scanInProgress, "a duplicate ScanFailed changes nothing");

    // A later clean scan still marks completion (no sticky poisoning).
    session.scanInProgress = true;
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the next authoritative completion still marks the volume");
}

// final-residual-hardening-and-hosted-ci-recovery 2.4: delivered ScanFailed
// outcomes use an intentionally unbounded retry policy with a five-second
// per-volume cadence. One map entry per ephemeral volume bounds queue state;
// disconnect and manager stop clear it. The test measures repeated scheduled
// intervals, then makes the final retry due without changing production time.
void TestScanFailedSchedulesRetry() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_scan_failed_retry.db")),
          "pipeline opens for scan-failure retry test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 63;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "scan-failure retry creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    connection.state_ = ffengine::ConnectionState::Active;
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 2;
    manager.active_ = true;
    manager.Start();
    manager.sessionsByEphemeralId_.at(7).scanInProgress = true;

    bool queueCardinalityRemainedOne = true;
    std::chrono::milliseconds shortestRetryDelay = std::chrono::milliseconds::max();
    constexpr uint32_t kRepeatedFailures = 3;
    for (uint32_t failure = 0; failure < kRepeatedFailures; ++failure) {
        {
            std::lock_guard<std::mutex> lock(manager.mutex_);
            manager.sessionsByEphemeralId_.at(7).scanInProgress = true;
        }
        const auto failureAt = std::chrono::steady_clock::now();
        connection.scanFailedCallback_(ffprotocol::VolumeId{7});
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        queueCardinalityRemainedOne = queueCardinalityRemainedOne && manager.failedScanRetries_.size() == 1;
        const auto retry = manager.failedScanRetries_.find(7);
        if (retry != manager.failedScanRetries_.end()) {
            const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(retry->second.due - failureAt);
            shortestRetryDelay = (std::min)(shortestRetryDelay, delay);
        }
    }
    Check(queueCardinalityRemainedOne,
          "repeated delivered ScanFailed outcomes keep one retry slot for the same volume");
    Check(shortestRetryDelay >= 4900ms && shortestRetryDelay <= 5100ms,
          "each repeated ScanFailed outcome schedules no earlier than the five-second cadence");

    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        queued = manager.failedScanRetries_.contains(7);
        if (queued) {
            manager.failedScanRetries_.at(7).due = std::chrono::steady_clock::now() - 1ms;
            ++manager.scanRetryGeneration_;
        }
    }
    Check(queued, "ScanFailed queues its per-volume retry deadline");
    manager.wakeCv_.notify_one();

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!HasCapturedBytes(capture->client) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    const auto message = ReadCapturedMessage(capture->client);
    Check(message == static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan),
          "the due ScanFailed retry reissues StartVolumeScan");
    manager.Stop();
}

// index-durability-false-success-closure 6.1: a terminal for a volume with
// no scan in flight carries no authority -- it must never mark, and must
// never finalize a pass its scan did not own.
void TestDuplicateTerminalsWithoutScanInFlightAreRefused() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_duplicate_terminals.db")),
          "pipeline opens for duplicate-terminal test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 62;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    // A pass exists with no scan feeding it (stale-state shape).
    pipeline.BeginReconciliationPass(volumeId);
    // Give the volume a persisted journal identity so the public
    // journal-open path can legitimately request a reconciliation below.
    Check(pipeline.SetJournalPosition(volumeId, 10, 50), "stale-terminal fixture persists a journal identity");

    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "a completion without a scan in flight never marks the volume indexed");
    Check(pipeline.IsReconciliationPassActive(volumeId),
          "a stale completion does not finalize a pass its scan did not own");

    connection.scanFailedCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.IsReconciliationPassActive(volumeId),
          "a stale failure leaves pass state untouched too");

    // The pass is recoverable: the next genuine reconciliation (a journal
    // identity change -- the public trigger path) starts cleanly and a
    // fresh Begin replaces the orphaned pass.
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 11, 900);
    Check(pipeline.IsReconciliationPassActive(volumeId),
          "a real reconciliation can still be started over the orphaned pass");
    Check(manager.sessionsByEphemeralId_.at(7).reconciliationScan,
          "the started scan is tagged as the pass owner");
}

// index-durability-false-success-closure 6.1/6.4 (PA-003): a scan whose
// batch failed to apply has a guaranteed coverage hole at the frozen
// cursor; its later ScanComplete must not mark the volume indexed. The
// volume must still be markable by a subsequent clean scan.
void TestBatchFailedScanCompletionIsNotAuthoritative() {
    ffengine::IndexPipeline pipeline;
    const std::string dbPath = FreshDbPath("engine_test_batch_failed_completion.db");
    Check(pipeline.Open(dbPath), "pipeline opens for batch-failure test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 63;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.reconciliationScan = true;
    pipeline.BeginReconciliationPass(volumeId);

    // Force a real store-level batch failure: an exclusive lock on the
    // database makes the ingestion transaction fail immediately (no busy
    // timeout is set on the writer connection), exercising the genuine
    // apply-failure path rather than a fake one.
    sqlite3* locker = nullptr;
    bool locked = false;
    // Flush the pipeline's WAL first (passive maintenance checkpoint) so
    // establishing the exclusive lock is not racing the engine's own
    // un-checkpointed pages.
    pipeline.RunStoreMaintenance();
    if (sqlite3_open(dbPath.c_str(), &locker) == SQLITE_OK) {
        locked = sqlite3_exec(locker, "BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    Check(locked, "exclusive lock established for the batch-failure fixture");
    const std::vector<ffprotocol::MftRecordV1> failingBatch{
        MakeMftRecord(5, 5, u"", 0x10, 0),
    };
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{9}, failingBatch);
    if (locker) {
        sqlite3_exec(locker, "COMMIT;", nullptr, nullptr, nullptr);
        sqlite3_close(locker);
    }
    Check(session.scanBatchFailed, "a rejected batch marks the scan non-authoritative");
    Check(pipeline.GetVolumeMetadata(volumeId)->scanCursor.empty(),
          "the rejected batch leaves the stored cursor untouched");

    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "completion after a failed batch does not mark the volume indexed (PA-003)");
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "completion after a failed batch aborts the pass instead of deleting live rows");

    // A pause/resume cycle re-issues the unfinished scan through the real
    // (re)start path -- which resets the authority slate -- and the clean
    // scan that follows marks completion normally.
    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);
    Check(session.scanInProgress, "pause/resume re-issued the unfinished scan");
    Check(!session.scanBatchFailed, "a (re)started scan begins with a clean authority slate");
    const std::vector<ffprotocol::MftRecordV1> resumedBatch{
        MakeMftRecord(5, 5, u"", 0x10, 0),
        MakeMftRecord(120, 5, u"after-resume.txt", 0, 3),
    };
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{4, 5, 6}, resumedBatch);
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "a later clean scan marks completion -- the failure never poisons the volume");
}

// index-durability-false-success-closure 6.1 (PA-005): while indexing is
// paused, inbound batches are dropped -- a completion arriving in that
// window describes a scan whose records were thrown away and must not mark.
void TestPausedCompletionIsNotAuthoritative() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_paused_completion.db")),
          "pipeline opens for paused-completion test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 64;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.reconciliationScan = true;
    pipeline.BeginReconciliationPass(volumeId);

    manager.SetIndexingPaused(0, true);
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "a completion received while paused does not mark the volume indexed (PA-005)");
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "a paused completion aborts the pass -- its seen-set is missing every dropped batch");
    Check(pipeline.GetVolumeMetadata(volumeId)->lastReconciliationTime == 0,
          "the paused completion leaves the cadence clock where it was");

    manager.SetIndexingPaused(0, false); // resume re-issues the scan from the stored position
    Check(session.scanInProgress, "resume re-issues the unfinished scan");
    const std::vector<ffprotocol::MftRecordV1> resumedBatch{
        MakeMftRecord(5, 5, u"", 0x10, 0),
    };
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{1}, resumedBatch);
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the post-resume authoritative completion marks normally");
}

// index-durability-false-success-closure 6.2 (PA-004): a journal-gap
// reconciliation request that arrives while the initial scan is still in
// flight must not start a second pass under the scan; it is deferred and
// issued cleanly after the scan's authoritative completion.
void TestJournalGapDuringInFlightScanDefersUntilCompletion() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_deferred_reconciliation.db")),
          "pipeline opens for deferred-reconciliation test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 65;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "deferred-reconciliation test creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true; // initial scan in flight

    connection.journalResumeInvalidCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "a gap request during an in-flight scan begins no pass (PA-004)");
    Check(session.deferredReconciliation, "the gap request is deferred to the scan's completion");
    const auto reopenFrame = ReadCapturedMessage(capture->client);
    Check(reopenFrame && *reopenFrame == static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal),
          "only the journal reopen rides the wire while reconciliation is deferred");

    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the in-flight initial scan completes authoritatively");
    Check(pipeline.IsReconciliationPassActive(volumeId),
          "the deferred reconciliation starts once the scan authority is free");
    Check(session.reconciliationScan, "the deferred scan is tagged as the new pass owner");
    Check(!session.deferredReconciliation, "the deferred request is consumed on drain");
    const auto scanFrame = ReadCapturedMessage(capture->client);
    Check(scanFrame && *scanFrame == static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan),
          "the deferred reconciliation issues its low-priority scan after completion");
}

// Regression: Stop() must return promptly instead of waiting out the scheduler's
// 10-minute poll interval. The scheduler wakeup used to be lost when Stop() set
// running_ and notified outside wakeMutex_ while the scheduler was between its
// predicate re-check and its wait_for registration; a fast start/stop loop
// exercises that window, and a regression makes this suite exceed its 120 s
// CTest timeout rather than stalling a real engine shutdown for ten minutes.
void TestStopReturnsWithoutWaitingForTheSchedulerInterval() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_stop_promptness.db")),
          "stop-promptness fixture pipeline opens");

    constexpr int kIterations = 2000;
    const auto started = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        ffengine::PrivilegedConnection connection;
        ffengine::VolumeSessionManager manager(pipeline, connection);
        manager.Start();
        manager.Stop();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    Check(elapsed < std::chrono::seconds(30),
          "repeated Start/Stop never waits out the reconciliation scheduler interval");
}

// harden-ipc-trust-boundary-gaps 2.1/2.2: a failed service-peer identity
// revalidation on the heartbeat cadence must leave Active for degraded mode.
// The injectable verifier seam stands in for the production image+pin check,
// so this test needs no production pins and no installed service.
void TestHeartbeatServicePeerRevalidationDropsToDisconnected() {
    using namespace std::chrono_literals;
    const HANDLE server = CreateNamedPipeW(
        ffsetup::kCtrlPipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 64 * 1024, 64 * 1024, 0, nullptr);
    if (server == INVALID_HANDLE_VALUE) {
        std::cout << "service control pipe unavailable; skipping heartbeat revalidation test\n";
        return;
    }
    std::atomic<bool> stopService{false};
    std::thread service([&] {
        if (ConnectNamedPipe(server, nullptr) == FALSE && GetLastError() != ERROR_PIPE_CONNECTED) return;
        const auto handshake = ffipc::ReadFrame(server);
        if (!handshake) return;
        ffprotocol::HandshakeAckPayload ack{};
        ack.negotiatedVersion = ffprotocol::kCurrentProtocolVersion;
        if (!ffipc::WriteFrame(server, static_cast<uint16_t>(ffprotocol::MessageType::HandshakeAck), &ack, sizeof(ack))) return;
        while (!stopService.load(std::memory_order_relaxed)) {
            const auto frame = ffipc::ReadFrame(server);
            if (!frame) break;
            const auto type = ffprotocol::ToMessageType(frame->header.messageType);
            if (type && *type == ffprotocol::MessageType::Heartbeat) {
                ffipc::WriteFrame(server, static_cast<uint16_t>(ffprotocol::MessageType::HeartbeatAck));
            }
        }
    });

    ffengine::PrivilegedConnection connection;
    std::atomic<int> verifyCalls{0};
    connection.SetServiceIdentityVerifierForTesting([&verifyCalls](HANDLE) {
        return verifyCalls.fetch_add(1, std::memory_order_relaxed) == 0;
    });
    std::atomic<bool> sawActive{false};
    std::atomic<bool> leftActive{false};
    connection.Start(L"C:\\FastFilesHeartbeatTestInstall",
        [&sawActive, &leftActive](ffengine::ConnectionState state, ffengine::UnavailableReason) {
            if (state == ffengine::ConnectionState::Active) sawActive.store(true, std::memory_order_relaxed);
            else if (sawActive.load(std::memory_order_relaxed)) leftActive.store(true, std::memory_order_relaxed);
        });
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!leftActive.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    connection.Stop();
    stopService.store(true, std::memory_order_relaxed);
    if (service.joinable()) service.join();
    CloseHandle(server);

    Check(sawActive.load(), "the heartbeat fixture reached Active");
    Check(verifyCalls.load() >= 2,
          "the Active connection re-ran service identity verification on the heartbeat cadence");
    Check(leftActive.load(), "a failed heartbeat revalidation left Active instead of dropping to degraded mode");
}

// final-residual-hardening-and-hosted-ci-recovery 2.4: a peer that closes
// after delivering a real partial frame must release the Active reader, must
// not dispatch a completion callback, and must pass the test-only identity
// gate again on a newly connected pipe generation, and must drop a later
// generation when that gate rejects the service identity.
void TestPartialFrameDisconnectReconnectsAndReauthorizes() {
    using namespace std::chrono_literals;
    const std::wstring pipeName = L"\\\\.\\pipe\\FastFiles.ReconnectTest." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    HANDLE firstActive = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE secondActive = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(firstActive != nullptr && secondActive != nullptr,
          "reconnect fixture creates per-generation activity events");
    if (firstActive == nullptr || secondActive == nullptr) {
        if (firstActive != nullptr) CloseHandle(firstActive);
        if (secondActive != nullptr) CloseHandle(secondActive);
        return;
    }

    std::atomic<uint32_t> handshakeCount{0};
    std::atomic<bool> serverFailed{false};
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_partial_frame_reconnect.db")),
          "reconnect fixture index pipeline opens");
    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    manager.Start();
    std::thread service([&] {
        for (uint32_t generation = 0; generation < 3; ++generation) {
            HANDLE server = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 64 * 1024, 64 * 1024, 0, nullptr);
            if (server == INVALID_HANDLE_VALUE) {
                serverFailed.store(true, std::memory_order_release);
                return;
            }
            if (!ConnectOverlappedPipeWithTimeout(server, 8000)) {
                serverFailed.store(true, std::memory_order_release);
                CloseHandle(server);
                return;
            }

            if (generation == 1) {
                ffipc::ReceivedFrame unexpectedFrame;
                const auto status = ffipc::ReadFrameOverlappedDetailed(
                    server, nullptr, std::chrono::steady_clock::now() + 3s, &unexpectedFrame);
                if (status != ffipc::FrameReadStatus::Disconnected) {
                    serverFailed.store(true, std::memory_order_release);
                }
                CloseHandle(server);
                continue;
            }

            const auto request = ffipc::ReadFrameOverlapped(
                server, std::chrono::steady_clock::now() + 5s);
            if (!request || request->header.messageType != static_cast<uint16_t>(ffprotocol::MessageType::Handshake)) {
                serverFailed.store(true, std::memory_order_release);
                CloseHandle(server);
                return;
            }
            handshakeCount.fetch_add(1, std::memory_order_relaxed);
            ffprotocol::HandshakeAckPayload ack{ffprotocol::kCurrentProtocolVersion};
            if (!ffipc::WriteFrameOverlapped(server, static_cast<uint16_t>(ffprotocol::MessageType::HandshakeAck),
                                             &ack, sizeof(ack), std::chrono::steady_clock::now() + 5s)) {
                serverFailed.store(true, std::memory_order_release);
                CloseHandle(server);
                return;
            }

            if (generation == 0) {
                if (WaitForSingleObject(firstActive, 5000) != WAIT_OBJECT_0) {
                    serverFailed.store(true, std::memory_order_release);
                    CloseHandle(server);
                    return;
                }
                ffprotocol::FrameHeader partial{};
                partial.totalLength = static_cast<uint32_t>(sizeof(partial) + sizeof(ffprotocol::ScanCompletePayload));
                partial.structVersion = ffprotocol::kCurrentStructVersion;
                partial.messageType = static_cast<uint16_t>(ffprotocol::MessageType::ScanComplete);
                if (!WriteOneOverlappedByte(server, reinterpret_cast<const uint8_t*>(&partial)[0])) {
                    serverFailed.store(true, std::memory_order_release);
                }
                // Closing the service endpoint is the transport failure under
                // test. The client reader must drop the incomplete frame and
                // the connection loop must create a new pipe generation.
                CloseHandle(server);
                continue;
            }

            if (WaitForSingleObject(secondActive, 5000) != WAIT_OBJECT_0) {
                serverFailed.store(true, std::memory_order_release);
                CloseHandle(server);
                return;
            }
            for (;;) {
                const auto frame = ffipc::ReadFrameOverlapped(
                    server, std::chrono::steady_clock::now() + 5s);
                if (!frame) break;
                if (frame->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::Heartbeat)) {
                    if (!ffipc::WriteFrameOverlapped(server,
                            static_cast<uint16_t>(ffprotocol::MessageType::HeartbeatAck),
                            nullptr, 0, std::chrono::steady_clock::now() + 5s)) {
                        serverFailed.store(true, std::memory_order_release);
                        break;
                    }
                }
            }
            CloseHandle(server);
        }
    });

    connection.SetControlPipeNameForTesting(pipeName);
    std::atomic<uint32_t> identityChecks{0};
    connection.SetServiceIdentityVerifierForTesting([&identityChecks](HANDLE) {
        const uint32_t generation = identityChecks.fetch_add(1, std::memory_order_relaxed) + 1;
        return generation != 2;
    });
    std::atomic<uint32_t> activeCount{0};
    std::atomic<uint32_t> completionCallbacks{0};
    connection.SetScanCompleteCallback([&completionCallbacks](ffprotocol::VolumeId) {
        completionCallbacks.fetch_add(1, std::memory_order_relaxed);
    });
    std::atomic<bool> sessionAndRetryStateCleared{false};
    connection.Start(L"C:\\FastFilesReconnectFixture", [&](ffengine::ConnectionState state,
                                                               ffengine::UnavailableReason) {
        manager.OnConnectionStateChanged(state);
        if (state == ffengine::ConnectionState::Active) {
            const uint32_t activeGeneration = activeCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (activeGeneration == 1) {
                {
                    std::lock_guard<std::mutex> lock(manager.mutex_);
                    manager.sessionsByEphemeralId_[707].scanInProgress = true;
                }
                {
                    std::lock_guard<std::mutex> lock(manager.wakeMutex_);
                    manager.failedScanRetries_[707] = {std::chrono::steady_clock::now() + 1h, false};
                    ++manager.scanRetryGeneration_;
                }
            } else if (activeGeneration == 2) {
                bool sessionsEmpty = false;
                bool retriesEmpty = false;
                {
                    std::lock_guard<std::mutex> lock(manager.mutex_);
                    sessionsEmpty = manager.sessionsByEphemeralId_.empty();
                }
                {
                    std::lock_guard<std::mutex> lock(manager.wakeMutex_);
                    retriesEmpty = manager.failedScanRetries_.empty();
                }
                sessionAndRetryStateCleared.store(sessionsEmpty && retriesEmpty, std::memory_order_release);
            }
            if (activeGeneration == 1) SetEvent(firstActive);
            if (activeGeneration == 2) SetEvent(secondActive);
        }
    });

    const DWORD secondActiveResult = WaitForSingleObject(secondActive, 12000);
    manager.Stop();
    if (service.joinable()) service.join();
    CloseHandle(firstActive);
    CloseHandle(secondActive);

    Check(secondActiveResult == WAIT_OBJECT_0,
          "a partial frame disconnect is followed by a fresh Active connection");
    Check(handshakeCount.load() == 2,
          "only the initially authorized and later reauthorized pipes send handshakes");
    Check(identityChecks.load() >= 3,
          "service identity authorization runs again on each replacement connection");
    Check(completionCallbacks.load() == 0,
          "a truncated ScanComplete frame never reaches the engine completion callback");
    Check(sessionAndRetryStateCleared.load(std::memory_order_acquire),
          "disconnect clears ephemeral sessions and pending scan retries before reconnect");
    Check(!serverFailed.load(), "reconnect service fixture completed both transport generations cleanly");
}

} // namespace

int main() {
    TestMatchingJournalResumesWithoutReconciliation();
    TestChangedJournalTriggersReconciliation();
    TestDisappearanceMarksUnavailableAndWithdrawsPublishedVolume();
    TestCollectVolumeStatusReportsReachableScannedVolumes();
    TestCollectVolumeStatusSkipsDisabledAndUntracked();
    TestDisableTearsDownSessionLive();
    TestStopClosesOwnedJournalBeforeConnectionShutdown();
    TestPauseShownInStatusAndDropsBatchesWithoutAdvancing();
    TestInitialScanBatchesDoNotRepeatWholeSnapshotExports();
    TestPendingDecisionForObservedUnselectedVolume();
    TestScanFailedLeavesPriorIndexAuthorityIntact();
    TestScanFailedSchedulesRetry();
    TestDuplicateTerminalsWithoutScanInFlightAreRefused();
    TestBatchFailedScanCompletionIsNotAuthoritative();
    TestPausedCompletionIsNotAuthoritative();
    TestJournalGapDuringInFlightScanDefersUntilCompletion();
    TestStopReturnsWithoutWaitingForTheSchedulerInterval();
    TestHeartbeatServicePeerRevalidationDropsToDisconnected();
    TestPartialFrameDisconnectReconnectsAndReauthorizes();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
