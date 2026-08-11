#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Commands.h"
#include "ffprotocol/Frame.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

struct AnonymousPipe {
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    AnonymousPipe() = default;
    AnonymousPipe(const AnonymousPipe&) = delete;
    AnonymousPipe& operator=(const AnonymousPipe&) = delete;
    AnonymousPipe(AnonymousPipe&& other) noexcept
        : read(std::exchange(other.read, nullptr)), write(std::exchange(other.write, nullptr)) {}
    AnonymousPipe& operator=(AnonymousPipe&& other) noexcept {
        if (this != &other) {
            if (read != nullptr) CloseHandle(read);
            if (write != nullptr) CloseHandle(write);
            read = std::exchange(other.read, nullptr);
            write = std::exchange(other.write, nullptr);
        }
        return *this;
    }
    ~AnonymousPipe() {
        if (read != nullptr) CloseHandle(read);
        if (write != nullptr) CloseHandle(write);
    }
};

AnonymousPipe MakePipe() {
    AnonymousPipe pipe;
    if (!CreatePipe(&pipe.read, &pipe.write, nullptr, 64 * 1024)) {
        pipe.read = nullptr;
        pipe.write = nullptr;
    }
    return pipe;
}

struct NamedPipePair {
    HANDLE server = nullptr;
    HANDLE client = nullptr;
    NamedPipePair() = default;
    NamedPipePair(const NamedPipePair&) = delete;
    NamedPipePair& operator=(const NamedPipePair&) = delete;
    NamedPipePair(NamedPipePair&& other) noexcept
        : server(std::exchange(other.server, nullptr)), client(std::exchange(other.client, nullptr)) {}
    NamedPipePair& operator=(NamedPipePair&& other) noexcept {
        if (this != &other) {
            if (server != nullptr) CloseHandle(server);
            if (client != nullptr) CloseHandle(client);
            server = std::exchange(other.server, nullptr);
            client = std::exchange(other.client, nullptr);
        }
        return *this;
    }
    ~NamedPipePair() {
        if (server != nullptr) CloseHandle(server);
        if (client != nullptr) CloseHandle(client);
    }
};

NamedPipePair MakeOverlappedNamedPipe() {
    NamedPipePair pipe;
    const std::wstring name = L"\\\\.\\pipe\\FastFilesIpcTest." + std::to_wstring(GetCurrentProcessId());
    pipe.server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                                   64 * 1024, 64 * 1024, 0, nullptr);
    if (pipe.server == INVALID_HANDLE_VALUE) {
        pipe.server = nullptr;
        return pipe;
    }

    OVERLAPPED connect{};
    connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (connect.hEvent == nullptr) {
        CloseHandle(pipe.server);
        pipe.server = nullptr;
        return pipe;
    }
    const BOOL connected = ConnectNamedPipe(pipe.server, &connect);
    const DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();
    pipe.client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    bool connectionReady = connected != FALSE || connectError == ERROR_PIPE_CONNECTED;
    if (!connectionReady && connectError == ERROR_IO_PENDING) {
        const DWORD waitResult = WaitForSingleObject(connect.hEvent, 2000);
        DWORD ignored = 0;
        connectionReady = waitResult == WAIT_OBJECT_0
            && GetOverlappedResult(pipe.server, &connect, &ignored, FALSE) != FALSE;
    }
    CloseHandle(connect.hEvent);
    if (!connectionReady || pipe.client == INVALID_HANDLE_VALUE) {
        if (pipe.client == INVALID_HANDLE_VALUE) pipe.client = nullptr;
        CloseHandle(pipe.server);
        pipe.server = nullptr;
    }
    return pipe;
}

void TestMaximumValidPayloadRoundTrips() {
    AnonymousPipe pipe = MakePipe();
    Check(pipe.read != nullptr && pipe.write != nullptr, "anonymous byte-stream pipe is created");
    if (pipe.read == nullptr || pipe.write == nullptr) return;

    const size_t payloadSize = ffprotocol::kMaxFrameSize - sizeof(ffprotocol::FrameHeader);
    std::vector<uint8_t> payload(payloadSize);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i % 251);
    bool writeResult = false;
    std::thread writer([&] {
        writeResult = ffipc::WriteFrame(pipe.write,
            static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch),
            payload.data(), static_cast<uint32_t>(payload.size()));
    });
    const auto received = ffipc::ReadFrame(pipe.read);
    writer.join();
    Check(writeResult, "maximum-size valid frame is written");
    Check(received.has_value(), "maximum-size valid frame is read");
    Check(received && received->header.totalLength == ffprotocol::kMaxFrameSize,
          "maximum-size frame retains its bounded total length");
    Check(received && received->payload == payload,
          "maximum-size payload round-trips without truncation or over-allocation");
}

void TestOversizedWriteRejectedBeforeIo() {
    AnonymousPipe pipe = MakePipe();
    Check(pipe.read != nullptr && pipe.write != nullptr, "oversized-write pipe is created");
    if (pipe.read == nullptr || pipe.write == nullptr) return;
    std::vector<uint8_t> payload(ffprotocol::kMaxFrameSize, 0xA5);
    Check(!ffipc::WriteFrame(pipe.write,
        static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch),
        payload.data(), static_cast<uint32_t>(payload.size())),
        "payload exceeding the frame maximum is refused before I/O");
}

void TestMalformedHeadersRejectedWithoutPayloadRead() {
    for (const uint32_t badLength : {ffprotocol::kMaxFrameSize + 1, UINT32_MAX}) {
        AnonymousPipe pipe = MakePipe();
        Check(pipe.read != nullptr && pipe.write != nullptr, "malformed-header pipe is created");
        if (pipe.read == nullptr || pipe.write == nullptr) continue;
        ffprotocol::FrameHeader header{};
        header.totalLength = badLength;
        header.messageType = static_cast<uint16_t>(ffprotocol::MessageType::Heartbeat);
        header.structVersion = ffprotocol::kCurrentStructVersion;
        DWORD written = 0;
        const bool headerWritten = WriteFile(pipe.write, &header, sizeof(header), &written, nullptr) != FALSE;
        Check(headerWritten && written == sizeof(header), "malformed header bytes are delivered");
        Check(!ffipc::ReadFrame(pipe.read).has_value(),
              "oversized header is rejected without waiting for or allocating its declared payload");
    }
}

void TestOverlappedReadCompletesAndWriteRoundTrips() {
    NamedPipePair pipe = MakeOverlappedNamedPipe();
    Check(pipe.server != nullptr && pipe.client != nullptr, "overlapped named pipe is connected");
    if (pipe.server == nullptr || pipe.client == nullptr) return;

    bool writeResult = false;
    std::thread writer([&] {
        writeResult = ffipc::WriteFrame(pipe.client,
            static_cast<uint16_t>(ffprotocol::MessageType::Heartbeat), nullptr, 0);
    });
    const auto received = ffipc::ReadFrameOverlapped(
        pipe.server, std::chrono::steady_clock::now() + std::chrono::seconds(2));
    writer.join();
    Check(writeResult, "synchronous client write reaches overlapped service read");
    Check(received.has_value(), "overlapped service read completes without polling");
    Check(received && received->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::Heartbeat),
          "overlapped service read preserves the frame type");

    Check(ffipc::WriteFrameOverlapped(pipe.server,
                                      static_cast<uint16_t>(ffprotocol::MessageType::Heartbeat)),
          "overlapped service write completes");
    Check(ffipc::ReadFrame(pipe.client).has_value(), "synchronous client read receives overlapped service write");
}

void TestOverlappedReadTimesOut() {
    NamedPipePair pipe = MakeOverlappedNamedPipe();
    Check(pipe.server != nullptr && pipe.client != nullptr, "timeout named pipe is connected");
    if (pipe.server == nullptr || pipe.client == nullptr) return;

    const auto started = std::chrono::steady_clock::now();
    const auto received = ffipc::ReadFrameOverlapped(
        pipe.server, started + std::chrono::milliseconds(100));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Check(!received.has_value(), "overlapped service read returns no frame on timeout");
    Check(elapsed >= std::chrono::milliseconds(50) && elapsed < std::chrono::seconds(2),
          "overlapped timeout remains bounded");
}

void TestSynchronousReadFallbackCompletesAndTimesOut() {
    AnonymousPipe pipe = MakePipe();
    Check(pipe.read != nullptr && pipe.write != nullptr, "synchronous fallback pipe is created");
    if (pipe.read == nullptr || pipe.write == nullptr) return;

    bool writeResult = false;
    std::thread writer([&] {
        writeResult = ffipc::WriteFrame(pipe.write,
            static_cast<uint16_t>(ffprotocol::MessageType::Heartbeat), nullptr, 0);
    });
    const auto received = ffipc::ReadFrameWithTimeout(
        pipe.read, std::chrono::steady_clock::now() + std::chrono::seconds(2));
    writer.join();
    Check(writeResult && received.has_value(), "non-overlapped test handle uses the bounded read fallback");

    const auto timeout = ffipc::ReadFrameWithTimeout(
        pipe.read, std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
    Check(!timeout.has_value(), "non-overlapped fallback also returns on timeout");
}

} // namespace

int main() {
    TestMaximumValidPayloadRoundTrips();
    TestOversizedWriteRejectedBeforeIo();
    TestMalformedHeadersRejectedWithoutPayloadRead();
    TestOverlappedReadCompletesAndWriteRoundTrips();
    TestOverlappedReadTimesOut();
    TestSynchronousReadFallbackCompletesAndTimesOut();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
