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
// disconnect transfers pending intent to durable identity and manager stop
// clears it. The test measures repeated scheduled intervals, then makes the
// final retry due without changing production time.
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

// scan-generation-integrity-and-residual-certification-closure 10.2: a
// pause/resume is not allowed to bypass a delivered ScanFailed's five-second
// floor for an incomplete initial scan.
void TestPauseResumeHonorsPendingScanFailureFloor() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_pause_resume_retry_floor.db")),
          "pipeline opens for the pause/resume retry-floor test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 631;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the pause/resume retry-floor fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    connection.state_ = ffengine::ConnectionState::Active;
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 2;
    manager.active_ = true;
    manager.Start();
    manager.sessionsByEphemeralId_.at(7).scanInProgress = true;

    const auto failureAt = std::chrono::steady_clock::now();
    connection.scanFailedCallback_(ffprotocol::VolumeId{7});
    std::optional<std::chrono::steady_clock::time_point> originalDue;
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        const auto retry = manager.failedScanRetries_.find(7);
        if (retry != manager.failedScanRetries_.end()) originalDue = retry->second.due;
    }
    Check(originalDue.has_value(), "ScanFailed arms the pending retry slot");
    if (originalDue) {
        const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(*originalDue - failureAt);
        Check(delay >= 4900ms && delay <= 5100ms,
              "the pending retry starts at the five-second floor");
    }

    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);
    const auto resumeDeadline = std::chrono::steady_clock::now() + 2s;
    while (!HasCapturedBytes(capture->client) && std::chrono::steady_clock::now() < resumeDeadline) {
        std::this_thread::sleep_for(10ms);
    }
    std::optional<ffipc::ReceivedFrame> resumedFrame;
    if (HasCapturedBytes(capture->client)) resumedFrame = ffipc::ReadFrame(capture->client);
    Check(resumedFrame
              && resumedFrame->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal),
          "resume reopens the journal without issuing an early initial scan");
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        const auto retry = manager.failedScanRetries_.find(7);
        Check(originalDue && retry != manager.failedScanRetries_.end()
                  && retry->second.due == *originalDue,
              "resume preserves the original bounded retry deadline");
    }
    manager.Stop();
}

// A delivered reconciliation failure has already cleared scanInProgress by
// the time a transport disconnect arrives. Its retry must still move from the
// old ephemeral id to the durable volume and then to the new connection id.
void TestFailedReconciliationRetrySurvivesDisconnect() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_failed_reconciliation_disconnect.db")),
          "pipeline opens for the failed-reconciliation disconnect test");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the failed-reconciliation fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    connection.state_ = ffengine::ConnectionState::Active;
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 2;
    manager.Start();
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    const auto firstEnumeration = ffipc::ReadFrame(capture->client);
    Check(firstEnumeration
              && firstEnumeration->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes),
          "the original connection begins with volume enumeration");
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{7}, L'C'}});
    const auto initialScan = ffipc::ReadFrame(capture->client);
    const auto initialJournal = ffipc::ReadFrame(capture->client);
    Check(initialScan
              && initialScan->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan),
          "the original session starts its first scan");
    Check(initialJournal
              && initialJournal->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal),
          "the original session opens its journal");
    const auto first = manager.sessionsByEphemeralId_.find(7);
    Check(first != manager.sessionsByEphemeralId_.end(), "the original session is derived from the volume list");
    if (first == manager.sessionsByEphemeralId_.end()) {
        manager.Stop();
        return;
    }
    const auto volumeId = first->second.durableId;
    Check(pipeline.MarkScanComplete(volumeId), "the fixture volume is already indexed");
    Check(pipeline.SetJournalPosition(volumeId, 41, 900), "the fixture persists a journal position");
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    auto& firstSession = manager.sessionsByEphemeralId_.at(7);
    firstSession.scanInProgress = true;
    firstSession.reconciliationScan = true;
    pipeline.BeginReconciliationPass(volumeId);

    connection.scanFailedCallback_(ffprotocol::VolumeId{7});
    Check(!firstSession.scanInProgress,
          "ScanFailed closes the current reconciliation attempt before disconnect");
    std::chrono::steady_clock::time_point failureRetryDue{};
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        const auto retry = manager.failedScanRetries_.find(7);
        Check(retry != manager.failedScanRetries_.end() && retry->second.reconciliation,
              "ScanFailed queues a reconciliation retry for the current ephemeral id");
        if (retry != manager.failedScanRetries_.end()) failureRetryDue = retry->second.due;
    }

    const auto disconnectedAt = std::chrono::steady_clock::now();
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        const auto floor = manager.scanReissueFloors_.find(volumeId);
        Check(floor != manager.scanReissueFloors_.end() && floor->second.reconciliation,
              "disconnect transfers the finished reconciliation retry to durable volume identity");
        if (floor != manager.scanReissueFloors_.end()) {
            Check(floor->second.due >= failureRetryDue,
                  "the durable retry does not shorten the original failure deadline");
            const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
                floor->second.due - disconnectedAt);
            Check(delay >= 0ms && delay <= 5100ms,
                  "disconnect preserves an already-running failure floor without adding a new delay");
        }
    }
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        Check(manager.failedScanRetries_.empty(),
              "disconnect discards the obsolete ephemeral retry entry after transfer");
    }

    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    const auto enumerate = ffipc::ReadFrame(capture->client);
    Check(enumerate
              && enumerate->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes),
          "the replacement connection begins with volume enumeration");
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{8}, L'C'}});
    Check(manager.sessionsByEphemeralId_.contains(8) && !manager.sessionsByEphemeralId_.contains(7),
          "the replacement connection derives a new session id for the durable volume");
    Check(manager.sessionsByEphemeralId_.contains(8),
          "the replacement session reopens its journal while retaining the retry floor");
    std::optional<ffipc::ReceivedFrame> journalOpen;
    if (HasCapturedBytes(capture->client)) journalOpen = ffipc::ReadFrame(capture->client);
    Check(journalOpen
              && journalOpen->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal),
          "the replacement session reopens the journal before the queued retry");
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        const auto retry = manager.failedScanRetries_.find(8);
        Check(retry != manager.failedScanRetries_.end() && retry->second.reconciliation
                  && !manager.failedScanRetries_.contains(7),
              "the retry intent moves to exactly the new ephemeral id");
        if (retry != manager.failedScanRetries_.end()) {
            manager.failedScanRetries_.at(8).due = std::chrono::steady_clock::now() - 1ms;
            ++manager.scanRetryGeneration_;
        }
    }
    manager.wakeCv_.notify_one();
    const auto retryDeadline = std::chrono::steady_clock::now() + 2s;
    while (!HasCapturedBytes(capture->client) && std::chrono::steady_clock::now() < retryDeadline) {
        std::this_thread::sleep_for(10ms);
    }
    std::optional<ffipc::ReceivedFrame> retryFrame;
    if (HasCapturedBytes(capture->client)) retryFrame = ffipc::ReadFrame(capture->client);
    ffprotocol::StartVolumeScanRequest retryRequest{};
    const bool hasRetryRequest = retryFrame
        && retryFrame->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan)
        && retryFrame->payload.size() >= sizeof(retryRequest);
    if (hasRetryRequest) {
        std::memcpy(&retryRequest, retryFrame->payload.data(), sizeof(retryRequest));
    }
    Check(hasRetryRequest && retryRequest.volumeId.value == 8,
          "the durable retry starts reconciliation under the new connection-scoped id");
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

// legacy-protocol-correctness-and-raw-volume-cancellation-closure 3.4/5.2:
// losing the connection loses the scan that owned an in-flight reconciliation
// pass (this is exactly what the service's unreportable legacy failure does by
// tearing the connection down). The pass must be discarded -- its partial
// seen-set can never drive removals -- the volume must keep its durable state,
// and the old connection-scoped ids must carry no authority afterwards.
void TestConnectionLossAbortsReconciliationPassWithoutAuthority() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_connection_loss_reconciliation.db")),
          "pipeline opens for connection-loss reconciliation test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 71;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.ApplyMftBatch(volumeId, {MakeMftRecord(5, 5, u"", 0x10, 0),
                                            MakeMftRecord(100, 5, u"seed.txt", 0, 1)}),
          "the connection-loss fixture seeds a durable root + file entry");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.reconciliationScan = true;
    pipeline.BeginReconciliationPass(volumeId);
    Check(pipeline.IsReconciliationPassActive(volumeId), "the reconciliation pass is active before the loss");

    manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);

    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "connection loss discards the pass whose scan can no longer complete");
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "connection loss never marks the interrupted scan complete");
    Check(pipeline.GetVolumeMetadata(volumeId)->lastReconciliationTime == 0,
          "connection loss does not advance the reconciliation cadence clock");
    Check(manager.sessionsByEphemeralId_.empty(),
          "connection loss clears every connection-scoped engine session");
    {
        const auto snapshot = pipeline.ExportDirectorySnapshot(volumeId, L"C:");
        const auto root = snapshot.find(L"C:");
        bool seedPresent = false;
        if (root != snapshot.end()) {
            for (const auto& entry : root->second.entries) {
                if (entry.name == L"seed.txt") seedPresent = true;
            }
        }
        Check(seedPresent,
              "discarding the pass deletes no durable records from incomplete coverage");
    }

    // A terminal for the prior connection's id has no session to act on and
    // therefore no authority at all -- the next Active period re-derives
    // identity from a fresh EnumerateVolumes instead of reusing it.
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "a stale connection-scoped completion cannot mark the volume after the loss");

    // Recovery is not poisoned: a fresh Active period installs a new
    // connection-scoped id and a genuine completion still marks the volume.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    Check(manager.sessionsByEphemeralId_.empty(),
          "a later Active period starts from freshly derived connection state");
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        manager.sessionsByEphemeralId_.emplace(
            8, ffengine::VolumeSessionManager::VolumeSession{volumeId, L'C', 0, false});
        manager.sessionsByEphemeralId_.at(8).scanInProgress = true;
    }
    connection.scanCompleteCallback_(ffprotocol::VolumeId{8});
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the next genuine completion still marks the volume indexed");
    manager.Stop();
}

// legacy-protocol-correctness-and-raw-volume-cancellation-closure 8.6
// (reviewer finding A-F1/C-MAJOR): an unreportable legacy failure ends the
// connection without a terminal, so the delivered-ScanFailed retry queue is
// never armed and the connection-scoped session is cleared. The replacement
// connection must not restart a persistently failing scan immediately; the
// durable per-volume reissue floor keeps the documented five-second cadence.
void TestLegacyFailureReissueIsThrottledAfterConnectionLoss() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_legacy_reissue_floor.db")),
          "pipeline opens for the legacy reissue-floor test");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the legacy reissue-floor fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 1;
    // A persistent selection makes the volume eligible for the normal
    // OnVolumeList start path on every connection generation. The volume is
    // resolved through the manager's own production path so the durable
    // identity is the one a real reconnect would see.
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.Start();

    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    auto readNextFrame = [&capture]() {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!HasCapturedBytes(capture->client) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }
        return HasCapturedBytes(capture->client) ? ffipc::ReadFrame(capture->client) : std::nullopt;
    };
    auto drainFrames = [&capture]() {
        while (HasCapturedBytes(capture->client)) {
            if (!ffipc::ReadFrame(capture->client)) break;
        }
    };
    const auto firstEnumeration = readNextFrame();
    Check(firstEnumeration && firstEnumeration->header.messageType ==
              static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes),
          "the first Active period re-derives volumes from EnumerateVolumes");
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{7}, L'C'}});
    if (manager.sessionsByEphemeralId_.empty()) {
        Check(false, "the fixture host resolves a scannable C: volume");
        manager.Stop();
        return;
    }
    const auto firstScan = readNextFrame();
    Check(firstScan && firstScan->header.messageType ==
              static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan),
          "the first connection starts the volume's initial scan");
    drainFrames();
    manager.sessionsByEphemeralId_.at(7).scanInProgress = true;

    // The pre-{2,2} service aborted the connection while that scan was still
    // in flight: no terminal of any kind was delivered.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
    Check(manager.sessionsByEphemeralId_.empty(),
          "the connection loss clears the connection-scoped session");
    Check(manager.scanReissueFloors_.size() == 1,
          "the connection loss records a durable reissue floor for the interrupted scan");

    // The replacement connection re-enumerates and finds the same volume.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    const auto secondEnumeration = readNextFrame();
    Check(secondEnumeration && secondEnumeration->header.messageType ==
              static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes),
          "the fresh Active period re-derives volumes from EnumerateVolumes");
    drainFrames();
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{7}, L'C'}});

    Check(!HasCapturedBytes(capture->client),
          "the post-teardown scan reissue is deferred instead of immediate");
    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        queued = manager.failedScanRetries_.contains(7);
        if (queued) {
            manager.failedScanRetries_.at(7).due = std::chrono::steady_clock::now() - 1ms;
            ++manager.scanRetryGeneration_;
        }
    }
    Check(queued, "the deferred reissue is queued on the bounded retry scheduler");
    manager.wakeCv_.notify_one();

    const auto message = ReadCapturedMessage(capture->client);
    Check(message == static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan),
          "the due floor reissues StartVolumeScan on the replacement connection");
    manager.Stop();
}

// legacy-protocol-correctness-and-raw-volume-cancellation-closure 8.6
// (reviewer finding A-F2): batches dropped while paused are a coverage hole.
// If the volume resumes before the terminal arrives, the old pause-at-terminal
// check no longer sees the drop, so a reconciliation pass could finalize a
// seen-set missing the dropped window and delete still-valid rows. The dropped
// batch must make the in-flight scan non-authoritative.
void TestPausedReconciliationDropIsNonAuthoritative() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_paused_reconciliation_drop.db")),
          "pipeline opens for the paused-reconciliation-drop test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 82;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.ApplyMftBatch(volumeId, {MakeMftRecord(5, 5, u"", 0x10, 0),
                                            MakeMftRecord(100, 5, u"seed.txt", 0, 1)}),
          "the paused-drop fixture seeds a durable root + file entry");
    Check(pipeline.MarkScanComplete(volumeId), "the fixture volume is already fully indexed");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.reconciliationScan = true;
    pipeline.BeginReconciliationPass(volumeId);

    manager.SetIndexingPaused(0, true);
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{9, 9, 9},
                                  {MakeMftRecord(200, 5, u"during-pause.txt", 0, 2)});
    Check(session.scanBatchFailed,
          "a batch dropped while paused marks the in-flight scan non-authoritative");

    // The volume resumes before the scan's terminal arrives.
    manager.SetIndexingPaused(0, false);
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{10, 10, 10},
                                  {MakeMftRecord(300, 5, u"after-resume.txt", 0, 3)});
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});

    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the incomplete reconciliation pass is discarded, not finalized");
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "an already-indexed volume keeps its completion state through the dropped window");
    {
        const auto snapshot = pipeline.ExportDirectorySnapshot(volumeId, L"C:");
        const auto root = snapshot.find(L"C:");
        bool seedPresent = false;
        if (root != snapshot.end()) {
            for (const auto& entry : root->second.entries) {
                if (entry.name == L"seed.txt") seedPresent = true;
            }
        }
        Check(seedPresent,
              "no durable row is deleted from the partial seen-set left by the paused window");
    }
}

void DrainCapturedFrames(HANDLE client) {
    while (HasCapturedBytes(client)) {
        if (!ffipc::ReadFrame(client)) break;
    }
}

std::optional<ffipc::ReceivedFrame> ReadCapturedFrameWithTimeout(HANDLE client,
                                                                 std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!HasCapturedBytes(client) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!HasCapturedBytes(client)) return std::nullopt;
    return ffipc::ReadFrame(client);
}

std::optional<uint16_t> ReadCapturedMessageWithTimeout(HANDLE client, std::chrono::milliseconds timeout) {
    const auto frame = ReadCapturedFrameWithTimeout(client, timeout);
    if (!frame) return std::nullopt;
    return frame->header.messageType;
}

struct CapturedScanStart {
    uint32_t volumeId = 0;
    std::vector<uint8_t> cursor;
};

std::optional<CapturedScanStart> ReadCapturedScanStartWithTimeout(HANDLE client, std::chrono::milliseconds timeout) {
    const auto frame = ReadCapturedFrameWithTimeout(client, timeout);
    if (!frame || frame->header.messageType != static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan)) {
        return std::nullopt;
    }
    if (frame->payload.size() < sizeof(ffprotocol::StartVolumeScanRequest)) return std::nullopt;
    ffprotocol::StartVolumeScanRequest request{};
    std::memcpy(&request, frame->payload.data(), sizeof(request));
    if (frame->payload.size() != sizeof(request) + request.resumeCursorLengthBytes) return std::nullopt;
    CapturedScanStart captured;
    captured.volumeId = request.volumeId.value;
    captured.cursor.assign(frame->payload.begin() + sizeof(request), frame->payload.end());
    return captured;
}

bool HasDurableRow(ffengine::IndexPipeline& pipeline, ffindexstore::VolumeRowId volumeId,
                   const std::wstring& name) {
    const auto snapshot = pipeline.ExportDirectorySnapshot(volumeId, L"C:");
    const auto root = snapshot.find(L"C:");
    if (root == snapshot.end()) return false;
    for (const auto& entry : root->second.entries) {
        if (entry.name == name) return true;
    }
    return false;
}

// The reconciliation scheduler treats a complete volume whose
// lastReconciliationTime is still zero as immediately due ("reconcile once
// after the first scan"). Synthetic fixtures that need a stable pass state
// anchor that clock to now so the scheduler's ordinary sweep cannot race the
// assertions; the production rules are unchanged.
uint64_t CurrentFileTimeForTest() {
    FILETIME fileTime{};
    GetSystemTimeAsFileTime(&fileTime);
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return value.QuadPart;
}

// scan-generation-integrity-and-residual-certification-closure 2.1: the order
// reported by PR #9's review. An incomplete initial scan applies a batch,
// indexing is paused, a later batch is dropped (a coverage hole), indexing is
// resumed so replacement work is issued for the same connection-scoped volume
// id, and the ORIGINAL attempt's completion is delivered before the
// replacement finishes. ScanComplete carries only that volume id, so the
// engine cannot order the terminal after the replacement: it must neither mark
// the volume complete nor erase the coverage hole. The test's own phases are
// the only evidence that distinguishes the two attempts.
void TestResumeDoesNotClearCoverageHoleForAnOlderCompletion() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_resume_older_completion.db")),
          "pipeline opens for the resume/older-completion test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 91;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);
    pipeline.SetLastReconciliationTime(volumeId, CurrentFileTimeForTest());

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the resume/older-completion fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.active_ = true;
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true; // attempt 1 is the in-flight initial scan

    const std::vector<ffprotocol::MftRecordV1> appliedBatch{
        MakeMftRecord(5, 5, u"", 0x10, 0),
        MakeMftRecord(100, 5, u"users", 0x10, 0),
    };
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{1, 2, 3}, appliedBatch);
    Check(pipeline.GetVolumeMetadata(volumeId)->entryCount == 2,
          "attempt 1 applies its batch");
    Check(pipeline.GetVolumeMetadata(volumeId)->scanCursor == std::vector<uint8_t>({1, 2, 3}),
          "attempt 1 advances the durable cursor to its last applied position");
    DrainCapturedFrames(capture->client);

    manager.SetIndexingPaused(0, true);
    const std::vector<ffprotocol::MftRecordV1> droppedBatch{
        MakeMftRecord(200, 5, u"dropped-while-paused", 0x10, 0),
    };
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{4, 5, 6}, droppedBatch);
    Check(session.scanBatchFailed, "the pause drop records a coverage hole against attempt 1");
    Check(pipeline.GetVolumeMetadata(volumeId)->scanCursor == std::vector<uint8_t>({1, 2, 3}),
          "the dropped batch leaves the cursor frozen at the last applied position");

    manager.SetIndexingPaused(0, false);
    const auto replacement = ReadCapturedScanStartWithTimeout(capture->client, 2s);
    Check(replacement && replacement->volumeId == 7,
          "resume issues replacement work on the same connection-scoped volume id");
    DrainCapturedFrames(capture->client);

    // Attempt 1's completion was already written to the pipe before the resume.
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the older completion never marks the volume complete");
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the older completion finalizes no reconciliation pass");
    Check(session.scanInProgress && session.scanUnretiredAttempts == 0,
          "retiring attempt 1 leaves the replacement in flight with no older terminal outstanding");

    // The replacement now fails before covering the hole. Its own terminal
    // ends that attempt and arms the bounded retry from the durable cursor.
    connection.scanFailedCallback_(ffprotocol::VolumeId{7});
    Check(!session.scanInProgress,
          "the replacement's own failure releases its scan ownership");

    // The hole is still owed: the retirement consumed one terminal and the
    // bounded retry reissues the coverage from the frozen cursor.
    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        queued = manager.failedScanRetries_.contains(7);
        if (queued) {
            manager.failedScanRetries_.at(7).due = std::chrono::steady_clock::now() - 1ms;
            ++manager.scanRetryGeneration_;
        }
    }
    Check(queued, "the refused coverage hole stays queued on the bounded retry slot");
    manager.wakeCv_.notify_one();

    const auto retried = ReadCapturedScanStartWithTimeout(capture->client, 2s);
    Check(retried && retried->volumeId == 7,
          "the due retry reissues the initial scan on the same volume id");
    Check(retried && retried->cursor == std::vector<uint8_t>({1, 2, 3}),
          "the retried attempt starts from the last durably applied cursor, not from zero");
    Check(manager.sessionsByEphemeralId_.at(7).scanInProgress,
          "the retried attempt is in flight");

    // The retried attempt covers the frozen hole and completes authoritatively.
    const std::vector<ffprotocol::MftRecordV1> retriedBatch{
        MakeMftRecord(200, 5, u"dropped-while-paused", 0x10, 0),
    };
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{4, 5, 6}, retriedBatch);
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the attempt that applied the missing coverage marks the volume complete");
    manager.Stop();
}

// 2.2: the already-indexed reconciliation form of the same pause-then-terminal
// order. The dangerous outcome there is a stale completion finalizing a
// reconciliation pass whose seen-set is missing the dropped window -- that
// finalization is what deletes live rows. The test drives the exact order and
// asserts the pass is discarded and every durable row survives; the
// source-backed reason an already-indexed resume cannot clear the hole is
// recorded in the campaign evidence (the already-complete branch never issues
// a replacement scan, and TriggerReconciliation defers while one is in
// flight, so no terminal can be ordered after a replacement).
void TestPausedReconciliationOlderCompletionAfterResumeDeletesNothing() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_paused_reconciliation_older_completion.db")),
          "pipeline opens for the already-indexed reconciliation-order test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 92;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.ApplyMftBatch(volumeId, {MakeMftRecord(5, 5, u"", 0x10, 0),
                                            MakeMftRecord(100, 5, u"seed.txt", 0, 1)}),
          "the fixture seeds a durable root + file entry");
    Check(pipeline.MarkScanComplete(volumeId), "the fixture volume is already fully indexed");
    Check(pipeline.SetJournalPosition(volumeId, 41, 900), "the fixture persists a journal identity");
    const uint64_t cadenceBefore = CurrentFileTimeForTest();
    Check(pipeline.SetLastReconciliationTime(volumeId, cadenceBefore),
          "the fixture anchors the reconciliation cadence clock");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the already-indexed reconciliation fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.active_ = true;
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.reconciliationScan = true;
    pipeline.BeginReconciliationPass(volumeId);
    DrainCapturedFrames(capture->client);

    manager.SetIndexingPaused(0, true);
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{4, 5, 6},
                                  {MakeMftRecord(200, 5, u"during-pause", 0x10, 0)});
    Check(session.scanBatchFailed, "the pause drop records the pass's coverage hole");

    manager.SetIndexingPaused(0, false);
    const auto resumeFrame = ReadCapturedMessageWithTimeout(capture->client, 2s);
    Check(resumeFrame && *resumeFrame == static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal),
          "resume of an already-indexed volume reopens the journal instead of issuing a replacement scan");
    Check(!pipeline.IsReconciliationPassActive(volumeId) || !session.scanUnretiredAttempts,
          "no replacement attempt was issued over the already-indexed pass's hole");
    DrainCapturedFrames(capture->client);

    // The original attempt's completion arrives after the resume.
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the incomplete pass is discarded, not finalized on a partial seen-set");
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the already-indexed volume keeps its completion state through the dropped window");
    Check(pipeline.GetVolumeMetadata(volumeId)->lastReconciliationTime == cadenceBefore,
          "the discarded pass leaves the reconciliation cadence clock untouched");
    Check(HasDurableRow(pipeline, volumeId, L"seed.txt"),
          "no durable row is deleted from the incomplete pass's seen-set");

    // Recovery is not poisoned: a later reconciliation pass runs to an
    // authoritative completion and only then finalizes.
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 99, 1234); // journal identity changed
    Check(pipeline.IsReconciliationPassActive(volumeId),
          "a later journal discontinuity begins a fresh reconciliation pass");
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{7, 8, 9},
                                  {MakeMftRecord(5, 5, u"", 0x10, 0),
                                   MakeMftRecord(100, 5, u"seed.txt", 0, 1)});
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the later authoritative completion finalizes its own pass");
    Check(pipeline.GetVolumeMetadata(volumeId)->lastReconciliationTime != 0,
          "only the authoritative completion advances the cadence clock");
    Check(HasDurableRow(pipeline, volumeId, L"seed.txt"),
          "the observed row survives the authoritative pass too");
    manager.Stop();
}

// scan-generation-integrity-and-residual-certification-closure 10.2: retiring
// a late terminal from the superseded attempt must not release the replacement
// scan's ownership. A journal discontinuity can arrive while that replacement
// is still active; its terminal must not be reinterpreted as a reconciliation
// terminal and finalize a seen-set that only covers records after the frozen
// initial-scan cursor.
void TestRetiredTerminalCannotAuthorizeLaterReconciliation() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_retired_terminal_reconciliation_owner.db")),
          "pipeline opens for the retired-terminal reconciliation-ownership test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 991;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);
    pipeline.SetJournalPosition(volumeId, 41, 900);
    pipeline.SetLastReconciliationTime(volumeId, CurrentFileTimeForTest());

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the reconciliation-ownership fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.active_ = true;
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true; // attempt 1: the incomplete initial scan

    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{1, 2, 3},
                                  {MakeMftRecord(5, 5, u"", 0x10, 0),
                                   MakeMftRecord(100, 5, u"seed.txt", 0, 1)});
    Check(HasDurableRow(pipeline, volumeId, L"seed.txt"),
          "the incomplete scan has already indexed a durable row");
    manager.SetIndexingPaused(0, true);
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{4, 5, 6},
                                  {MakeMftRecord(200, 5, u"paused-window", 0x10, 0)});
    Check(session.scanBatchFailed, "the pause drop records the original attempt's coverage hole");
    manager.SetIndexingPaused(0, false);
    const auto replacement = ReadCapturedScanStartWithTimeout(capture->client, 2s);
    Check(replacement && replacement->volumeId == 7,
          "resume issues replacement work under the same connection-scoped volume id");
    Check(replacement && replacement->cursor == std::vector<uint8_t>({1, 2, 3}),
          "the replacement resumes from the last applied cursor");
    Check(session.scanUnretiredAttempts == 1,
          "the replacement retains exactly one unretired initial-scan attempt");
    DrainCapturedFrames(capture->client);

    // This terminal retires attempt 1. The replacement remains active and owns
    // the volume even though its wire terminal has the same volume id.
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(session.scanUnretiredAttempts == 0,
          "the original terminal retires exactly one superseded attempt");
    Check(session.scanInProgress,
          "retiring the older terminal preserves the replacement scan's in-flight ownership");

    // A changed journal id requests reconciliation while the replacement still
    // covers the initial-scan hole. That request must wait for the replacement
    // instead of borrowing its volume-id-only terminal.
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 99, 1234);
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "journal discontinuity defers reconciliation while replacement work is active");
    Check(session.deferredReconciliation,
          "the deferred reconciliation intent remains attached to the active session");

    // The replacement only covers entries after the frozen cursor. Its own
    // completion authorizes the initial scan, then the deferred full pass sees
    // the existing row before it is allowed to finalize.
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{4, 5, 6},
                                  {MakeMftRecord(200, 5, u"paused-window", 0x10, 0)});
    Check(session.scanUnretiredAttempts == 0 && session.scanInProgress,
          "the replacement remains the active attempt after the older terminal retires");
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.IsReconciliationPassActive(volumeId),
          "the deferred full reconciliation starts only after the replacement completes");
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{7, 8, 9},
                                  {MakeMftRecord(5, 5, u"", 0x10, 0),
                                   MakeMftRecord(100, 5, u"seed.txt", 0, 1),
                                   MakeMftRecord(200, 5, u"paused-window", 0x10, 0)});
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the later authoritative reconciliation finalizes normally");
    Check(HasDurableRow(pipeline, volumeId, L"seed.txt"),
          "no stale terminal deletes the row omitted by the replacement cursor window");
    const auto finalMetadata = pipeline.GetVolumeMetadata(volumeId);
    Check(finalMetadata && finalMetadata->entryCount == 3,
          "durable row count remains complete after reconciliation sees only the resumed cursor window");
    manager.Stop();
}

// 3.3 (ordinary success): a pause/resume whose replacement applies every batch
// is still authoritative as soon as the terminal is ordered after that
// replacement, and disconnect cannot restore a stale terminal afterwards.
void TestReplacementCompletionStillMarksWithoutADroppedBatch() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_replacement_completion.db")),
          "pipeline opens for the replacement-completion test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 93;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetLastReconciliationTime(volumeId, CurrentFileTimeForTest());

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the replacement-completion fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.active_ = true;
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{1},
                                  {MakeMftRecord(5, 5, u"", 0x10, 0)});
    DrainCapturedFrames(capture->client);

    manager.SetIndexingPaused(0, true); // no batch arrives while paused: no hole
    manager.SetIndexingPaused(0, false);
    Check(session.scanInProgress, "resume re-issues the unfinished scan");
    Check(!session.scanUnretiredAttempts,
          "a replacement over an attempt with no coverage hole is not ambiguous");
    DrainCapturedFrames(capture->client);

    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{2},
                                  {MakeMftRecord(100, 5, u"after-resume.txt", 0, 2)});
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the replacement's ordered completion still marks the volume complete");

    // A terminal for the retired connection-scoped id carries no authority
    // once the connection is gone.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "a terminal after disconnect neither removes nor restores scan authority");
    manager.Stop();
}

// 4.1/4.2: an already-indexed volume whose reconciliation work is interrupted
// by connection loss must transfer its durable five-second reissue floor onto
// the replacement connection's own connection-scoped volume id -- and must
// never address the discarded id.
void TestInterruptedReconciliationRetryReattachesToTheNewVolumeId() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_interrupted_reconciliation_retry.db")),
          "pipeline opens for the interrupted-reconciliation retry test");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the interrupted-reconciliation fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 1;
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.Start();

    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    DrainCapturedFrames(capture->client);
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{7}, L'C'}});
    if (manager.sessionsByEphemeralId_.empty()) {
        Check(false, "the fixture host resolves a scannable C: volume");
        manager.Stop();
        return;
    }
    const auto durableId = manager.sessionsByEphemeralId_.at(7).durableId;
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{1},
                                  {MakeMftRecord(5, 5, u"", 0x10, 0),
                                   MakeMftRecord(100, 5, u"seed.txt", 0, 1)});
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.GetVolumeMetadata(durableId)->scanComplete,
          "the fixture volume finishes its initial scan before the interruption");
    DrainCapturedFrames(capture->client);

    // A journal-identity change is a real reconciliation trigger on an
    // already-indexed volume.
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 41, 900);
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 42, 1000);
    Check(pipeline.IsReconciliationPassActive(durableId),
          "the interrupted work is a reconciliation pass of the already-indexed volume");
    Check(manager.sessionsByEphemeralId_.at(7).reconciliationScan,
          "the in-flight scan owns that pass");
    DrainCapturedFrames(capture->client);

    const auto lostAt = std::chrono::steady_clock::now();
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        const auto floor = manager.scanReissueFloors_.find(durableId);
        Check(floor != manager.scanReissueFloors_.end() && floor->second.reconciliation,
              "connection loss records a durable reconciliation reissue floor");
        if (floor != manager.scanReissueFloors_.end()) {
            const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(floor->second.due - lostAt);
            Check(delay >= 4900ms && delay <= 5100ms,
                  "the durable floor is measured at the five-second cadence");
        }
    }
    Check(!pipeline.IsReconciliationPassActive(durableId),
          "the interrupted pass is discarded rather than finalized");

    // The five-second delay was asserted above. Keep the synthetic floor
    // future-dated during connection handoff so its natural deadline cannot
    // expire while the fixture reads and drains the journal-reopen frame.
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        const auto floor = manager.scanReissueFloors_.find(durableId);
        if (floor != manager.scanReissueFloors_.end()) {
            floor->second.due = std::chrono::steady_clock::now() + 30s;
        }
    }

    // Replacement connection: same durable volume, a NEW connection-scoped id.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    DrainCapturedFrames(capture->client);
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{8}, L'C'}});
    Check(manager.sessionsByEphemeralId_.contains(8) && !manager.sessionsByEphemeralId_.contains(7),
          "the replacement connection derives a new connection-scoped session");
    const auto enumerationFrame = ReadCapturedMessageWithTimeout(capture->client, 500ms);
    Check(!enumerationFrame || *enumerationFrame == static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal),
          "the enumeration path issues no scan while the durable floor is unexpired");
    // Drain the enumeration's journal reopen while the retry is still
    // future-dated. ArmScanReissue already wakes the scheduler during the
    // volume-list callback; once the fixture expires the floor, a retry can
    // race any later drain and be consumed before the assertion reads it.
    DrainCapturedFrames(capture->client);
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        Check(manager.scanReissueFloors_.empty(),
              "the durable intent is consumed exactly once on transfer");
    }
    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        queued = manager.failedScanRetries_.contains(8) && !manager.failedScanRetries_.contains(7);
        if (queued) {
            manager.failedScanRetries_.at(8).due = std::chrono::steady_clock::now() - 1ms;
            ++manager.scanRetryGeneration_;
        }
    }
    Check(queued, "the durable intent moves onto exactly one slot for the new volume id");
    manager.wakeCv_.notify_one();

    const auto retried = ReadCapturedScanStartWithTimeout(capture->client, 5s);
    Check(retried && retried->volumeId == 8,
          "the retry addresses the new connection-scoped id, never the discarded one");
    bool retryMarkedReconciliation = false;
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        const auto session = manager.sessionsByEphemeralId_.find(8);
        retryMarkedReconciliation = session != manager.sessionsByEphemeralId_.end() && session->second.reconciliationScan;
    }
    Check(retryMarkedReconciliation,
          "the transferred retry is a reconciliation of the already-indexed volume");
    Check(pipeline.IsReconciliationPassActive(durableId),
          "the transferred retry begins its own reconciliation pass");
    manager.Stop();
}

// 4.1/4.4: when the replacement connection recognizes the already-indexed
// volume only after the five-second floor has elapsed, the intent still enters
// the retry queue (one throttled retry, not a tight reconnect/rescan loop) and
// the enumeration path itself issues no scan.
void TestExpiredReconciliationFloorStillRetriesThroughTheQueue() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_expired_reconciliation_floor.db")),
          "pipeline opens for the expired-floor retry test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 94;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.MarkScanComplete(volumeId), "the expired-floor fixture volume is already indexed");
    pipeline.SetLastReconciliationTime(volumeId, CurrentFileTimeForTest());

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the expired-floor fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.active_ = true;
    manager.Start();
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        manager.scanReissueFloors_[volumeId] = {std::chrono::steady_clock::now() - 1s, true};
    }

    const auto session = manager.sessionsByEphemeralId_.at(7);
    (void)session;
    // Resume drives the production StartOrResumeVolume path for the volume.
    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);

    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        Check(manager.scanReissueFloors_.empty(),
              "an already-expired floor is consumed rather than left pending");
    }
    const auto enumerationFrame = ReadCapturedMessageWithTimeout(capture->client, 200ms);
    Check(!enumerationFrame || *enumerationFrame == static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal),
          "the enumeration path issues the journal reopen, not an immediate reconciliation scan");
    DrainCapturedFrames(capture->client);

    const auto retried = ReadCapturedScanStartWithTimeout(capture->client, 2s);
    Check(retried && retried->volumeId == 7,
          "an expired floor issues one reconciliation retry promptly through the retry slot");
    Check(manager.sessionsByEphemeralId_.at(7).reconciliationScan,
          "the issued retry is the reconciliation kind carried by the floor");
    manager.Stop();
}

// 4.3/4.4: one outstanding retry per volume, repeated failure stays throttled,
// a paused volume's intent survives, and shutdown while a retry is pending
// leaves nothing that could issue a request afterwards.
void TestRetryQueueStaysBoundedThrottledAndPauseAware() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_retry_queue_bounds.db")),
          "pipeline opens for the retry-queue bounds test");
    ffindexstore::VolumeKey keyA;
    keyA.serialNumber = 95;
    ffindexstore::VolumeKey keyB;
    keyB.serialNumber = 96;
    const auto volumeA = pipeline.ResolveVolume(keyA);
    const auto volumeB = pipeline.ResolveVolume(keyB);
    Check(pipeline.MarkScanComplete(volumeA) && pipeline.MarkScanComplete(volumeB),
          "both fixture volumes are already indexed");
    pipeline.SetLastReconciliationTime(volumeA, CurrentFileTimeForTest());
    pipeline.SetLastReconciliationTime(volumeB, CurrentFileTimeForTest());

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the retry-queue bounds fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeA);
    manager.sessionsByEphemeralId_.emplace(
        8, ffengine::VolumeSessionManager::VolumeSession{volumeB, L'D', 0, false});
    manager.active_ = true;
    manager.Start();

    // Two in-flight reconciliation scans, each interrupted by connection loss.
    for (const uint32_t ephemeral : {7u, 8u}) {
        auto& session = manager.sessionsByEphemeralId_.at(ephemeral);
        session.scanInProgress = true;
        session.reconciliationScan = true;
        pipeline.BeginReconciliationPass(session.durableId);
    }
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
    Check(manager.scanReissueFloors_.size() == 2,
          "each interrupted reconciliation keeps exactly one durable floor");
    Check(!pipeline.IsReconciliationPassActive(volumeA) && !pipeline.IsReconciliationPassActive(volumeB),
          "both interrupted passes are discarded rather than finalized");

    // The replacement connection enumerates both volumes under new ids.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    DrainCapturedFrames(capture->client);
    manager.sessionsByEphemeralId_.emplace(
        9, ffengine::VolumeSessionManager::VolumeSession{volumeA, L'C', 0, false});
    manager.sessionsByEphemeralId_.emplace(
        10, ffengine::VolumeSessionManager::VolumeSession{volumeB, L'D', 0, false});
    // Global resume drives the production StartOrResumeVolume path for both.
    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        Check(manager.scanReissueFloors_.empty(), "both floors are consumed on transfer");
    }
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        Check(manager.failedScanRetries_.size() == 2, "one outstanding retry per volume");
    }

    // A paused volume's intent is not issued, but is not dropped either.
    manager.SetIndexingPaused(static_cast<uint8_t>(L'C'), true);
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        for (auto& [ephemeral, retry] : manager.failedScanRetries_) {
            (void)ephemeral;
            retry.due = std::chrono::steady_clock::now() - 1ms;
        }
        ++manager.scanRetryGeneration_;
    }
    manager.wakeCv_.notify_all();
    DrainCapturedFrames(capture->client);

    const auto issuedFromQueue = ReadCapturedScanStartWithTimeout(capture->client, 2s);
    Check(issuedFromQueue && issuedFromQueue->volumeId == 10,
          "only the unpaused volume's retry issues a scan");
    Check(manager.sessionsByEphemeralId_.at(10).scanInProgress,
          "the issued retry is in flight for the unpaused volume");
    Check(!manager.sessionsByEphemeralId_.at(9).scanInProgress,
          "no scan is issued for the paused volume");
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        const auto pausedRetry = manager.failedScanRetries_.find(9);
        Check(pausedRetry != manager.failedScanRetries_.end(),
              "the paused volume's still-valid retry intent survives");
        if (pausedRetry != manager.failedScanRetries_.end()) {
            Check(!manager.failedScanRetries_.contains(10),
                  "the issued retry leaves no second outstanding retry (one per volume)");
        }
    }

    // Repeated failure replaces the same slot at the five-second cadence.
    const auto failedAt = std::chrono::steady_clock::now();
    connection.scanFailedCallback_(ffprotocol::VolumeId{10});
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        Check(manager.failedScanRetries_.size() == 2,
              "a repeated failure keeps one slot per volume");
        const auto retry = manager.failedScanRetries_.find(10);
        Check(retry != manager.failedScanRetries_.end(), "the failed volume is re-queued");
        if (retry != manager.failedScanRetries_.end()) {
            const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
                retry->second.due - failedAt);
            Check(delay >= 4900ms && delay <= 5100ms,
                  "repeated failure stays throttled to the five-second floor");
        }
    }
    Check(manager.sessionsByEphemeralId_.at(9).scanInProgress == false &&
              manager.sessionsByEphemeralId_.at(10).scanInProgress == false,
          "the failed attempt is not left in flight");

    // Shutdown while a retry is pending clears the queue and issues nothing.
    manager.Stop();
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        Check(manager.failedScanRetries_.empty(),
              "shutdown leaves no pending retry that could issue a request afterwards");
    }
}

// 4.3: a retry whose submission fails on a still-live session re-arms only the
// single throttled slot and abandons the pass it could not start.
void TestFailedRetrySubmissionReArmsOnlyTheSingleSlot() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_failed_retry_submission.db")),
          "pipeline opens for the failed-retry submission test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 97;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.MarkScanComplete(volumeId), "the failed-submission fixture volume is already indexed");
    pipeline.SetLastReconciliationTime(volumeId, CurrentFileTimeForTest());

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.active_ = true;
    manager.Start();
    // A delivered ScanFailed is the production path that arms the bounded
    // retry for an in-flight reconciliation scan. No pipe is attached, so the
    // due retry's own submission fails on the still-live session.
    manager.sessionsByEphemeralId_.at(7).scanInProgress = true;
    manager.sessionsByEphemeralId_.at(7).reconciliationScan = true;
    connection.scanFailedCallback_(ffprotocol::VolumeId{7});
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        Check(manager.failedScanRetries_.contains(7), "the delivered failure arms the retry slot");
        manager.failedScanRetries_.at(7).due = std::chrono::steady_clock::now() - 1ms;
        ++manager.scanRetryGeneration_;
    }
    manager.wakeCv_.notify_one();

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    bool rearmed = false;
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(manager.wakeMutex_);
            const auto retry = manager.failedScanRetries_.find(7);
            rearmed = retry != manager.failedScanRetries_.end() &&
                      retry->second.due > std::chrono::steady_clock::now();
        }
        if (rearmed) break;
        // Never sleep while holding wakeMutex_: the scheduler needs it to
        // observe the re-armed slot.
        std::this_thread::sleep_for(20ms);
    }
    Check(rearmed, "a failed retry submission re-arms the same single slot");
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        Check(manager.failedScanRetries_.size() == 1, "no second retry slot is created by the failure");
    }
    Check(!manager.sessionsByEphemeralId_.at(7).scanInProgress,
          "the failed attempt does not stay in flight");
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the pass that could not start is abandoned rather than left active");
    manager.Stop();
}

// 5.1: one combined regression -- pause a scan, drop a batch, resume so a
// replacement is issued, deliver the retired attempt's stale terminal, then
// interrupt a reconciliation by disconnecting, reconnect under a new
// connection-scoped id, and recover on an authoritative attempt. Finding A and
// Finding B are not resolved unless this whole sequence holds together.
void TestCombinedPauseDisconnectReconnectStaleTerminalRecovers() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_combined_authority_and_retry.db")),
          "pipeline opens for the combined authority/retry regression");
    ffindexstore::VolumeKey key;
    key.serialNumber = 98;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);
    pipeline.SetLastReconciliationTime(volumeId, CurrentFileTimeForTest());
    Check(pipeline.ApplyMftBatch(volumeId, {MakeMftRecord(5, 5, u"", 0x10, 0),
                                            MakeMftRecord(100, 5, u"seed.txt", 0, 1)}),
          "the combined fixture seeds a durable row");
    Check(pipeline.SetJournalPosition(volumeId, 41, 900),
          "the combined fixture persists a journal identity");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the combined regression creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.active_ = true;
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true; // attempt 1: the unfinished initial scan
    DrainCapturedFrames(capture->client);

    // Phase A: the pause drops a batch, resume issues a replacement on the same
    // connection-scoped id, and the ORIGINAL attempt's completion is still in
    // the pipe when it arrives.
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{1},
                                  {MakeMftRecord(5, 5, u"", 0x10, 0),
                                   MakeMftRecord(100, 5, u"seed.txt", 0, 1)});
    manager.SetIndexingPaused(0, true);
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{2},
                                  {MakeMftRecord(200, 5, u"paused-window", 0x10, 0)});
    Check(manager.sessionsByEphemeralId_.at(7).scanBatchFailed,
          "the pause drop records the coverage hole");
    manager.SetIndexingPaused(0, false);
    DrainCapturedFrames(capture->client);
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the retired attempt's stale terminal grants no completion");
    Check(HasDurableRow(pipeline, volumeId, L"seed.txt"),
          "the stale terminal deletes no durable row");

    // Once attempt 1's terminal has retired, the service's joined replacement
    // is the only scan that can produce the next terminal on this connection.
    // Its complete coverage from the frozen cursor therefore authorizes the
    // volume and clears the repair slot.
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{2},
                                  {MakeMftRecord(200, 5, u"paused-window", 0x10, 0)});
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the replacement's own terminal authorizes the coverage resumed from the frozen cursor");
    Check(HasDurableRow(pipeline, volumeId, L"seed.txt"),
          "the replacement's authoritative completion leaves the durable row in place");
    DrainCapturedFrames(capture->client);

    // Phase B: interrupt a reconciliation of the now-complete volume.
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 42, 1000); // journal changed -> reconciliation
    Check(pipeline.IsReconciliationPassActive(volumeId),
          "the journal discontinuity starts a reconciliation pass");
    DrainCapturedFrames(capture->client);
    manager.SetIndexingPaused(0, true);
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{3},
                                  {MakeMftRecord(300, 5, u"reconciliation-window", 0x10, 0)});
    Check(manager.sessionsByEphemeralId_.at(7).scanBatchFailed,
          "the reconciliation pass records its dropped window");
    manager.SetIndexingPaused(0, false);
    DrainCapturedFrames(capture->client);

    manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the interrupted reconciliation is discarded, never finalized on a partial seen-set");
    Check(HasDurableRow(pipeline, volumeId, L"seed.txt"),
          "discarding the pass deletes no valid row");
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        const auto floor = manager.scanReissueFloors_.find(volumeId);
        Check(floor != manager.scanReissueFloors_.end() && floor->second.reconciliation,
              "the interrupted reconciliation keeps a durable five-second floor");
    }
    // Disconnect cannot restore stale authority for the lost id.
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "a terminal for the lost connection's id neither restores nor removes authority");

    // Reconnect under a new connection-scoped id and recover.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    DrainCapturedFrames(capture->client);
    manager.sessionsByEphemeralId_.emplace(
        9, ffengine::VolumeSessionManager::VolumeSession{volumeId, L'C', 0, false});
    // Global resume drives the production StartOrResumeVolume path, which is
    // what transfers the durable retry intent onto the new id.
    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);
    DrainCapturedFrames(capture->client);
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        Check(manager.failedScanRetries_.contains(9),
              "the durable intent is queued for the new connection-scoped id");
        if (manager.failedScanRetries_.contains(9)) {
            manager.failedScanRetries_.at(9).due = std::chrono::steady_clock::now() - 1ms;
            ++manager.scanRetryGeneration_;
        }
    }
    manager.wakeCv_.notify_one();
    const auto retried = ReadCapturedScanStartWithTimeout(capture->client, 2s);
    Check(retried && retried->volumeId == 9,
          "the interrupted reconciliation is retried under the new id");
    Check(pipeline.IsReconciliationPassActive(volumeId),
          "the retried reconciliation owns its own pass");

    connection.scanBatchCallback_(ffprotocol::VolumeId{9}, std::vector<uint8_t>{4},
                                  {MakeMftRecord(5, 5, u"", 0x10, 0),
                                   MakeMftRecord(100, 5, u"seed.txt", 0, 1),
                                   MakeMftRecord(300, 5, u"reconciliation-window", 0x10, 0)});
    connection.scanCompleteCallback_(ffprotocol::VolumeId{9});
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the recovered attempt finalizes its pass authoritatively");
    Check(pipeline.GetVolumeMetadata(volumeId)->lastReconciliationTime != 0,
          "the recovered pass advances the reconciliation cadence clock");
    Check(HasDurableRow(pipeline, volumeId, L"seed.txt"),
          "the recovered pass keeps every valid row");
    manager.Stop();
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
    TestPauseResumeHonorsPendingScanFailureFloor();
    TestFailedReconciliationRetrySurvivesDisconnect();
    TestDuplicateTerminalsWithoutScanInFlightAreRefused();
    TestBatchFailedScanCompletionIsNotAuthoritative();
    TestPausedCompletionIsNotAuthoritative();
    TestJournalGapDuringInFlightScanDefersUntilCompletion();
    TestStopReturnsWithoutWaitingForTheSchedulerInterval();
    TestHeartbeatServicePeerRevalidationDropsToDisconnected();
    TestPartialFrameDisconnectReconnectsAndReauthorizes();
    TestConnectionLossAbortsReconciliationPassWithoutAuthority();
    TestLegacyFailureReissueIsThrottledAfterConnectionLoss();
    TestPausedReconciliationDropIsNonAuthoritative();
    TestResumeDoesNotClearCoverageHoleForAnOlderCompletion();
    TestPausedReconciliationOlderCompletionAfterResumeDeletesNothing();
    TestRetiredTerminalCannotAuthorizeLaterReconciliation();
    TestReplacementCompletionStillMarksWithoutADroppedBatch();
    TestInterruptedReconciliationRetryReattachesToTheNewVolumeId();
    TestExpiredReconciliationFloorStillRetriesThroughTheQueue();
    TestRetryQueueStaysBoundedThrottledAndPauseAware();
    TestFailedRetrySubmissionReArmsOnlyTheSingleSlot();
    TestCombinedPauseDisconnectReconnectStaleTerminalRecovers();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
