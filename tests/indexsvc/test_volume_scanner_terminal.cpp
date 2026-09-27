// index-durability-false-success-closure 2.3 (PA-001): the service must
// never report ScanComplete for a scan that could not run to completion.
// Drives the real RunVolumeScan against an unmounted drive letter over an
// in-process pipe pair: both reachable failure paths (backup-privilege
// enable denial in an unprivileged test process, and if that somehow
// succeeds, the raw-volume open denial for a letter with no volume behind
// it) must produce the ScanFailed terminal for a {2,2} peer, and must keep
// today's legacy ScanComplete byte-for-byte for a pre-{2,2} peer.
#include <cstdlib>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <windows.h>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Commands.h"
#include "ffprotocol/Version.h"
#include "../TestSupport.h"

#include "VolumeScanner.h"
#include "ScanWorkerBoundary.h"
#include "ServiceWorkerLifecycle.h"
#include "WriteDeadline.h"

using namespace fftest;

namespace {

struct PipePair {
    HANDLE server = INVALID_HANDLE_VALUE; // service writes
    HANDLE client = INVALID_HANDLE_VALUE; // test reads
    ~PipePair() {
        if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
        if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
    }
};

std::unique_ptr<PipePair> CreatePipePair(bool duplex = false, bool overlappedServer = true) {
    static std::atomic<uint32_t> pipeInstance{0};
    const std::wstring name = L"\\\\.\\pipe\\FastFilesScanTerminalTest-" +
                              std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64()) + L"-" +
                              std::to_wstring(pipeInstance.fetch_add(1, std::memory_order_relaxed));
    auto pair = std::make_unique<PipePair>();
    // FILE_FLAG_OVERLAPPED because the service write path is
    // WriteFrameOverlapped (WriteDeadline.h) -- same handle shape as the
    // production ctrl connection.
    const DWORD serverFlags = overlappedServer ? FILE_FLAG_OVERLAPPED : 0;
    pair->server = CreateNamedPipeW(name.c_str(),
                                    (duplex ? PIPE_ACCESS_DUPLEX : PIPE_ACCESS_OUTBOUND) | serverFlags,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                    1, 65536, 65536, 0, nullptr);
    if (pair->server == INVALID_HANDLE_VALUE) return nullptr;
    const DWORD clientAccess = duplex ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ;
    pair->client = CreateFileW(name.c_str(), clientAccess, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pair->client == INVALID_HANDLE_VALUE) return nullptr;
    if (ConnectNamedPipe(pair->server, nullptr) == FALSE && GetLastError() != ERROR_PIPE_CONNECTED) {
        return nullptr;
    }
    return pair;
}

class PendingServiceRead {
public:
    ~PendingServiceRead() {
        if (pending_ && pipe_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe_, &overlapped_);
            WaitForSingleObject(overlapped_.hEvent, 1000);
        }
        if (overlapped_.hEvent != nullptr) CloseHandle(overlapped_.hEvent);
    }

    bool Start(HANDLE pipe) {
        pipe_ = pipe;
        buffer_.resize(sizeof(ffprotocol::FrameHeader));
        overlapped_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped_.hEvent == nullptr) return false;
        DWORD bytesRead = 0;
        const BOOL started = ReadFile(pipe_, buffer_.data(), static_cast<DWORD>(buffer_.size()),
                                      &bytesRead, &overlapped_);
        pending_ = started == FALSE && GetLastError() == ERROR_IO_PENDING;
        return pending_;
    }

    bool WasCancelled() {
        if (!pending_ || WaitForSingleObject(overlapped_.hEvent, 2000) != WAIT_OBJECT_0) return false;
        DWORD bytesRead = 0;
        const BOOL completed = GetOverlappedResult(pipe_, &overlapped_, &bytesRead, FALSE);
        const DWORD error = completed == FALSE ? GetLastError() : ERROR_SUCCESS;
        pending_ = false;
        return completed == FALSE && error == ERROR_OPERATION_ABORTED;
    }

private:
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped_{};
    std::vector<uint8_t> buffer_;
    bool pending_ = false;
};

template <typename Work, typename OnException>
void RunScanBoundaryOnBoundedWorker(HANDLE pipe, std::atomic<bool>& transportFailed,
                                    Work&& work, OnException&& onException) {
    HANDLE workerDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(workerDone != nullptr, "scan boundary fixture creates its worker completion event");
    if (workerDone == nullptr) return;

    std::thread worker([&] {
        ffindexsvc::RunScanWorkerBoundary(pipe, transportFailed, std::forward<Work>(work),
                                           [](ffindexsvc::ScanRunResult) {}, std::forward<OnException>(onException));
        SetEvent(workerDone);
    });
    DWORD wait = WaitForSingleObject(workerDone, 2000);
    Check(wait == WAIT_OBJECT_0, "scan worker boundary returns within its two-second fixture deadline");
    if (wait != WAIT_OBJECT_0) {
        CancelIoEx(pipe, nullptr);
        wait = WaitForSingleObject(workerDone, 5000);
        if (wait != WAIT_OBJECT_0) {
            std::fprintf(stderr, "scan worker fixture did not stop after pipe-I/O cancellation\n");
            std::_Exit(EXIT_FAILURE);
        }
    }
    worker.join();
    CloseHandle(workerDone);
}

// legacy-protocol-correctness-and-raw-volume-cancellation-closure 8.6
// (reviewer finding D-5): the setup-failure cases must fail on a letter with
// no volume behind it even when the process holds the backup privilege (a
// mounted drive letter would otherwise enumerate for real). Scan for a free
// letter instead of assuming one.
wchar_t ResolveUnmountedDriveLetter() {
    for (wchar_t letter = L'R'; letter <= L'Z'; ++letter) {
        const wchar_t root[] = {letter, L':', L'\\', L'\0'};
        if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR) {
            return letter;
        }
    }
    return L'\0';
}

void RunDeliveredFailureTerminalCase(ffprotocol::ProtocolVersion negotiated, uint16_t expectedTerminal,
                                     const char* description) {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "scan-terminal case creates an in-process pipe pair");
    if (!pair) return;
    const wchar_t unmounted = ResolveUnmountedDriveLetter();
    Check(unmounted != L'\0', "the test host has a drive letter with no volume behind it");
    if (unmounted == L'\0') return;

    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> transportFailed{false};
    constexpr ffprotocol::VolumeId kVolume{7};
    // The scan never enumerates (no volume behind the letter); whichever setup
    // stage fails first (privilege enable, volume open, or geometry query), the
    // single terminal frame is what distinguishes truth from a false success.
    const ffindexsvc::ScanRunResult result =
        ffindexsvc::RunVolumeScan(pair->server, writeMutex, kVolume, unmounted, {}, false, negotiated, shouldStop,
                                  &transportFailed);
    Check(result == ffindexsvc::ScanRunResult::FailedTerminalDelivered,
          "a setup failure reports a delivered failure terminal");
    Check(!transportFailed.load(),
          "a failure terminal a capable peer can parse keeps the connection alive");

    const auto frame = ffipc::ReadFrame(pair->client);
    Check(frame.has_value(), "the failed scan sends exactly one terminal frame");
    if (!frame) return;
    Check(frame->header.messageType == expectedTerminal, description);
    Check(frame->payload.size() == sizeof(ffprotocol::VolumeId),
          "the terminal frame carries the fixed VolumeId payload");
    if (frame->payload.size() == sizeof(ffprotocol::VolumeId)) {
        ffprotocol::VolumeId echoed{};
        std::memcpy(&echoed, frame->payload.data(), sizeof(echoed));
        Check(echoed == kVolume, "the terminal frame is addressed to the scanned volume");
    }
    // Exactly one frame: nothing else may ride the connection after a
    // terminal, and setup-failed scans emit no batches.
    DWORD available = 0;
    const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &available, nullptr) != FALSE;
    Check(!peeked || available == 0, "no further frames follow the terminal");
}

// legacy-protocol-correctness-and-raw-volume-cancellation-closure 3.3: a peer
// that cannot parse ScanFailed must not receive the old fallback ScanComplete
// for a scan that did not complete. The honest outcome is no terminal at all
// plus a connection abort, which the worker boundary reports as poison so the
// control loop ends this connection generation.
void RunUnreportableFailureCase(ffprotocol::ProtocolVersion negotiated, const char* level) {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "unreportable-failure case creates an in-process pipe pair");
    if (!pair) return;
    const wchar_t unmounted = ResolveUnmountedDriveLetter();
    Check(unmounted != L'\0', "the test host has a drive letter with no volume behind it");
    if (unmounted == L'\0') return;

    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> transportFailed{false};
    constexpr ffprotocol::VolumeId kVolume{7};
    ffindexsvc::ScanRunResult result = ffindexsvc::ScanRunResult::Completed;
    ffindexsvc::RunScanWorkerBoundary(
        pair->server, transportFailed,
        [&]() -> ffindexsvc::ScanRunResult {
            result = ffindexsvc::RunVolumeScan(pair->server, writeMutex, kVolume, unmounted, {}, false, negotiated,
                                               shouldStop, &transportFailed);
            return result;
        },
        [](ffindexsvc::ScanRunResult) {},
        [](ffindexsvc::ScanWorkerExceptionKind) {});

    char description[192];
    std::snprintf(description, sizeof(description),
                  "%s setup failure reports the unreportable-failure outcome", level);
    Check(result == ffindexsvc::ScanRunResult::FailureAbortedConnection, description);
    std::snprintf(description, sizeof(description),
                  "%s setup failure requests the connection abort at the worker boundary", level);
    Check(transportFailed.load(), description);

    DWORD available = 0;
    const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &available, nullptr) != FALSE;
    std::snprintf(description, sizeof(description),
                  "%s setup failure writes no terminal frame at all", level);
    Check(peeked && available == 0, description);
}

void RunCancelledCase() {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "cancelled scan case creates an in-process pipe pair");
    if (!pair) return;

    std::mutex writeMutex;
    std::atomic<bool> shouldStop{true};
    const ffindexsvc::ScanRunResult result = ffindexsvc::RunVolumeScan(
        pair->server, writeMutex, ffprotocol::VolumeId{7}, L'Q', {}, false,
        ffprotocol::ProtocolVersion{2, 2}, shouldStop);
    Check(result == ffindexsvc::ScanRunResult::Cancelled,
          "a pre-start cancellation is silent and does not open a volume");
    DWORD available = 0;
    const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &available, nullptr) != FALSE;
    Check(!peeked || available == 0, "a cancelled scan emits no terminal frame");
}

void RunTerminalTransportFailureCase() {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "transport-failure case creates an in-process pipe pair");
    if (!pair) return;

    // Closing the reader makes the failure occur on the terminal write. The
    // scanner must return TransportFailed rather than claiming a delivered
    // ScanFailed terminal or attempting a second frame.
    CloseHandle(pair->client);
    pair->client = INVALID_HANDLE_VALUE;
    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    const ffindexsvc::ScanRunResult result = ffindexsvc::RunVolumeScan(
        pair->server, writeMutex, ffprotocol::VolumeId{7}, L'Q', {}, false,
        ffprotocol::ProtocolVersion{2, 2}, shouldStop);
    Check(result == ffindexsvc::ScanRunResult::TransportFailed,
          "a failed terminal write is reported as transport-failed");
}

std::vector<ffprotocol::MftRecordV2> MakeSyntheticRecords() {
    ffprotocol::MftRecordV2 record{};
    record.fixed.fileReferenceNumber = 0x1000000000007ull;
    record.fixed.parentFileReferenceNumber = 0x1000000000001ull;
    record.fixed.sizeBytes = 23;
    record.fixed.allocatedSizeBytes = 4096;
    record.fixed.fileAttributes = FILE_ATTRIBUTE_ARCHIVE;
    record.fileName = u"fixture.txt";
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(record.fileName.size());
    return {record};
}

std::vector<uint8_t> MakeExpectedSyntheticBatchFrame(
    const std::vector<ffprotocol::MftRecordV2>& records, ffprotocol::VolumeId volumeId) {
    ffprotocol::ScanRecordBatchV2Header batchHeader{};
    batchHeader.volumeId = volumeId;
    batchHeader.recordCount = static_cast<uint32_t>(records.size());
    batchHeader.resumeCursorLengthBytes = sizeof(uint64_t);
    batchHeader.recordFormatVersion = 2;
    const uint64_t cursor = 0;
    const std::vector<uint8_t> serializedRecords = ffprotocol::SerializeMftBatchV2(records);
    std::vector<uint8_t> payload(sizeof(batchHeader) + sizeof(cursor) + serializedRecords.size());
    size_t offset = 0;
    std::memcpy(payload.data() + offset, &batchHeader, sizeof(batchHeader));
    offset += sizeof(batchHeader);
    std::memcpy(payload.data() + offset, &cursor, sizeof(cursor));
    offset += sizeof(cursor);
    std::memcpy(payload.data() + offset, serializedRecords.data(), serializedRecords.size());

    ffprotocol::FrameHeader frameHeader{};
    frameHeader.totalLength = static_cast<uint32_t>(sizeof(frameHeader) + payload.size());
    frameHeader.structVersion = ffprotocol::kCurrentStructVersion;
    frameHeader.messageType = static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatchV2);
    std::vector<uint8_t> frame(sizeof(frameHeader) + payload.size());
    std::memcpy(frame.data(), &frameHeader, sizeof(frameHeader));
    std::memcpy(frame.data() + sizeof(frameHeader), payload.data(), payload.size());
    return frame;
}

// legacy-protocol-correctness-and-raw-volume-cancellation-closure 3.3: the
// mid-enumeration failure path shares ScanOutput::Finish, so a pre-{2,2} peer
// must see the delivered batches and then no terminal at all -- never the old
// ScanComplete fallback.
void RunUnreportableSyntheticMidEnumerationCase(ffprotocol::ProtocolVersion negotiated, const char* level) {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "unreportable mid-enumeration case creates an in-process pipe pair");
    if (!pair) return;

    const auto records = MakeSyntheticRecords();
    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> transportFailed{false};
    ffindexsvc::ScanRunResult result = ffindexsvc::ScanRunResult::Completed;
    ffindexsvc::RunScanWorkerBoundary(
        pair->server, transportFailed,
        [&]() -> ffindexsvc::ScanRunResult {
            result = ffindexsvc::RunSyntheticVolumeScanForTesting(
                pair->server, writeMutex, ffprotocol::VolumeId{7}, negotiated,
                [&records](std::vector<ffprotocol::MftRecordV2>& out) {
                    out = records;
                    return true;
                },
                shouldStop, /*enumerationFailed=*/true, {}, &transportFailed);
            return result;
        },
        [](ffindexsvc::ScanRunResult) {},
        [](ffindexsvc::ScanWorkerExceptionKind) {});

    const uint16_t expectedBatch = ffprotocol::ProtocolSupportsAllocatedSize(negotiated)
        ? static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatchV2)
        : static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch);
    char description[192];
    std::snprintf(description, sizeof(description),
                  "%s mid-enumeration failure reports the unreportable-failure outcome", level);
    Check(result == ffindexsvc::ScanRunResult::FailureAbortedConnection, description);
    std::snprintf(description, sizeof(description),
                  "%s mid-enumeration failure requests the connection abort at the worker boundary", level);
    Check(transportFailed.load(), description);

    const auto batch = ffipc::ReadFrame(pair->client);
    std::snprintf(description, sizeof(description),
                  "%s mid-enumeration failure still delivers the real scan batch", level);
    Check(batch && batch->header.messageType == expectedBatch, description);
    DWORD available = 0;
    const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &available, nullptr) != FALSE;
    std::snprintf(description, sizeof(description),
                  "%s mid-enumeration failure appends no terminal after the delivered batch", level);
    Check(peeked && available == 0, description);
}

// legacy-protocol-correctness-and-raw-volume-cancellation-closure 3.2: the
// successful-scan wire bytes a legacy peer receives must remain unchanged by
// the failure-path correction. This compares the terminal frame's complete
// header + payload bytes against the canonical ScanComplete encoding.
void RunLegacySuccessTerminalBytesCase(ffprotocol::ProtocolVersion negotiated, const char* level) {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "legacy success byte case creates an in-process pipe pair");
    if (!pair) return;

    constexpr ffprotocol::VolumeId kVolume{7};
    const auto records = MakeSyntheticRecords();
    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    const ffindexsvc::ScanRunResult result = ffindexsvc::RunSyntheticVolumeScanForTesting(
        pair->server, writeMutex, kVolume, negotiated,
        [&records](std::vector<ffprotocol::MftRecordV2>& out) {
            out = records;
            return true;
        },
        shouldStop, /*enumerationFailed=*/false);
    char description[192];
    std::snprintf(description, sizeof(description),
                  "%s genuine success still completes after the failure-path correction", level);
    Check(result == ffindexsvc::ScanRunResult::Completed, description);

    const auto batch = ffipc::ReadFrame(pair->client);
    std::snprintf(description, sizeof(description),
                  "%s genuine success still emits its negotiated batch layout", level);
    const uint16_t expectedBatch = ffprotocol::ProtocolSupportsAllocatedSize(negotiated)
        ? static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatchV2)
        : static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch);
    Check(batch && batch->header.messageType == expectedBatch, description);

    const auto terminal = ffipc::ReadFrame(pair->client);
    std::snprintf(description, sizeof(description),
                  "%s genuine success still emits a ScanComplete terminal", level);
    Check(terminal && terminal->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::ScanComplete),
          description);
    if (!terminal) return;

    ffprotocol::FrameHeader expectedHeader{};
    expectedHeader.totalLength = static_cast<uint32_t>(sizeof(expectedHeader) + sizeof(ffprotocol::VolumeId));
    expectedHeader.structVersion = ffprotocol::kCurrentStructVersion;
    expectedHeader.messageType = static_cast<uint16_t>(ffprotocol::MessageType::ScanComplete);
    std::vector<uint8_t> expected(sizeof(expectedHeader) + sizeof(ffprotocol::VolumeId));
    std::memcpy(expected.data(), &expectedHeader, sizeof(expectedHeader));
    std::memcpy(expected.data() + sizeof(expectedHeader), &kVolume, sizeof(kVolume));

    std::vector<uint8_t> actual(sizeof(terminal->header) + terminal->payload.size());
    std::memcpy(actual.data(), &terminal->header, sizeof(terminal->header));
    if (!terminal->payload.empty()) {
        std::memcpy(actual.data() + sizeof(terminal->header), terminal->payload.data(), terminal->payload.size());
    }
    std::snprintf(description, sizeof(description),
                  "%s successful-scan terminal stays byte-identical to the canonical encoding", level);
    Check(actual == expected, description);
}

void TestSyntheticBatchWriteFailureAbortsBeforeTerminal() {
    auto pair = CreatePipePair(true);
    Check(pair != nullptr, "synthetic batch failure creates a duplex in-process pipe pair");
    if (!pair) return;

    PendingServiceRead pendingRead;
    const bool readStarted = pendingRead.Start(pair->server);
    Check(readStarted, "batch failure fixture starts a pending service-pipe read");
    if (!readStarted) return;

    constexpr ffprotocol::VolumeId kVolume{17};
    constexpr uint32_t kPrefixSize = 12;
    const auto records = MakeSyntheticRecords();
    const auto expected = MakeExpectedSyntheticBatchFrame(records, kVolume);
    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> transportFailed{false};
    auto result = ffindexsvc::ScanRunResult::Completed;
    RunScanBoundaryOnBoundedWorker(pair->server, transportFailed, [&] {
        ffipc::testing::FailNextOverlappedFrameWriteAfterBytes(kPrefixSize);
        result = ffindexsvc::RunSyntheticVolumeScanForTesting(
            pair->server, writeMutex, kVolume, ffprotocol::ProtocolVersion{2, 2},
            [&records](std::vector<ffprotocol::MftRecordV2>& out) {
                out = records;
                return true;
            }, shouldStop);
        return result;
    }, [](ffindexsvc::ScanWorkerExceptionKind) {});
    Check(result == ffindexsvc::ScanRunResult::TransportFailed,
          "synthetic ScanRecordBatch write failure is transport-fatal");
    Check(transportFailed.load(), "scan-worker boundary marks failed batch output transport-fatal");
    Check(pendingRead.WasCancelled(), "batch failure cancels the connection read through the worker boundary");

    std::vector<uint8_t> actual(expected.size());
    DWORD peekedBytes = 0;
    DWORD availableBytes = 0;
    const bool peeked = PeekNamedPipe(pair->client, actual.data(), static_cast<DWORD>(actual.size()),
                                      &peekedBytes, &availableBytes, nullptr) != FALSE;
    Check(peeked && peekedBytes == kPrefixSize && availableBytes == kPrefixSize,
          "failed batch leaves only the selected real prefix on the pipe");
    Check(peekedBytes == 0 || std::memcmp(actual.data(), expected.data(), peekedBytes) == 0,
          "failed batch prefix matches the real production V2 batch serialization");
}

void TestSyntheticTerminalWriteFailureIsNotRetried() {
    auto pair = CreatePipePair(true);
    Check(pair != nullptr, "synthetic terminal failure creates a duplex in-process pipe pair");
    if (!pair) return;

    PendingServiceRead pendingRead;
    const bool readStarted = pendingRead.Start(pair->server);
    Check(readStarted, "terminal failure fixture starts a pending service-pipe read");
    if (!readStarted) return;

    constexpr ffprotocol::VolumeId kVolume{18};
    const auto records = MakeSyntheticRecords();
    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> transportFailed{false};
    auto result = ffindexsvc::ScanRunResult::Completed;
    RunScanBoundaryOnBoundedWorker(pair->server, transportFailed, [&] {
        result = ffindexsvc::RunSyntheticVolumeScanForTesting(
            pair->server, writeMutex, kVolume, ffprotocol::ProtocolVersion{2, 2},
            [&records](std::vector<ffprotocol::MftRecordV2>& out) {
                out = records;
                return true;
            }, shouldStop, false,
            [] { ffipc::testing::FailNextOverlappedFrameWriteAfterBytes(0); });
        return result;
    }, [](ffindexsvc::ScanWorkerExceptionKind) {});
    Check(result == ffindexsvc::ScanRunResult::TransportFailed,
          "synthetic terminal write failure is transport-fatal");
    Check(transportFailed.load(), "scan-worker boundary marks terminal failure transport-fatal");
    Check(pendingRead.WasCancelled(), "terminal failure cancels the connection read through the worker boundary");

    const auto batch = ffipc::ReadFrame(pair->client);
    Check(batch && batch->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatchV2),
          "the completed scan batch precedes the failed terminal attempt");
    DWORD availableBytes = 0;
    const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &availableBytes, nullptr) != FALSE;
    Check(peeked && availableBytes == 0,
          "failed terminal is not retried and no later terminal is appended");
}

void TestVolumeHandleClosesWhenPostOpenWorkThrows() {
    HANDLE fakeVolume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(fakeVolume != nullptr, "volume-handle exception fixture creates a portable kernel handle");
    if (fakeVolume == nullptr) return;

    bool exceptionObserved = false;
    try {
        ffindexsvc::ThrowAfterVolumeHandleAcquiredForTesting(fakeVolume);
    } catch (const std::bad_alloc&) {
        exceptionObserved = true;
    }
    Check(exceptionObserved, "the production-shaped volume scan seam throws after handle acquisition");

    DWORD flags = 0;
    SetLastError(ERROR_SUCCESS);
    const BOOL handleStillValid = GetHandleInformation(fakeVolume, &flags);
    Check(handleStillValid == FALSE && GetLastError() == ERROR_INVALID_HANDLE,
          "the scoped volume guard closes the acquired handle during exception unwinding");
}

void TestSyntheticScanCancellationDuringActiveSource() {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "active cancellation creates an in-process pipe pair");
    if (!pair) return;

    constexpr ffprotocol::VolumeId kVolume{19};
    const auto records = MakeSyntheticRecords();
    HANDLE sourceEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(sourceEntered != nullptr, "synthetic active source creates its coordination event");
    if (sourceEntered == nullptr) return;

    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> finished{false};
    ffindexsvc::ScanRunResult result = ffindexsvc::ScanRunResult::Completed;
    std::thread worker([&] {
        result = ffindexsvc::RunSyntheticVolumeScanForTesting(
            pair->server, writeMutex, kVolume, ffprotocol::ProtocolVersion{2, 2},
            [&records, &shouldStop, sourceEntered](std::vector<ffprotocol::MftRecordV2>& out) {
                SetEvent(sourceEntered);
                while (!shouldStop.load(std::memory_order_acquire)) Sleep(1);
                out = records;
                return true;
            }, shouldStop);
        finished.store(true, std::memory_order_release);
    });
    const DWORD entered = WaitForSingleObject(sourceEntered, 2000);
    Check(entered == WAIT_OBJECT_0, "synthetic scan source entered active work before cancellation");
    shouldStop.store(true, std::memory_order_release);
    worker.join();
    CloseHandle(sourceEntered);
    Check(finished.load(std::memory_order_acquire), "active synthetic scan worker exits after cancellation");
    Check(result == ffindexsvc::ScanRunResult::Cancelled,
          "cancellation during active source work suppresses both batch and terminal writes");
    DWORD availableBytes = 0;
    const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &availableBytes, nullptr) != FALSE;
    Check(peeked && availableBytes == 0, "active cancellation emits no completion or failure frame");
}

void TestSyntheticWorkerExceptionUsesProductionBoundary() {
    auto pair = CreatePipePair(true);
    Check(pair != nullptr, "worker exception case creates a duplex in-process pipe pair");
    if (!pair) return;

    PendingServiceRead pendingRead;
    const bool readStarted = pendingRead.Start(pair->server);
    Check(readStarted, "worker exception case starts a pending service-pipe read");
    if (!readStarted) return;

    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> transportFailed{false};
    std::atomic<bool> standardExceptionObserved{false};
    RunScanBoundaryOnBoundedWorker(pair->server, transportFailed, [&] {
        return ffindexsvc::RunSyntheticVolumeScanForTesting(
                pair->server, writeMutex, ffprotocol::VolumeId{20}, ffprotocol::ProtocolVersion{2, 2},
                [](std::vector<ffprotocol::MftRecordV2>&) -> bool {
                    throw std::runtime_error("controlled scan source exception");
                }, shouldStop);
    }, [&standardExceptionObserved](ffindexsvc::ScanWorkerExceptionKind kind) {
        standardExceptionObserved.store(kind == ffindexsvc::ScanWorkerExceptionKind::Standard,
                                          std::memory_order_release);
    });
    Check(transportFailed.load(std::memory_order_acquire),
          "worker exception marks the connection transport-fatal");
    Check(pendingRead.WasCancelled(),
          "worker failure cancels the service connection's pending inbound pipe read");
    Check(standardExceptionObserved.load(std::memory_order_acquire),
          "controlled source exception is identified as a standard worker exception");
    DWORD availableBytes = 0;
    const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &availableBytes, nullptr) != FALSE;
    Check(peeked && availableBytes == 0,
          "exception path emits neither a batch nor a false scan terminal");
}

void TestStopAndJoinCancelsSynchronousWorkerIo() {
    auto pair = CreatePipePair(true, false);
    Check(pair != nullptr, "synchronous worker-owner fixture creates a synchronous duplex pipe");
    if (!pair) return;

    HANDLE readEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE workerFinished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE ownerFinished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(readEntered != nullptr && workerFinished != nullptr && ownerFinished != nullptr,
          "synchronous worker-owner fixture creates coordination events");
    if (readEntered == nullptr || workerFinished == nullptr || ownerFinished == nullptr) {
        if (readEntered != nullptr) CloseHandle(readEntered);
        if (workerFinished != nullptr) CloseHandle(workerFinished);
        if (ownerFinished != nullptr) CloseHandle(ownerFinished);
        return;
    }

    auto stopFlag = std::make_shared<std::atomic<bool>>(false);
    std::atomic<DWORD> readError{ERROR_SUCCESS};
    std::atomic<bool> readSucceeded{true};
    std::vector<uint8_t> input(1);
    std::vector<ffindexsvc::detail::ActiveWorker> workers;
    ffindexsvc::detail::ActiveWorker worker;
    worker.volumeId = 41;
    worker.stopFlag = stopFlag;
    worker.thread = std::thread([&] {
        SetEvent(readEntered);
        DWORD bytesRead = 0;
        const BOOL readResult = ReadFile(pair->server, input.data(), static_cast<DWORD>(input.size()),
                                         &bytesRead, nullptr);
        const DWORD error = readResult != FALSE ? ERROR_SUCCESS : GetLastError();
        readSucceeded.store(readResult != FALSE, std::memory_order_release);
        readError.store(error, std::memory_order_release);
        SetEvent(workerFinished);
    });
    Check(WaitForSingleObject(readEntered, 2000) == WAIT_OBJECT_0,
          "synchronous worker reaches its blocking pipe read");

    const DWORD workerThreadId = GetThreadId(worker.thread.native_handle());
    HANDLE queryThread = workerThreadId == 0 ? nullptr
        : OpenThread(THREAD_QUERY_INFORMATION, FALSE, workerThreadId);
    Check(queryThread != nullptr, "fixture can query the owned worker's pending-I/O state");
    bool ioPending = false;
    const auto pendingDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (queryThread != nullptr && std::chrono::steady_clock::now() < pendingDeadline) {
        BOOL pending = FALSE;
        if (GetThreadIOPendingFlag(queryThread, &pending) != FALSE && pending != FALSE) {
            ioPending = true;
            break;
        }
        if (WaitForSingleObject(workerFinished, 0) == WAIT_OBJECT_0) break;
        Sleep(1);
    }
    if (queryThread != nullptr) CloseHandle(queryThread);
    Check(ioPending, "the owner stops only after a real synchronous read is pending");
    if (!ioPending) {
        CloseHandle(pair->client);
        pair->client = INVALID_HANDLE_VALUE;
        if (WaitForSingleObject(workerFinished, 2000) != WAIT_OBJECT_0) {
            std::fprintf(stderr, "synchronous worker fixture could not release its read after peer close\n");
            std::_Exit(EXIT_FAILURE);
        }
        worker.thread.join();
        CloseHandle(readEntered);
        CloseHandle(workerFinished);
        CloseHandle(ownerFinished);
        return;
    }

    workers.push_back(std::move(worker));
    const auto stopStarted = std::chrono::steady_clock::now();
    std::thread owner([&] {
        ffindexsvc::detail::StopAndJoinAll(workers);
        SetEvent(ownerFinished);
    });
    DWORD ownerWait = WaitForSingleObject(ownerFinished, 2000);
    Check(ownerWait == WAIT_OBJECT_0,
          "production worker-owner stop and join completes within the synchronous-pipe deadline");
    if (ownerWait != WAIT_OBJECT_0) {
        CloseHandle(pair->client);
        pair->client = INVALID_HANDLE_VALUE;
        ownerWait = WaitForSingleObject(ownerFinished, 2000);
        if (ownerWait != WAIT_OBJECT_0) {
            std::fprintf(stderr, "synchronous worker-owner fixture could not release its blocked worker\n");
            std::_Exit(EXIT_FAILURE);
        }
    }
    owner.join();
    const auto stopElapsed = std::chrono::steady_clock::now() - stopStarted;
    Check(stopElapsed < std::chrono::seconds(2),
          "CancelSynchronousIo releases the deterministic blocked owner before the deadline");
    Check(stopFlag->load(std::memory_order_acquire), "owner publishes the worker stop flag before joining");
    Check(workers.empty(), "production StopAndJoinAll drains the worker owner");
    Check(WaitForSingleObject(workerFinished, 0) == WAIT_OBJECT_0,
          "the owned synchronous-I/O worker has returned before teardown completes");
    Check(!readSucceeded.load(std::memory_order_acquire) &&
              readError.load(std::memory_order_acquire) == ERROR_OPERATION_ABORTED,
          "CancelSynchronousIo aborts the owned synchronous pipe read");

    CloseHandle(readEntered);
    CloseHandle(workerFinished);
    CloseHandle(ownerFinished);
}

// ---------------------------------------------------------------------------
// legacy-protocol-correctness-and-raw-volume-cancellation-closure 4.2/4.3:
// hermetic stall fixture and separate measurements.
//
// A non-cooperative storage driver can leave a synchronous raw-volume
// DeviceIoControl parked indefinitely, and CancelSynchronousIo only cancels
// *I/O* requests. This fixture reproduces the "operation does not complete
// promptly" shape without touching any volume: the worker parks in
// WaitForSingleObject on an event the test releases explicitly, exactly like a
// kernel call that has not returned. Each guarantee is measured separately --
// time until cancellation is requested, whether the control path returned,
// time until the worker stopped, whether cleanup required the operation to
// complete, whether replacement work was admitted, and whether a later worker
// can write to a torn-down connection.
// ---------------------------------------------------------------------------

struct StallFixture {
    HANDLE entered = nullptr; // the worker signals it is parked
    HANDLE release = nullptr; // the test releases the parked operation
    ~StallFixture() {
        if (entered != nullptr) CloseHandle(entered);
        if (release != nullptr) CloseHandle(release);
    }
};

std::unique_ptr<StallFixture> CreateStallFixture() {
    auto fixture = std::make_unique<StallFixture>();
    fixture->entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    fixture->release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (fixture->entered == nullptr || fixture->release == nullptr) return nullptr;
    return fixture;
}

std::chrono::milliseconds ElapsedMilliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
}

void WaitForFlag(const std::atomic<bool>& flag, std::chrono::milliseconds bound) {
    const auto deadline = std::chrono::steady_clock::now() + bound;
    while (std::chrono::steady_clock::now() < deadline && !flag.load(std::memory_order_acquire)) {
        Sleep(5);
    }
}

void TestUncancelableStallTreatsCancellationAsARequest() {
    auto fixture = CreateStallFixture();
    Check(fixture != nullptr, "uncancelable-stall fixture creates its coordination events");
    if (!fixture) return;

    auto stopFlag = std::make_shared<std::atomic<bool>>(false);
    std::atomic<bool> workerReturned{false};
    std::vector<ffindexsvc::detail::ActiveWorker> workers;
    ffindexsvc::detail::ActiveWorker worker;
    worker.volumeId = 51;
    worker.stopFlag = stopFlag;
    worker.thread = std::thread([&] {
        SetEvent(fixture->entered);
        WaitForSingleObject(fixture->release, INFINITE); // the non-cooperative "DeviceIoControl"
        workerReturned.store(true, std::memory_order_release);
    });
    Check(WaitForSingleObject(fixture->entered, 2000) == WAIT_OBJECT_0,
          "the stalled worker reaches its non-cancelable operation");
    workers.push_back(std::move(worker));

    const auto requestStarted = std::chrono::steady_clock::now();
    const bool cancelRequested = ffindexsvc::detail::RequestWorkerStop(workers.front());
    const auto requestElapsed = ElapsedMilliseconds(requestStarted);
    Check(cancelRequested, "the owner reports the cancellation request was made");
    Check(stopFlag->load(std::memory_order_acquire), "the cancellation request publishes the stop flag");
    Check(requestElapsed < std::chrono::milliseconds(500),
          "requesting cancellation returns without waiting for the kernel operation");
    std::printf("measured: cancellation requested in %lld ms\n", static_cast<long long>(requestElapsed.count()));

    // The join cannot return while the operation is parked: cleanup requires
    // the operation to complete, so a request is never reported as completion.
    std::atomic<bool> ownerReturned{false};
    std::thread owner([&] {
        ffindexsvc::detail::StopAndJoinAll(workers);
        ownerReturned.store(true, std::memory_order_release);
    });
    WaitForFlag(ownerReturned, std::chrono::milliseconds(400));
    Check(!ownerReturned.load(std::memory_order_acquire),
          "the owner's join does not return while the operation is still parked (request != completion)");
    Check(!workerReturned.load(std::memory_order_acquire),
          "the stalled worker has not stopped while its operation is parked");
    std::printf("measured: control path still parked 400 ms after the request; cleanup needs kernel completion\n");

    const auto releaseStarted = std::chrono::steady_clock::now();
    SetEvent(fixture->release);
    WaitForFlag(ownerReturned, std::chrono::seconds(3));
    if (!ownerReturned.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "uncancelable-stall fixture did not release its worker after the operation returned\n");
        std::_Exit(EXIT_FAILURE);
    }
    owner.join();
    const auto completionElapsed = ElapsedMilliseconds(releaseStarted);
    Check(workerReturned.load(std::memory_order_acquire),
          "the stalled worker stops once its operation finally returns");
    Check(workers.empty(), "StopAndJoinAll drains the worker after the operation returns");
    Check(completionElapsed < std::chrono::seconds(3),
          "the observed worker stop follows the released operation promptly");
    std::printf("measured: worker stopped %lld ms after the operation returned\n",
                static_cast<long long>(completionElapsed.count()));
}

// A scan worker whose raw-volume operation finally returns after a stop was
// requested must observe the stop flag and emit nothing -- no batch, no
// terminal, and therefore no false success.
void TestLateCompletingScanOperationEmitsNothingAfterStop() {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "late-completion fixture creates an in-process pipe pair");
    if (!pair) return;
    auto fixture = CreateStallFixture();
    Check(fixture != nullptr, "late-completion fixture creates its coordination events");
    if (!fixture) return;

    const auto records = MakeSyntheticRecords();
    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> transportFailed{false};
    std::atomic<bool> workerReturned{false};
    ffindexsvc::ScanRunResult result = ffindexsvc::ScanRunResult::Completed;
    std::thread worker([&] {
        result = ffindexsvc::RunSyntheticVolumeScanForTesting(
            pair->server, writeMutex, ffprotocol::VolumeId{7}, ffprotocol::ProtocolVersion{2, 2},
            [&records, &fixture](std::vector<ffprotocol::MftRecordV2>& out) {
                SetEvent(fixture->entered);
                WaitForSingleObject(fixture->release, INFINITE);
                out = records;
                return true;
            },
            shouldStop, /*enumerationFailed=*/false, {}, &transportFailed);
        workerReturned.store(true, std::memory_order_release);
    });
    Check(WaitForSingleObject(fixture->entered, 2000) == WAIT_OBJECT_0,
          "the scan worker is parked inside its raw-volume-shaped operation");

    shouldStop.store(true, std::memory_order_release);
    SetEvent(fixture->release);
    WaitForFlag(workerReturned, std::chrono::seconds(3));
    if (!workerReturned.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "late-completion scan fixture did not return after the operation released\n");
        std::_Exit(EXIT_FAILURE);
    }
    worker.join();
    Check(result == ffindexsvc::ScanRunResult::Cancelled,
          "a late-completing operation observes the stop and reports cancellation, not completion");
    DWORD available = 0;
    const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &available, nullptr) != FALSE;
    Check(peeked && available == 0,
          "a late-completing cancelled scan writes neither a batch nor a false success terminal");
}

// The production connection-abort flag (the same flag RunCtrlConnection uses
// to stop dispatching and tear the generation down) must suppress a late
// worker's writes, so an abandoned worker can never write into a torn-down
// connection.
void TestDeadConnectionSuppressesLateWorkerWrites() {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "dead-connection fixture creates an in-process pipe pair");
    if (!pair) return;
    auto fixture = CreateStallFixture();
    Check(fixture != nullptr, "dead-connection fixture creates its coordination events");
    if (!fixture) return;

    const auto records = MakeSyntheticRecords();
    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> transportFailed{false};
    std::atomic<bool> workerReturned{false};
    ffindexsvc::ScanRunResult result = ffindexsvc::ScanRunResult::Completed;
    std::thread worker([&] {
        result = ffindexsvc::RunSyntheticVolumeScanForTesting(
            pair->server, writeMutex, ffprotocol::VolumeId{7}, ffprotocol::ProtocolVersion{2, 2},
            [&records, &fixture](std::vector<ffprotocol::MftRecordV2>& out) {
                SetEvent(fixture->entered);
                WaitForSingleObject(fixture->release, INFINITE);
                out = records;
                return true;
            },
            shouldStop, /*enumerationFailed=*/false, {}, &transportFailed);
        workerReturned.store(true, std::memory_order_release);
    });
    Check(WaitForSingleObject(fixture->entered, 2000) == WAIT_OBJECT_0,
          "the connection-abort worker is parked inside its operation");

    transportFailed.store(true, std::memory_order_release);
    SetEvent(fixture->release);
    WaitForFlag(workerReturned, std::chrono::seconds(3));
    if (!workerReturned.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "dead-connection fixture did not return after the operation released\n");
        std::_Exit(EXIT_FAILURE);
    }
    worker.join();
    Check(result == ffindexsvc::ScanRunResult::TransportFailed,
          "a dead connection turns the late worker's frame write into a transport failure");
    DWORD available = 0;
    const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &available, nullptr) != FALSE;
    Check(peeked && available == 0,
          "a late worker writes no batch or terminal into a torn-down connection");
}

// Supersede/admission ordering and per-volume isolation with a parked worker:
// the replacement for a stuck volume is not admitted until its own worker has
// returned, and a worker for a different volume is untouched.
void TestSupersedeDoesNotAdmitReplacementsWhileStalled() {
    auto fixture = CreateStallFixture();
    Check(fixture != nullptr, "supersede fixture creates its coordination events");
    if (!fixture) return;

    auto stopSeven = std::make_shared<std::atomic<bool>>(false);
    auto stopEight = std::make_shared<std::atomic<bool>>(false);
    std::atomic<uint32_t> enteredCount{0};
    std::vector<ffindexsvc::detail::ActiveWorker> workers;
    for (const uint32_t volumeId : {7u, 8u}) {
        ffindexsvc::detail::ActiveWorker worker;
        worker.volumeId = volumeId;
        worker.stopFlag = volumeId == 7 ? stopSeven : stopEight;
        worker.thread = std::thread([&fixture, &enteredCount] {
            enteredCount.fetch_add(1, std::memory_order_acq_rel);
            WaitForSingleObject(fixture->release, INFINITE);
        });
        workers.push_back(std::move(worker));
    }
    const auto enteredDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (enteredCount.load(std::memory_order_acquire) < 2 && std::chrono::steady_clock::now() < enteredDeadline) {
        Sleep(5);
    }
    Check(enteredCount.load(std::memory_order_acquire) == 2,
          "both per-volume workers reach their parked operations");

    std::atomic<bool> supersedeReturned{false};
    std::atomic<bool> onlyTheReplacementRemained{false};
    // The owner thread exclusively touches the worker vector until it is
    // joined, so the assertions below never read it concurrently (reviewer
    // finding B-MINOR: the previous form read workers.size() from the main
    // thread while the owner might be erasing).
    std::thread supersedeOwner([&] {
        ffindexsvc::detail::StopAndJoin(workers, 7);
        onlyTheReplacementRemained.store(workers.size() == 1 && workers.front().volumeId == 8,
                                         std::memory_order_release);
        supersedeReturned.store(true, std::memory_order_release);
    });
    WaitForFlag(supersedeReturned, std::chrono::milliseconds(400));
    Check(!supersedeReturned.load(std::memory_order_acquire),
          "a supersede for the stalled volume does not return while its operation is parked");
    Check(stopSeven->load(std::memory_order_acquire),
          "the supersede requests the stalled volume's worker to stop");
    Check(!stopEight->load(std::memory_order_acquire),
          "the supersede leaves the other volume's worker untouched");

    SetEvent(fixture->release);
    WaitForFlag(supersedeReturned, std::chrono::seconds(3));
    if (!supersedeReturned.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "supersede fixture did not return after the operation released\n");
        std::_Exit(EXIT_FAILURE);
    }
    supersedeOwner.join();
    Check(onlyTheReplacementRemained.load(std::memory_order_acquire),
          "the stalled volume's slot is drained and only the replacement is admitted afterwards");

    ffindexsvc::detail::StopAndJoinAll(workers);
    Check(workers.empty(), "the remaining volume's worker drains on the next stop");
}

// Repeated stop/start cycles with a parked operation must leave no worker
// behind and must not accumulate owners.
void TestRepeatedStalledStopStartCyclesRemainBounded() {
    constexpr uint32_t kCycles = 3;
    std::vector<float> cycleMilliseconds;
    for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
        auto fixture = CreateStallFixture();
        Check(fixture != nullptr, "repeated-cycle fixture creates its coordination events");
        if (!fixture) return;
        std::vector<ffindexsvc::detail::ActiveWorker> workers;
        ffindexsvc::detail::ActiveWorker worker;
        worker.volumeId = 60 + cycle;
        worker.stopFlag = std::make_shared<std::atomic<bool>>(false);
        worker.thread = std::thread([&fixture] {
            SetEvent(fixture->entered);
            WaitForSingleObject(fixture->release, INFINITE);
        });
        Check(WaitForSingleObject(fixture->entered, 2000) == WAIT_OBJECT_0,
              "a repeated-cycle worker reaches its parked operation");
        workers.push_back(std::move(worker));

        const auto cycleStarted = std::chrono::steady_clock::now();
        ffindexsvc::detail::RequestWorkerStop(workers.front());
        SetEvent(fixture->release);
        ffindexsvc::detail::StopAndJoinAll(workers);
        const auto cycleElapsed = ElapsedMilliseconds(cycleStarted);
        Check(workers.empty(), "every repeated stop/start cycle drains its worker");
        Check(cycleElapsed < std::chrono::seconds(2),
              "every repeated stop/start cycle completes within the bounded fixture deadline");
        cycleMilliseconds.push_back(static_cast<float>(cycleElapsed.count()));
    }
    Check(cycleMilliseconds.size() == kCycles, "all repeated stop/start cycles were measured");
    std::printf("measured: repeated stalled stop/start cycles completed without accumulation\n");
}

// legacy-protocol-correctness-and-raw-volume-cancellation-closure 8.6
// (reviewer finding B-MINOR): a stop that lands while a pre-enumeration setup
// failure is in flight must win. Cancellation stays terminal-free on the setup
// paths exactly as ScanOutput::Finish already guarantees for the enumeration
// path, for both a failure-terminal-capable peer and an unreportable one.
void TestStopDuringSetupFailureStaysSilent() {
    const ffprotocol::ProtocolVersion versions[] = {{2, 2}, {2, 1}, {1, 5}};
    for (const auto negotiated : versions) {
        auto pair = CreatePipePair();
        Check(pair != nullptr, "stop-vs-setup-failure case creates an in-process pipe pair");
        if (!pair) return;
        const wchar_t unmounted = ResolveUnmountedDriveLetter();
        Check(unmounted != L'\0', "the test host has a drive letter with no volume behind it");
        if (unmounted == L'\0') return;

        std::mutex writeMutex;
        std::atomic<bool> shouldStop{false};
        std::atomic<bool> transportFailed{false};
        const ffindexsvc::ScanRunResult result = ffindexsvc::RunVolumeScanWithFailureHookForTesting(
            pair->server, writeMutex, ffprotocol::VolumeId{7}, unmounted, {}, false, negotiated, shouldStop,
            [&shouldStop] { shouldStop.store(true, std::memory_order_release); }, &transportFailed);

        char description[192];
        std::snprintf(description, sizeof(description),
                      "{%u,%u} a stop during a setup failure reports cancellation",
                      negotiated.major, negotiated.minor);
        Check(result == ffindexsvc::ScanRunResult::Cancelled, description);
        std::snprintf(description, sizeof(description),
                      "{%u,%u} a stop during a setup failure requests no connection abort",
                      negotiated.major, negotiated.minor);
        Check(!transportFailed.load(), description);
        DWORD available = 0;
        const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &available, nullptr) != FALSE;
        std::snprintf(description, sizeof(description),
                      "{%u,%u} a stop during a setup failure writes no terminal",
                      negotiated.major, negotiated.minor);
        Check(peeked && available == 0, description);
    }
}

// legacy-protocol-correctness-and-raw-volume-cancellation-closure 4.2/5.2
// (reviewer finding B-MAJOR): the journal worker owns the same ActiveWorker
// lifecycle as the scan worker, and its frames are written through the same
// production WriteFrameWithDeadline primitive. The fixture parks a
// journal-shaped worker in the same non-cancelable operation and measures the
// same promises separately: the stop request returns immediately, the owner
// waits for the operation, per-volume stop leaves the other journal untouched,
// and a late worker whose connection is already torn down cannot frame a
// journal message. The raw FSCTL_READ_USN_JOURNAL completion bound itself
// remains UNPROVEN (no real volume; see the architecture note).
void TestJournalWorkerStopUsesTheSameBoundedRequest() {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "journal-worker fixture creates an in-process pipe pair");
    if (!pair) return;
    auto fixture = CreateStallFixture();
    Check(fixture != nullptr, "journal-worker fixture creates its coordination events");
    if (!fixture) return;

    auto stopEleven = std::make_shared<std::atomic<bool>>(false);
    auto stopTwelve = std::make_shared<std::atomic<bool>>(false);
    std::atomic<uint32_t> entered{0};
    std::vector<ffindexsvc::detail::ActiveWorker> activeJournals;
    for (const uint32_t volumeId : {11u, 12u}) {
        ffindexsvc::detail::ActiveWorker worker;
        worker.volumeId = volumeId;
        worker.stopFlag = volumeId == 11 ? stopEleven : stopTwelve;
        worker.thread = std::thread([&fixture, &entered] {
            entered.fetch_add(1, std::memory_order_acq_rel);
            WaitForSingleObject(fixture->release, INFINITE); // the non-cooperative journal IOCTL
        });
        activeJournals.push_back(std::move(worker));
    }
    const auto enteredDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (entered.load(std::memory_order_acquire) < 2 && std::chrono::steady_clock::now() < enteredDeadline) {
        Sleep(5);
    }
    Check(entered.load(std::memory_order_acquire) == 2,
          "both journal workers reach their parked operations");

    const auto requestStarted = std::chrono::steady_clock::now();
    const bool cancelRequested = ffindexsvc::detail::RequestWorkerStop(activeJournals[0]);
    const auto requestElapsed = ElapsedMilliseconds(requestStarted);
    Check(cancelRequested, "the journal owner reports the cancellation request was made");
    Check(requestElapsed < std::chrono::milliseconds(500),
          "requesting a journal cancellation returns without waiting for the operation");
    Check(stopEleven->load(std::memory_order_acquire) && !stopTwelve->load(std::memory_order_acquire),
          "the journal stop request is scoped to the requested volume only");

    std::atomic<bool> ownerReturned{false};
    std::thread owner([&] {
        ffindexsvc::detail::StopAndJoin(activeJournals, 11);
        ownerReturned.store(true, std::memory_order_release);
    });
    WaitForFlag(ownerReturned, std::chrono::milliseconds(400));
    Check(!ownerReturned.load(std::memory_order_acquire),
          "a journal stop does not return while the journal operation is parked (request != completion)");
    SetEvent(fixture->release);
    WaitForFlag(ownerReturned, std::chrono::seconds(3));
    if (!ownerReturned.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "journal-worker fixture did not return after the operation released\n");
        std::_Exit(EXIT_FAILURE);
    }
    owner.join();
    Check(activeJournals.size() == 1 && activeJournals.front().volumeId == 12,
          "the stopped journal's slot is drained and the other journal keeps running");
    ffindexsvc::detail::StopAndJoinAll(activeJournals);
    Check(activeJournals.empty(), "StopAndJoinAll drains every journal worker slot");

    // The production journal frame writer suppresses a late worker whose
    // connection was already torn down.
    std::atomic<bool> connectionDead{true};
    constexpr uint16_t kJournalOpenedMessage = static_cast<uint16_t>(ffprotocol::MessageType::UsnJournalOpened);
    const ffprotocol::UsnJournalOpenedPayload journalOpened{ffprotocol::VolumeId{11}, 1, 2};
    const bool journalWriteAccepted =
        ffindexsvc::WriteFrameWithDeadline(pair->server, kJournalOpenedMessage, &journalOpened,
                                           sizeof(journalOpened), &connectionDead);
    Check(!journalWriteAccepted,
          "a late journal worker's frame write is suppressed on a torn-down connection");
    DWORD available = 0;
    const bool peeked = PeekNamedPipe(pair->client, nullptr, 0, nullptr, &available, nullptr) != FALSE;
    Check(peeked && available == 0, "no journal frame reaches the pipe from the suppressed late write");
}

} // namespace

int main() {
    // {2,2}: the additive ScanFailed terminal replaces the false ScanComplete.
    RunDeliveredFailureTerminalCase(ffprotocol::ProtocolVersion{2, 2},
                                    static_cast<uint16_t>(ffprotocol::MessageType::ScanFailed),
                                    "a setup-failed scan reports ScanFailed to a {2,2} peer");
    // pre-{2,2}: no terminal the peer cannot parse and no false-success
    // ScanComplete -- the attempt ends by aborting the connection instead.
    RunUnreportableFailureCase(ffprotocol::ProtocolVersion{2, 1}, "{2,1}");
    RunUnreportableFailureCase(ffprotocol::ProtocolVersion{2, 0}, "{2,0}");
    RunUnreportableFailureCase(ffprotocol::ProtocolVersion{1, 5}, "{1,5}");
    // The mid-enumeration failure path shares ScanOutput::Finish; a legacy
    // peer must also see no terminal there.
    RunUnreportableSyntheticMidEnumerationCase(ffprotocol::ProtocolVersion{2, 1}, "{2,1}");
    RunUnreportableSyntheticMidEnumerationCase(ffprotocol::ProtocolVersion{1, 5}, "{1,5}");
    // Successful legacy scans keep their exact terminal bytes and batch layout.
    RunLegacySuccessTerminalBytesCase(ffprotocol::ProtocolVersion{1, 5}, "{1,5}");
    RunLegacySuccessTerminalBytesCase(ffprotocol::ProtocolVersion{2, 0}, "{2,0}");
    RunLegacySuccessTerminalBytesCase(ffprotocol::ProtocolVersion{2, 1}, "{2,1}");
    RunLegacySuccessTerminalBytesCase(ffprotocol::ProtocolVersion{2, 2}, "{2,2}");
    RunCancelledCase();
    RunTerminalTransportFailureCase();
    TestSyntheticBatchWriteFailureAbortsBeforeTerminal();
    TestSyntheticTerminalWriteFailureIsNotRetried();
    TestVolumeHandleClosesWhenPostOpenWorkThrows();
    TestSyntheticScanCancellationDuringActiveSource();
    TestSyntheticWorkerExceptionUsesProductionBoundary();
    TestStopAndJoinCancelsSynchronousWorkerIo();
    TestUncancelableStallTreatsCancellationAsARequest();
    TestLateCompletingScanOperationEmitsNothingAfterStop();
    TestDeadConnectionSuppressesLateWorkerWrites();
    TestSupersedeDoesNotAdmitReplacementsWhileStalled();
    TestRepeatedStalledStopStartCyclesRemainBounded();
    TestStopDuringSetupFailureStaysSilent();
    TestJournalWorkerStopUsesTheSameBoundedRequest();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
