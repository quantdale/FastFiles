// index-durability-false-success-closure 2.3 (PA-001): the service must
// never report ScanComplete for a scan that could not run to completion.
// Drives the real RunVolumeScan against an unmounted drive letter over an
// in-process pipe pair: both reachable failure paths (backup-privilege
// enable denial in an unprivileged test process, and if that somehow
// succeeds, the raw-volume open denial for a letter with no volume behind
// it) must produce the ScanFailed terminal for a {2,2} peer, and must keep
// today's legacy ScanComplete byte-for-byte for a pre-{2,2} peer.
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <windows.h>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Commands.h"
#include "ffprotocol/Version.h"
#include "../TestSupport.h"

#include "VolumeScanner.h"

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

std::unique_ptr<PipePair> CreatePipePair() {
    const std::wstring name = L"\\\\.\\pipe\\FastFilesScanTerminalTest-" +
                              std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64());
    auto pair = std::make_unique<PipePair>();
    // FILE_FLAG_OVERLAPPED because the service write path is
    // WriteFrameOverlapped (WriteDeadline.h) -- same handle shape as the
    // production ctrl connection.
    pair->server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                    1, 65536, 65536, 0, nullptr);
    if (pair->server == INVALID_HANDLE_VALUE) return nullptr;
    pair->client = CreateFileW(name.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pair->client == INVALID_HANDLE_VALUE) return nullptr;
    if (ConnectNamedPipe(pair->server, nullptr) == FALSE && GetLastError() != ERROR_PIPE_CONNECTED) {
        return nullptr;
    }
    return pair;
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
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
