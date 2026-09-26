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
                                           [] {}, std::forward<OnException>(onException));
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

void RunTerminalCase(ffprotocol::ProtocolVersion negotiated, uint16_t expectedTerminal,
                     const char* description) {
    auto pair = CreatePipePair();
    Check(pair != nullptr, "scan-terminal case creates an in-process pipe pair");
    if (!pair) return;

    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    constexpr ffprotocol::VolumeId kVolume{7};
    // Drive letter Q is deliberately chosen as a letter with no volume on
    // the test host; whichever setup stage fails first (privilege enable,
    // volume open, or geometry query), the scan never enumerates and the
    // single terminal frame is what distinguishes truth from a false
    // success.
    const ffindexsvc::ScanRunResult result =
        ffindexsvc::RunVolumeScan(pair->server, writeMutex, kVolume, L'Q', {}, false, negotiated, shouldStop);
    Check(result == ffindexsvc::ScanRunResult::FailedTerminalDelivered,
          "a setup failure reports a delivered failure terminal");

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

} // namespace

int main() {
    // {2,2}: the additive ScanFailed terminal replaces the false ScanComplete.
    RunTerminalCase(ffprotocol::ProtocolVersion{2, 2},
                    static_cast<uint16_t>(ffprotocol::MessageType::ScanFailed),
                    "a setup-failed scan reports ScanFailed to a {2,2} peer");
    // {2,1}: legacy peers keep today's terminal byte-for-byte (no type they
    // cannot parse is ever emitted to them).
    RunTerminalCase(ffprotocol::ProtocolVersion{2, 1},
                    static_cast<uint16_t>(ffprotocol::MessageType::ScanComplete),
                    "a setup-failed scan keeps the legacy ScanComplete for a {2,1} peer");
    RunCancelledCase();
    RunTerminalTransportFailureCase();
    TestSyntheticBatchWriteFailureAbortsBeforeTerminal();
    TestSyntheticTerminalWriteFailureIsNotRetried();
    TestVolumeHandleClosesWhenPostOpenWorkThrows();
    TestSyntheticScanCancellationDuringActiveSource();
    TestSyntheticWorkerExceptionUsesProductionBoundary();
    TestStopAndJoinCancelsSynchronousWorkerIo();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
