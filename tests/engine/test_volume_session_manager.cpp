#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <future>
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

// Route the negotiated service-to-engine barrier through the production
// PrivilegedConnection parser before it reaches VolumeSessionManager. This
// keeps the scan-authority regressions bound to the exact reply frame and its
// exact-length payload, rather than testing the manager callback in isolation.
bool DispatchScanAttemptStarted(ffengine::PrivilegedConnection& connection, ffprotocol::VolumeId volumeId) {
    ffprotocol::ScanAttemptStartedPayload payload{volumeId};
    ffipc::ReceivedFrame frame{};
    frame.header.messageType = static_cast<uint16_t>(ffprotocol::MessageType::ScanAttemptStarted);
    frame.payload.resize(sizeof(payload));
    std::memcpy(frame.payload.data(), &payload, sizeof(payload));
    // The dispatcher only accepts frames from the current authenticated Active
    // transport; tests modelling a live connection set that state here. A test
    // proving the non-Active rejection calls DispatchFrameForTesting directly.
    connection.state_ = ffengine::ConnectionState::Active;
    return connection.DispatchFrameForTesting(frame);
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

// Test oracle for design D2's single-owner invariant. It mirrors
// RetryOwnerStateLocked's derivation from the observable owner containers;
// a test TU cannot call the private production method directly because
// `#define private public` changes the member's access-mangled symbol.
// Callers hold both mutex_ and wakeMutex_.
int RetryOwnerCountForTest(ffengine::VolumeSessionManager& manager,
                           ffindexstore::VolumeRowId durableId) {
    int owners = 0;
    if (manager.schedulerDispatchRetries_.contains(durableId)) ++owners;
    if (manager.scanReissueFloors_.contains(durableId)) ++owners;
    for (const auto& [ephemeralValue, session] : manager.sessionsByEphemeralId_) {
        if (session.durableId != durableId) continue;
        if (session.scanInProgress) ++owners;
        if (manager.failedScanRetries_.contains(ephemeralValue)) ++owners;
    }
    return owners;
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
        [&](ffindexstore::VolumeRowId, ffprotocol::PooledSnapshotMap directories) {
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

// retry-ownership-and-focus-escape-closure 2.1: the
// already-indexed reconciliation failure must install its next retry owner
// before OnScanFailed republishes, because the publication callback is a real
// re-entrant window in which the connection can disconnect. On the current
// source the failure clears the in-flight flag then republishes, so a
// disconnect in that window finds no owner at all and the durable retry
// intent is lost; the volume would then wait for the ordinary reconciliation
// interval.
void TestScanFailedRacingDisconnectFromPublicationKeepsRetryOwner() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_scan_failed_publication_race.db")),
          "pipeline opens for the scan-failure publication-race test");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the publication-race fixture creates a capture pipe");
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
    const auto enumeration = ffipc::ReadFrame(capture->client);
    Check(enumeration
              && enumeration->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes),
          "the publication-race connection begins with volume enumeration");
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{7}, L'C'}});
    const auto initialScan = ffipc::ReadFrame(capture->client);
    const auto initialJournal = ffipc::ReadFrame(capture->client);
    Check(initialScan
              && initialScan->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan),
          "the publication-race session starts its first scan");
    Check(initialJournal
              && initialJournal->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal),
          "the publication-race session opens its journal");
    const auto first = manager.sessionsByEphemeralId_.find(7);
    Check(first != manager.sessionsByEphemeralId_.end(), "the publication-race session is derived from the volume list");
    if (first == manager.sessionsByEphemeralId_.end()) {
        manager.Stop();
        return;
    }
    const auto volumeId = first->second.durableId;
    Check(pipeline.MarkScanComplete(volumeId), "the publication-race volume is already indexed");
    Check(pipeline.SetJournalPosition(volumeId, 41, 900), "the publication-race volume persists a journal position");
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.reconciliationScan = true;
    pipeline.BeginReconciliationPass(volumeId);

    bool publicationRan = false;
    bool ownerSurvivedDisconnect = false;
    bool disconnectedOnce = false;
    manager.SetSnapshotReadyCallback(
        [&](ffindexstore::VolumeRowId, ffprotocol::PooledSnapshotMap) {
            publicationRan = true;
            if (disconnectedOnce) return;
            disconnectedOnce = true;
            // This is the production failure window: the in-flight attempt is
            // already closed and the retry owner must already be installed.
            manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
            {
                std::lock_guard<std::mutex> lock(manager.mutex_);
                if (manager.scanReissueFloors_.contains(volumeId)) ownerSurvivedDisconnect = true;
            }
            {
                std::lock_guard<std::mutex> lock(manager.wakeMutex_);
                if (!manager.failedScanRetries_.empty()) ownerSurvivedDisconnect = true;
            }
        });

    connection.scanFailedCallback_(ffprotocol::VolumeId{7});

    Check(publicationRan, "the failure terminal republishes before arming its retry");
    Check(ownerSurvivedDisconnect,
          "an already-indexed reconciliation failure owns its retry before the publication window");

    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    const auto reenumeration = ffipc::ReadFrame(capture->client);
    Check(reenumeration
              && reenumeration->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes),
          "the replacement publication-race connection enumerates volumes");
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{8}, L'C'}});
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        const auto retry = manager.failedScanRetries_.find(8);
        Check(retry != manager.failedScanRetries_.end() && retry->second.reconciliation
                  && !manager.failedScanRetries_.contains(7),
              "the raced retry intent reattaches to the replacement connection as a reconciliation");
    }
    manager.Stop();
}

// retry-ownership-and-focus-escape-closure 2.2: a due
// retry that has left its queue but is not yet admitted must still own the
// durable volume. On the current source the scheduler extracts the retry into
// a local vector and releases wakeMutex_, so a disconnect in that window
// observes neither an in-flight scan nor a queued entry and drops the intent.
void TestSchedulerDispatchRacingDisconnectKeepsRetryOwner() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_scheduler_dispatch_race.db")),
          "pipeline opens for the scheduler dispatch-race test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 6402;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.MarkScanComplete(volumeId), "the scheduler dispatch-race volume is already indexed");
    Check(pipeline.SetJournalPosition(volumeId, 41, 900),
          "the scheduler dispatch-race volume persists a journal position");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the scheduler dispatch-race fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    connection.state_ = ffengine::ConnectionState::Active;
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 2;
    manager.active_ = true;
    manager.Start();

    std::promise<void> seamReached;
    auto seamFuture = seamReached.get_future();
    bool ownerSurvivedDisconnect = false;
    manager.scanRetryDispatchSeam_ = [&]() {
        manager.scanRetryDispatchSeam_ = nullptr;
        // Exactly the reviewed window: the retry has left failedScanRetries_
        // and no admission has happened yet.
        manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
        {
            std::lock_guard<std::mutex> lock(manager.mutex_);
            if (manager.scanReissueFloors_.contains(volumeId)) ownerSurvivedDisconnect = true;
        }
        {
            std::lock_guard<std::mutex> lock(manager.wakeMutex_);
            if (!manager.failedScanRetries_.empty()) ownerSurvivedDisconnect = true;
        }
        seamReached.set_value();
    };
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        manager.failedScanRetries_[7] = {std::chrono::steady_clock::now() - 1ms, true};
        ++manager.scanRetryGeneration_;
    }
    manager.wakeCv_.notify_one();

    Check(seamFuture.wait_for(5s) == std::future_status::ready,
          "the scheduler reaches its in-hand dispatch window");
    Check(ownerSurvivedDisconnect,
          "a due retry that left its queue still owns the durable volume across the disconnect");

    // Let the scheduler finish its post-seam admission attempt: the durable
    // owner must remain, never be clobbered by the dead session.
    const auto settleDeadline = std::chrono::steady_clock::now() + 2s;
    bool durableOwnerRemains = false;
    while (std::chrono::steady_clock::now() < settleDeadline) {
        {
            std::lock_guard<std::mutex> lock(manager.mutex_);
            durableOwnerRemains = manager.scanReissueFloors_.contains(volumeId);
        }
        if (durableOwnerRemains) break;
        std::this_thread::sleep_for(10ms);
    }
    Check(durableOwnerRemains,
          "the scheduler does not clobber the durable floor after the racing disconnect");
    manager.Stop();
}

// retry-ownership-and-focus-escape-closure D2: disabling a volume erases its connection-scoped session, so the owned
// retry intent must move to the durable floor instead of disappearing with
// the id. Re-enabling the volume consumes the floor exactly once.
void TestDisableTransfersOwnedRetryToDurableFloor() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_disable_retry_transfer.db")),
          "pipeline opens for the disable retry-transfer test");
    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the disable transfer fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    ffprotocol::VolumeSetting enabled;
    enabled.key = L"C:";
    enabled.enabled = true;
    manager.ReloadConfiguration({enabled});
    connection.state_ = ffengine::ConnectionState::Active;
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 2;
    manager.Start();
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    const auto enumeration = ffipc::ReadFrame(capture->client);
    Check(enumeration
              && enumeration->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes),
          "the disable transfer connection begins with volume enumeration");
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{7}, L'C'}});
    const auto first = manager.sessionsByEphemeralId_.find(7);
    Check(first != manager.sessionsByEphemeralId_.end(),
          "the disable transfer session is derived from the volume list");
    if (first == manager.sessionsByEphemeralId_.end()) {
        manager.Stop();
        return;
    }
    const auto volumeId = first->second.durableId;
    DrainCapturedFrames(capture->client);
    Check(pipeline.MarkScanComplete(volumeId), "the disable transfer fixture is already indexed");
    Check(pipeline.SetJournalPosition(volumeId, 41, 900), "the disable transfer fixture has a journal position");
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});

    manager.sessionsByEphemeralId_.at(7).scanInProgress = true;
    manager.sessionsByEphemeralId_.at(7).reconciliationScan = true;
    connection.scanFailedCallback_(ffprotocol::VolumeId{7});
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        Check(RetryOwnerCountForTest(manager, volumeId) == 1
                  && manager.failedScanRetries_.contains(7),
              "the delivered failure installs exactly one connection-scoped queued owner");
    }

    ffprotocol::VolumeSetting disabled;
    disabled.key = L"C:";
    disabled.enabled = false;
    manager.ReloadConfiguration({disabled});
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        Check(manager.sessionsByEphemeralId_.empty(), "disabling the volume tears its session down");
        Check(RetryOwnerCountForTest(manager, volumeId) == 1
                  && manager.scanReissueFloors_.contains(volumeId),
              "disabling the volume transfers the retry intent to exactly the durable floor");
        Check(!manager.failedScanRetries_.contains(7),
              "no dead ephemeral id keeps a retry after disable");
    }

    manager.ReloadConfiguration({enabled});
    DrainCapturedFrames(capture->client);
    connection.volumeListCallback_({ffprotocol::VolumeInfo{ffprotocol::VolumeId{9}, L'C'}});
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        const auto retry = manager.failedScanRetries_.find(9);
        Check(RetryOwnerCountForTest(manager, volumeId) == 1
                  && retry != manager.failedScanRetries_.end() && retry->second.reconciliation
                  && !manager.failedScanRetries_.contains(7),
              "re-enabling the volume consumes the floor onto exactly one reconciliation retry");
    }
    manager.Stop();
}

// retry-ownership-and-focus-escape-closure D2: a volume disappearing from the enumeration erases its session, so its
// owned retry intent must transfer to the durable floor before the id goes.
void TestDisappearanceTransfersOwnedRetryToDurableFloor() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_disappearance_retry_transfer.db")),
          "pipeline opens for the disappearance retry-transfer test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 6404;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.MarkScanComplete(volumeId), "the disappearance transfer fixture is already indexed");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.active_ = true;
    manager.Start();
    manager.sessionsByEphemeralId_.at(7).scanInProgress = true;
    manager.sessionsByEphemeralId_.at(7).reconciliationScan = true;
    connection.scanFailedCallback_(ffprotocol::VolumeId{7});

    connection.volumeListCallback_({});
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        Check(manager.sessionsByEphemeralId_.empty(), "a disappeared volume's session is erased");
        Check(RetryOwnerCountForTest(manager, volumeId) == 1
                  && manager.scanReissueFloors_.contains(volumeId),
              "disappearance keeps the retry intent on exactly the durable floor, not the dead id");
        Check(!manager.failedScanRetries_.contains(7), "the disappeared volume leaves no stranded retry");
    }
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
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the stale-terminal fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
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
uint64_t CurrentFileTimeForTest();

void TestDeferredReconciliationSurvivesFinalizationFailure() {
    using namespace std::chrono_literals;
    for (const bool failTimestamp : {false, true}) {
        ffengine::IndexPipeline pipeline;
        const auto path = FreshDbPath(failTimestamp ? "engine_finalize_timestamp.db" : "engine_finalize_deferred.db");
        Check(pipeline.Open(path), "metadata-finalization fixture opens");
        ffindexstore::VolumeKey key;
        key.serialNumber = failTimestamp ? 1764 : 1763;
        const auto volume = pipeline.ResolveVolume(key);
        Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(5, 5, u"", 0x10, 0)}), "metadata-finalization root commits");
        Check(pipeline.SetScanCursor(volume, {1, 2, 3}), "metadata-finalization has a resumable initial cursor");
        const auto previousTime = failTimestamp ? CurrentFileTimeForTest() : 0;
        if (failTimestamp) {
            Check(pipeline.MarkScanComplete(volume), "timestamp fixture has previous initial authority");
            Check(pipeline.SetLastReconciliationTime(volume, previousTime), "timestamp fixture has prior cadence value");
            pipeline.BeginReconciliationPass(volume);
            Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(5, 5, u"", 0x10, 0)}), "timestamp fixture sees authoritative root");
        }
        ffengine::PrivilegedConnection connection;
        ffengine::VolumeSessionManager manager(pipeline, connection);
        auto capture = CreatePipeCapture();
        Check(capture != nullptr, "metadata-finalization capture pipe opens");
        if (!capture) return;
        AttachCaptureServer(connection, *capture);
        AddSession(manager, volume);
        auto& session = manager.sessionsByEphemeralId_.at(7);
        session.scanInProgress = true;
        session.reconciliationScan = failTimestamp;
        session.deferredReconciliation = !failTimestamp;
        connection.state_ = ffengine::ConnectionState::Active;
        connection.negotiatedMajor_ = 2;
        connection.negotiatedMinor_ = 2;
        manager.active_ = true;
        manager.Start();
        sqlite3* raw = nullptr;
        Check(sqlite3_open(path.c_str(), &raw) == SQLITE_OK, "metadata-only fault connection opens");
        const auto sql = failTimestamp ?
            "CREATE TRIGGER fail_finalize BEFORE UPDATE OF last_reconciliation_time ON volumes BEGIN SELECT RAISE(ABORT,'test finalization'); END;" :
            "CREATE TRIGGER fail_finalize BEFORE UPDATE OF scan_complete ON volumes BEGIN SELECT RAISE(ABORT,'test finalization'); END;";
        Check(sqlite3_exec(raw, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "trigger targets actual metadata finalization write");
        connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
        {
            std::lock_guard<std::mutex> lock(manager.wakeMutex_);
            const auto retry = manager.failedScanRetries_.find(7);
            Check(retry != manager.failedScanRetries_.end() && retry->second.reconciliation,
                  "finalization failure preserves strongest owed full reconciliation intent");
            if (retry != manager.failedScanRetries_.end()) {
                retry->second.due = std::chrono::steady_clock::now() - 1ms;
                ++manager.scanRetryGeneration_;
            }
        }
        Check(pipeline.GetVolumeMetadata(volume)->lastReconciliationTime == previousTime,
              "metadata failure does not advertise successful reconciliation time");
        Check(sqlite3_exec(raw, "DROP TRIGGER fail_finalize;", nullptr, nullptr, nullptr) == SQLITE_OK, "transient metadata failure is removed");
        sqlite3_close(raw);
        manager.wakeCv_.notify_one();
        const auto issued = ReadCapturedScanStartWithTimeout(capture->client, 2s);
        Check(issued && issued->cursor.empty(), "real bounded retry scheduler issues cursor-free full rescan");
        manager.Stop();
    }
}

void TestReconciliationRemovalFailureRetainsRetryAuthority() {
    ffengine::IndexPipeline pipeline;
    const auto path = FreshDbPath("engine_reconciliation_removal_retry.db");
    Check(pipeline.Open(path), "manager removal-failure fixture opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 763;
    const auto volume = pipeline.ResolveVolume(key);
    Check(pipeline.MarkScanComplete(volume), "manager removal fixture has prior scan authority");
    std::vector<ffprotocol::MftRecordV1> rows{MakeMftRecord(5, 5, u"", 0x10, 0)};
    for (uint64_t id = 1000; id < 13000; ++id) rows.push_back(MakeMftRecord(id, 5, u"stale.txt", 0, 1));
    Check(pipeline.ApplyMftBatch(volume, rows), "manager removal fixture seeds three chunks");
    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volume);
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.reconciliationScan = true;
    pipeline.BeginReconciliationPass(volume);
    Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(5, 5, u"", 0x10, 0)}), "authoritative rescan sees root only");
    ffindexstore::SetTestingCandidateMaintenanceFault(5501, false);
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    ffindexstore::SetTestingCandidateMaintenanceFault(0, false);
    Check(pipeline.IsReconciliationPassActive(volume), "manager retains failed removal obligation");
    Check(pipeline.GetVolumeMetadata(volume)->lastReconciliationTime == 0,
          "failed removal must not advance successful-reconciliation timestamp");
    Check(RetryOwnerCountForTest(manager, volume) == 1,
          "failed removal has one bounded scheduled reconciliation retry owner");
    session.scanInProgress = true;
    session.reconciliationScan = true;
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.IsReconciliationPassActive(volume) &&
          pipeline.GetVolumeMetadata(volume)->lastReconciliationTime != 0,
          "successful removal retry alone advances reconciliation timestamp");
    Check(RetryOwnerCountForTest(manager, volume) == 0, "successful retry clears its repair owner");
}

void TestBatchFailedScanCompletionIsNotAuthoritative() {
    ffengine::IndexPipeline pipeline;
    const std::string dbPath = FreshDbPath("engine_test_batch_failed_completion.db");
    Check(pipeline.Open(dbPath), "pipeline opens for batch-failure test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 63;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the batch-failure fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    connection.state_ = ffengine::ConnectionState::Active;
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 2;
    manager.active_ = true;
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

    // A pause/resume cycle re-issues the unfinished scan, but D2 requires the
    // non-authoritative completion's single retry owner to be honored first:
    // the slot survives at its five-second cadence and issues through the real
    // retry path. The clean scan that follows marks completion normally.
    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);
    Check(!session.scanInProgress, "pause/resume does not bypass the owned retry floor");
    Check(!session.scanBatchFailed, "the deferred restart begins from a clean authority slate");
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        const auto retry = manager.failedScanRetries_.find(7);
        Check(retry != manager.failedScanRetries_.end(),
              "the non-authoritative completion keeps exactly one retry slot");
        if (retry != manager.failedScanRetries_.end()) {
            manager.failedScanRetries_.at(7).due = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
            ++manager.scanRetryGeneration_;
        }
    }
    manager.wakeCv_.notify_one();
    DrainCapturedFrames(capture->client);
    const auto restart = ReadCapturedScanStartWithTimeout(capture->client, std::chrono::milliseconds(2000));
    Check(restart && restart->volumeId == 7,
          "the owned retry restarts the unfinished scan at its due time");
    Check(session.scanInProgress && !session.scanBatchFailed,
          "the (re)started scan begins in flight with a clean authority slate");
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
// id, and the ORIGINAL attempt's completion is delivered before the service's
// ordered replacement barrier. The engine must reject that terminal, retain
// the frozen cursor, and accept the replacement only after the negotiated
// marker separates the two workers' output.
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
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 3;
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
    Check(session.awaitingScanAttemptStarted,
          "the coverage-hole replacement remains provisional until the service barrier arrives");
    DrainCapturedFrames(capture->client);

    // Attempt 1's completion was already written to the pipe before the
    // service joined it and emitted the replacement barrier.
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the older completion never marks the volume complete");
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the older completion finalizes no reconciliation pass");
    Check(session.scanInProgress && session.awaitingScanAttemptStarted,
          "the pre-barrier terminal leaves the replacement provisional and in flight");
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "the exact ScanAttemptStarted frame is accepted by the production dispatcher");
    Check(!session.awaitingScanAttemptStarted && !session.scanBatchFailed,
          "the barrier releases only the replacement's frozen coverage authority");

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

// scan-generation-integrity-and-residual-certification-closure 10.2: a
// superseded service worker observes its stop flag before writing a terminal.
// The replacement therefore owns the only terminal that reaches the engine.
// That terminal must complete the replacement after it applies the missing
// cursor window; it cannot remain reserved forever for a terminal that the
// service deliberately did not send.
void TestReplacementOnlyCompletionAfterSupersedeIsAuthoritative() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_replacement_only_terminal.db")),
          "pipeline opens for the replacement-only-terminal test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 9901;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);
    pipeline.SetLastReconciliationTime(volumeId, CurrentFileTimeForTest());

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the replacement-only-terminal fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 3;
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.active_ = true;
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true; // attempt 1: an incomplete initial scan

    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{1, 2, 3},
                                  {MakeMftRecord(5, 5, u"", 0x10, 0),
                                   MakeMftRecord(100, 5, u"seed.txt", 0, 1)});
    DrainCapturedFrames(capture->client);

    manager.SetIndexingPaused(0, true);
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{4, 5, 6},
                                  {MakeMftRecord(200, 5, u"missed-window", 0x10, 0)});
    Check(session.scanBatchFailed,
          "the paused batch records the initial attempt's missing coverage");
    manager.SetIndexingPaused(0, false);
    const auto replacement = ReadCapturedScanStartWithTimeout(capture->client, 2s);
    Check(replacement && replacement->volumeId == 7,
          "resume starts a replacement under the same connection-scoped volume id");
    Check(replacement && replacement->cursor == std::vector<uint8_t>({1, 2, 3}),
          "the replacement starts at the last durably applied cursor");
    Check(session.awaitingScanAttemptStarted,
          "a replacement that covers a paused window waits for the ordered service barrier");
    DrainCapturedFrames(capture->client);

    // The production service's StopAndJoin path can suppress attempt 1's
    // terminal. Its ordering marker is therefore the sole proof that the
    // subsequent work and only completion belong to the replacement.
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "the service barrier releases the terminal-free replacement");
    Check(!session.awaitingScanAttemptStarted,
          "the terminal-free replacement becomes authoritative after the barrier");
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{4, 5, 6},
                                  {MakeMftRecord(200, 5, u"missed-window", 0x10, 0)});
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});

    Check(pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the replacement's only terminal completes the recovered initial scan");
    Check(!session.scanInProgress,
          "the replacement-only terminal releases the in-flight scan ownership");
    Check(!session.awaitingScanAttemptStarted,
          "no provisional barrier state remains after the replacement-only completion");
    manager.Stop();
}

// scan-generation-integrity-and-residual-certification-closure 3.4: a staged
// peer cannot serialize the ScanAttemptStarted barrier. A coverage-hole
// replacement therefore discards the old connection-scoped VolumeId instead
// of letting a pre-{2,3} terminal choose the new attempt's authority.
void TestLegacyPeerCoverageHoleResetsConnectionAuthority() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_legacy_peer_scan_authority_reset.db")),
          "pipeline opens for the legacy-peer authority-reset test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 9902;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.Start();
    manager.active_ = true;
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 2;
    bool resetObserved = false;
    connection.onStateChange_ = [&manager, &resetObserved](ffengine::ConnectionState state,
                                                            ffengine::UnavailableReason reason) {
        if (state == ffengine::ConnectionState::Disconnected && reason == ffengine::UnavailableReason::None) {
            resetObserved = true;
        }
        manager.OnConnectionStateChanged(state);
    };

    auto& stored = manager.sessionsByEphemeralId_.at(7);
    stored.scanInProgress = true;
    stored.scanBatchFailed = true;
    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);

    Check(resetObserved, "a pre-{2,3} coverage-hole replacement resets before issuing an ambiguous request");
    Check(manager.sessionsByEphemeralId_.empty(),
          "the reset drops every connection-scoped scan authority before old terminals can arrive");
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        Check(manager.scanReissueFloors_.contains(volumeId),
              "the reset retains one durable reissue floor for the interrupted scan");
    }
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        Check(manager.failedScanRetries_.empty(),
              "the discarded connection keeps no ephemeral retry slot behind");
    }
    manager.Stop();
}

// retry-ownership-and-focus-escape-closure 3.3/4.4: a pre-{2,3} peer cannot
// order a replacement that supersedes an attempt still in flight, even when no
// batch was dropped. The retired worker can still emit its only terminal, so
// the engine resets the connection instead of accepting the ambiguous
// terminal, and keeps the interrupted work as one durable floor.
void TestLegacyPeerInFlightSupersedeResetsConnectionAuthority() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_legacy_peer_inflight_reset.db")),
          "pipeline opens for the legacy in-flight supersede test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 9911;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.Start();
    manager.active_ = true;
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 2;
    bool resetObserved = false;
    connection.onStateChange_ = [&manager, &resetObserved](ffengine::ConnectionState state,
                                                            ffengine::UnavailableReason reason) {
        if (state == ffengine::ConnectionState::Disconnected && reason == ffengine::UnavailableReason::None) {
            resetObserved = true;
        }
        manager.OnConnectionStateChanged(state);
    };

    auto& stored = manager.sessionsByEphemeralId_.at(7);
    stored.scanInProgress = true;
    stored.scanBatchFailed = false; // no coverage hole: the supersede itself is ambiguous
    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);

    Check(resetObserved, "a pre-{2,3} in-flight supersede resets before issuing an ambiguous request");
    Check(manager.sessionsByEphemeralId_.empty(),
          "the reset drops the ambiguous attempt before an old terminal can arrive");
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        Check(manager.scanReissueFloors_.contains(volumeId),
              "the reset keeps the interrupted attempt as one durable reissue floor");
    }
    manager.Stop();
}

// scan-generation-integrity-and-residual-certification-closure 3.4: a false
// result from the bounded start write is not proof that the service ignored
// the request. The engine must discard its provisional state and move the
// retry to durable identity rather than leaving a scan permanently in flight.
void TestFailedScanStartResetsProvisionalAuthority() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_failed_scan_start_authority_reset.db")),
          "pipeline opens for the failed-start authority-reset test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 9903;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    manager.ReloadConfiguration({setting});
    manager.Start();
    manager.active_ = true;
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 3;
    bool resetObserved = false;
    connection.onStateChange_ = [&manager, &resetObserved](ffengine::ConnectionState state,
                                                            ffengine::UnavailableReason reason) {
        if (state == ffengine::ConnectionState::Disconnected && reason == ffengine::UnavailableReason::None) {
            resetObserved = true;
        }
        manager.OnConnectionStateChanged(state);
    };

    auto& stored = manager.sessionsByEphemeralId_.at(7);
    stored.scanInProgress = true;
    stored.scanBatchFailed = true;
    // No active pipe is installed. SendRequest therefore returns false after
    // the manager has made the {2,3} replacement provisional.
    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);

    Check(resetObserved, "a failed scan-start write resets the connection generation");
    Check(manager.sessionsByEphemeralId_.empty(),
          "a failed scan-start write leaves no provisional scan ownership behind");
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        Check(manager.scanReissueFloors_.contains(volumeId),
              "the failed start moves recovery to a durable reissue floor");
    }
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        Check(manager.failedScanRetries_.empty(),
              "the failed start leaves no stale ephemeral retry slot behind");
    }
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
    Check(!pipeline.IsReconciliationPassActive(volumeId) || !session.awaitingScanAttemptStarted,
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

    // Recovery is not poisoned: the owed reconciliation issues through its
    // single owned slot at the bounded due time (no caller may preempt the
    // five-second clock), runs to an authoritative completion, and only then
    // finalizes.
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 99, 1234); // journal identity changed
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the journal discontinuity defers to the already-owned reconciliation retry");
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        const auto retry = manager.failedScanRetries_.find(7);
        Check(retry != manager.failedScanRetries_.end() && retry->second.reconciliation,
              "the owed reconciliation keeps exactly one queued owner");
        if (retry != manager.failedScanRetries_.end()) {
            retry->second.due = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
            ++manager.scanRetryGeneration_;
        }
    }
    manager.wakeCv_.notify_one();
    const auto ownedSlot = ReadCapturedScanStartWithTimeout(capture->client, 2s);
    Check(ownedSlot && ownedSlot->volumeId == 7,
          "the owned slot starts its reconciliation at the bounded due time");
    Check(pipeline.IsReconciliationPassActive(volumeId),
          "the issued reconciliation owns a fresh pass");
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
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 3;
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
    Check(session.awaitingScanAttemptStarted,
          "the replacement retains no authority until the service orders its barrier");
    DrainCapturedFrames(capture->client);

    // This terminal belongs to attempt 1. The replacement remains provisional
    // until the service's ordered marker proves that attempt 1 is joined.
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(session.awaitingScanAttemptStarted && session.scanInProgress,
          "the original terminal cannot release or complete the provisional replacement");
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "the ordered marker accepts the replacement after the old terminal");
    Check(!session.awaitingScanAttemptStarted && session.scanInProgress,
          "the barrier preserves replacement ownership while granting it authority");

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
    Check(!session.awaitingScanAttemptStarted && session.scanInProgress,
          "the replacement remains the active attempt after the ordered barrier");
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
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 3;
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    connection.scanBatchCallback_(ffprotocol::VolumeId{7}, std::vector<uint8_t>{1},
                                  {MakeMftRecord(5, 5, u"", 0x10, 0)});
    DrainCapturedFrames(capture->client);

    manager.SetIndexingPaused(0, true); // no batch arrives while paused: no hole
    manager.SetIndexingPaused(0, false);
    Check(session.scanInProgress, "resume re-issues the unfinished scan");
    Check(session.awaitingScanAttemptStarted,
          "a replacement that supersedes an in-flight attempt waits for its ordered barrier");
    DrainCapturedFrames(capture->client);
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "the replacement's ordered barrier is accepted");
    Check(!session.awaitingScanAttemptStarted && session.scanInProgress,
          "the barrier releases the replacement while keeping it in flight");

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
    // future-dated. The floor transfer already woke the scheduler during the
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
    // Drain frames already on the wire before forcing the retries due: the
    // scheduler may issue the unpaused volume's scan as soon as it is
    // notified, and a later drain would consume that frame.
    DrainCapturedFrames(capture->client);
    {
        std::lock_guard<std::mutex> lock(manager.wakeMutex_);
        for (auto& [ephemeral, retry] : manager.failedScanRetries_) {
            (void)ephemeral;
            retry.due = std::chrono::steady_clock::now() - 1ms;
        }
        ++manager.scanRetryGeneration_;
    }
    manager.wakeCv_.notify_all();

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
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 3;
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
    Check(session.awaitingScanAttemptStarted,
          "the combined regression holds the replacement behind its service barrier");
    connection.scanCompleteCallback_(ffprotocol::VolumeId{7});
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "the retired attempt's stale terminal grants no completion");
    Check(HasDurableRow(pipeline, volumeId, L"seed.txt"),
          "the stale terminal deletes no durable row");

    // The service's joined replacement becomes authoritative only after its
    // marker. Its complete coverage from the frozen cursor then authorizes
    // the volume and clears the repair slot.
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "the combined regression receives the ordered replacement barrier");
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

    // The replacement attempt fails while its connection is still current;
    // the publication callback is the production window in which a
    // disconnect can re-enter. The single-owner transition must install the
    // retry before publishing, and the disconnect must transfer it to the
    // durable floor instead of dropping it.
    bool disconnectedDuringPublication = false;
    manager.SetSnapshotReadyCallback(
        [&](ffindexstore::VolumeRowId, ffprotocol::PooledSnapshotMap) {
            if (disconnectedDuringPublication) return;
            disconnectedDuringPublication = true;
            manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
        });
    const auto failureAt = std::chrono::steady_clock::now();
    connection.scanFailedCallback_(ffprotocol::VolumeId{7});
    Check(disconnectedDuringPublication,
          "the reconciliation failure owns its retry before the publication window");
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the failed reconciliation is discarded, never finalized on a partial seen-set");
    Check(HasDurableRow(pipeline, volumeId, L"seed.txt"),
          "discarding the failed pass deletes no valid row");
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        const auto floor = manager.scanReissueFloors_.find(volumeId);
        Check(floor != manager.scanReissueFloors_.end() && floor->second.reconciliation,
              "the failed reconciliation keeps exactly one durable reconciliation floor");
        if (floor != manager.scanReissueFloors_.end()) {
            const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(floor->second.due - failureAt);
            Check(delay >= 0ms && delay <= 5100ms,
                  "the transferred reconciliation floor preserves the five-second throttle");
        }
    }
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        Check(RetryOwnerCountForTest(manager, volumeId) == 1,
              "the failed reconciliation leaves exactly one retry owner after the disconnect");
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
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        Check(RetryOwnerCountForTest(manager, volumeId) == 0,
              "the authoritative completion consumes the retry owner exactly once");
        Check(manager.failedScanRetries_.empty() && manager.scanReissueFloors_.empty()
                  && manager.schedulerDispatchRetries_.empty(),
              "no queued, dispatched, or floored retry survives the authoritative completion");
    }
    Check(!manager.sessionsByEphemeralId_.contains(7),
          "no stale connection-scoped id remains after recovery");
    manager.Stop();
}

} // namespace

// retry-ownership-and-focus-escape-closure 4.2/4.4/4.5:
// the protocol-2.3 barrier stays reply-only and exact. The production parser
// rejects truncated and extended payloads as protocol violations, an
// unsolicited/duplicate/unknown-id barrier moves no authority, and a barrier
// from a retired connection generation cannot release the current attempt.
void TestScanAttemptStartedFrameIsExactAndAuthorityScoped() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_scan_attempt_started_frames.db")),
          "pipeline opens for the barrier-frame validation test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 6405;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);

    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "an exact-size ScanAttemptStarted frame is accepted by the production parser");
    Check(!session.scanInProgress && !session.awaitingScanAttemptStarted && !session.scanBatchFailed,
          "an unsolicited barrier for an idle session grants no authority");
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{99}),
          "a well-formed barrier for an unknown volume id is still parsed");
    Check(!session.scanInProgress && !session.awaitingScanAttemptStarted && !session.scanBatchFailed,
          "a barrier for an unknown volume id changes no scan authority");

    ffipc::ReceivedFrame shortFrame{};
    shortFrame.header.messageType = static_cast<uint16_t>(ffprotocol::MessageType::ScanAttemptStarted);
    shortFrame.payload.resize(sizeof(ffprotocol::ScanAttemptStartedPayload) - 1);
    Check(!connection.DispatchFrameForTesting(shortFrame),
          "a truncated barrier payload is a fail-closed protocol violation");
    ffipc::ReceivedFrame extendedFrame{};
    extendedFrame.header.messageType = static_cast<uint16_t>(ffprotocol::MessageType::ScanAttemptStarted);
    extendedFrame.payload.resize(sizeof(ffprotocol::ScanAttemptStartedPayload) + 1);
    Check(!connection.DispatchFrameForTesting(extendedFrame),
          "an extended barrier payload (trailing bytes) is rejected, never treated as a start signal");
    Check(!session.scanInProgress && !session.awaitingScanAttemptStarted,
          "malformed barrier frames never grant authority");

    // A provisional coverage-hole replacement accepts its barrier exactly once.
    session.scanInProgress = true;
    session.awaitingScanAttemptStarted = true;
    session.scanBatchFailed = true;
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "the replacement's ordered barrier is accepted");
    Check(!session.awaitingScanAttemptStarted && !session.scanBatchFailed && session.scanInProgress,
          "the accepted barrier releases the replacement and clears its provisional hole flag");
    session.scanBatchFailed = true;
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "a duplicate barrier is still a valid frame");
    Check(session.scanBatchFailed && session.scanInProgress,
          "a duplicate barrier neither clears a new coverage hole nor revokes the attempt");
    manager.Stop();
}

// retry-ownership-and-focus-escape-closure 4.4/4.5:
// disconnect around the barrier must leave the retry intent owned, produce no
// false completion, and let no later barrier from the lost generation
// authorize the replacement connection's attempt.
void TestDisconnectAroundBarrierKeepsRetryOwnership() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_disconnect_around_barrier.db")),
          "pipeline opens for the disconnect-around-barrier test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 6406;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 3;
    manager.active_ = true;
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.awaitingScanAttemptStarted = true;
    session.scanBatchFailed = true;
    pipeline.BeginReconciliationPass(volumeId);

    // Disconnect before the barrier: the provisional attempt travels to the
    // durable floor, and no partial pass is finalized.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
    Check(manager.sessionsByEphemeralId_.empty(), "disconnect before the barrier drops the connection-scoped ids");
    {
        std::lock_guard<std::mutex> lock(manager.mutex_);
        Check(manager.scanReissueFloors_.contains(volumeId),
              "disconnect before the barrier keeps the provisional attempt as a durable floor");
    }
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "a disconnect before the barrier never marks the volume complete");
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "a disconnect before the barrier discards the partial pass");

    // A barrier from the retired generation is well-formed but finds no
    // current attempt: it must not clear a hole, complete, or revoke anything.
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "a retired-generation barrier frame still parses");
    Check(manager.sessionsByEphemeralId_.empty(), "a retired-generation barrier creates no session");
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "a retired-generation barrier clears no coverage hole and starts no pass");

    // A replacement connection's provisional attempt is scoped to its own
    // connection-scoped id; the stale barrier for id 7 cannot release it.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    manager.sessionsByEphemeralId_.emplace(
        8, ffengine::VolumeSessionManager::VolumeSession{volumeId, L'C', 0, false});
    auto& replacement = manager.sessionsByEphemeralId_.at(8);
    replacement.scanInProgress = true;
    replacement.awaitingScanAttemptStarted = true;
    replacement.scanBatchFailed = true;
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "a stale-id barrier is still parsed");
    Check(replacement.awaitingScanAttemptStarted && replacement.scanBatchFailed,
          "a barrier for the retired id cannot release the current attempt");
    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{8}),
          "the current attempt's own barrier is accepted");
    Check(!replacement.awaitingScanAttemptStarted && !replacement.scanBatchFailed,
          "the current attempt's barrier releases exactly that attempt");
    manager.Stop();
}
// retry-ownership-and-focus-escape-closure 4.4/4.5: only the current
// authenticated Active transport may move scan authority. A barrier delivered
// while the connection is disconnected or mid-handshake (unauthenticated,
// stale, or replaced generation) is rejected before any callback, and it
// cannot release the provisional replacement.
void TestBarrierFromNonActiveTransportIsRejected() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_barrier_non_active_transport.db")),
          "pipeline opens for the non-Active barrier test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 6407;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.awaitingScanAttemptStarted = true;
    session.scanBatchFailed = true;

    ffprotocol::ScanAttemptStartedPayload payload{ffprotocol::VolumeId{7}};
    ffipc::ReceivedFrame frame{};
    frame.header.messageType = static_cast<uint16_t>(ffprotocol::MessageType::ScanAttemptStarted);
    frame.payload.resize(sizeof(payload));
    std::memcpy(frame.payload.data(), &payload, sizeof(payload));

    connection.state_ = ffengine::ConnectionState::Disconnected;
    Check(!connection.DispatchFrameForTesting(frame),
          "a barrier from a disconnected transport is rejected fail-closed");
    connection.state_ = ffengine::ConnectionState::Handshaking;
    Check(!connection.DispatchFrameForTesting(frame),
          "a barrier from an unauthenticated handshake transport is rejected");
    connection.state_ = ffengine::ConnectionState::Connecting;
    Check(!connection.DispatchFrameForTesting(frame),
          "a barrier from a replaced/connecting transport is rejected");
    Check(session.awaitingScanAttemptStarted && session.scanBatchFailed && session.scanInProgress,
          "no non-Active transport can release or revoke the provisional replacement");

    connection.state_ = ffengine::ConnectionState::Active;
    Check(connection.DispatchFrameForTesting(frame), "the current Active transport's barrier is accepted");
    Check(!session.awaitingScanAttemptStarted && !session.scanBatchFailed && session.scanInProgress,
          "the Active barrier releases exactly the current attempt");
    manager.Stop();
}

// retry-ownership-and-focus-escape-closure 4.4: disconnect immediately after
// an accepted barrier must keep the replacement's owed work owned, produce no
// false completion, and reattach to a fresh id on reconnect.
void TestDisconnectImmediatelyAfterAcceptedBarrier() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_disconnect_after_barrier.db")),
          "pipeline opens for the disconnect-after-barrier test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 6408;
    const auto volumeId = pipeline.ResolveVolume(key);
    pipeline.SetVolumeAvailable(volumeId, true, 1);
    Check(pipeline.SetJournalPosition(volumeId, 41, 900), "the fixture persists a journal identity");
    pipeline.SetLastReconciliationTime(volumeId, CurrentFileTimeForTest());

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the disconnect-after-barrier fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    connection.negotiatedMajor_ = 2;
    connection.negotiatedMinor_ = 3;
    manager.active_ = true;
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.reconciliationScan = true;
    session.awaitingScanAttemptStarted = true;
    session.scanBatchFailed = true;
    pipeline.BeginReconciliationPass(volumeId);

    Check(DispatchScanAttemptStarted(connection, ffprotocol::VolumeId{7}),
          "the replacement's barrier is accepted before the disconnect");
    Check(!session.awaitingScanAttemptStarted && !session.scanBatchFailed,
          "the barrier authorizes the replacement before the disconnect");

    const auto disconnectedAt = std::chrono::steady_clock::now();
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
    Check(manager.sessionsByEphemeralId_.empty(), "disconnect immediately after the barrier clears the session ids");
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        const auto floor = manager.scanReissueFloors_.find(volumeId);
        Check(floor != manager.scanReissueFloors_.end() && floor->second.reconciliation,
              "the accepted replacement's owed work stays on the durable floor as a reconciliation");
        if (floor != manager.scanReissueFloors_.end()) {
            const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(floor->second.due - disconnectedAt);
            Check(delay >= 0ms && delay <= 5100ms,
                  "the floor keeps the scheduled replacement's five-second cadence");
        }
        Check(RetryOwnerCountForTest(manager, volumeId) == 1,
              "exactly one owner remains after the barrier-following disconnect");
    }
    Check(!pipeline.GetVolumeMetadata(volumeId)->scanComplete,
          "a disconnect immediately after the barrier is not a false completion");
    Check(!pipeline.IsReconciliationPassActive(volumeId),
          "the partial reconciliation pass is discarded, not finalized");
    // Reconnect under a fresh id: the floor reattaches with its kind intact.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    DrainCapturedFrames(capture->client);
    manager.sessionsByEphemeralId_.emplace(
        9, ffengine::VolumeSessionManager::VolumeSession{volumeId, L'C', 0, false});
    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        const auto retry = manager.failedScanRetries_.find(9);
        Check(retry != manager.failedScanRetries_.end() && retry->second.reconciliation
                  && !manager.failedScanRetries_.contains(7),
              "the owed reconciliation attaches to exactly the fresh id");
        Check(manager.scanReissueFloors_.empty(), "the floor is consumed exactly once on reconnection");
    }
    manager.Stop();
}

// retry-ownership-and-focus-escape-closure 3.3: an interrupted initial scan
// keeps its owed kind across an authentication failure and a later
// authenticated session, and the durable floor attaches to the fresh id.
void TestReauthenticationFailurePreservesFloorKindForFreshId() {
    using namespace std::chrono_literals;
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_reauth_floor_kind.db")),
          "pipeline opens for the reauthentication floor-kind test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 6409;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    auto capture = CreatePipeCapture();
    Check(capture != nullptr, "the reauthentication fixture creates a capture pipe");
    if (!capture) return;
    AttachCaptureServer(connection, *capture);
    AddSession(manager, volumeId);
    manager.active_ = true;
    manager.Start();
    auto& session = manager.sessionsByEphemeralId_.at(7);
    session.scanInProgress = true;
    session.reconciliationScan = false; // an unfinished initial scan

    // The transport drops, a reauthentication attempt fails, and the failure
    // path lands on a further non-Active state: the floor must survive both.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Connecting);
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Handshaking);
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        const auto floor = manager.scanReissueFloors_.find(volumeId);
        Check(floor != manager.scanReissueFloors_.end() && !floor->second.reconciliation,
              "the interrupted initial scan keeps its owed kind across reauthentication failure");
        Check(RetryOwnerCountForTest(manager, volumeId) == 1,
              "the failed reauthentication leaves exactly one retry owner");
    }

    // A later authenticated session attaches the intent to a fresh id without
    // upgrading an initial scan into a reconciliation.
    manager.OnConnectionStateChanged(ffengine::ConnectionState::Active);
    DrainCapturedFrames(capture->client);
    manager.sessionsByEphemeralId_.emplace(
        9, ffengine::VolumeSessionManager::VolumeSession{volumeId, L'D', 0, false});
    manager.SetIndexingPaused(0, true);
    manager.SetIndexingPaused(0, false);
    {
        std::scoped_lock lock(manager.mutex_, manager.wakeMutex_);
        const auto retry = manager.failedScanRetries_.find(9);
        Check(retry != manager.failedScanRetries_.end() && !retry->second.reconciliation
                  && !manager.failedScanRetries_.contains(7),
              "the initial-scan intent attaches to the fresh id without changing kind");
        Check(manager.scanReissueFloors_.empty(), "the reauthenticated session consumes the floor once");
    }
    manager.Stop();
}

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
    TestScanFailedRacingDisconnectFromPublicationKeepsRetryOwner();
    TestSchedulerDispatchRacingDisconnectKeepsRetryOwner();
    TestDisableTransfersOwnedRetryToDurableFloor();
    TestDisappearanceTransfersOwnedRetryToDurableFloor();
    TestDuplicateTerminalsWithoutScanInFlightAreRefused();
    TestDeferredReconciliationSurvivesFinalizationFailure();
    TestReconciliationRemovalFailureRetainsRetryAuthority();
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
    TestReplacementOnlyCompletionAfterSupersedeIsAuthoritative();
    TestLegacyPeerCoverageHoleResetsConnectionAuthority();
    TestLegacyPeerInFlightSupersedeResetsConnectionAuthority();
    TestFailedScanStartResetsProvisionalAuthority();
    TestPausedReconciliationOlderCompletionAfterResumeDeletesNothing();
    TestRetiredTerminalCannotAuthorizeLaterReconciliation();
    TestReplacementCompletionStillMarksWithoutADroppedBatch();
    TestInterruptedReconciliationRetryReattachesToTheNewVolumeId();
    TestExpiredReconciliationFloorStillRetriesThroughTheQueue();
    TestRetryQueueStaysBoundedThrottledAndPauseAware();
    TestFailedRetrySubmissionReArmsOnlyTheSingleSlot();
    TestCombinedPauseDisconnectReconnectStaleTerminalRecovers();
    TestScanAttemptStartedFrameIsExactAndAuthorityScoped();
    TestDisconnectAroundBarrierKeepsRetryOwnership();
    TestBarrierFromNonActiveTransportIsRejected();
    TestDisconnectImmediatelyAfterAcceptedBarrier();
    TestReauthenticationFailurePreservesFloorKindForFreshId();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
