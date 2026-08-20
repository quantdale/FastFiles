#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

void TestRealNamedPipeHandshakeRoundTrips() {
    NamedPipePair pipe = MakeOverlappedNamedPipe();
    Check(pipe.server != nullptr && pipe.client != nullptr, "overlapped named pipe is connected");
    if (pipe.server == nullptr || pipe.client == nullptr) return;

    const ffprotocol::HandshakeRequest request{ffprotocol::kCurrentProtocolVersion};
    bool writeResult = false;
    std::thread writer([&] {
        writeResult = ffipc::WriteFrame(pipe.client,
            static_cast<uint16_t>(ffprotocol::MessageType::Handshake), &request, sizeof(request));
    });
    const auto received = ffipc::ReadFrameOverlapped(
        pipe.server, std::chrono::steady_clock::now() + std::chrono::seconds(2));
    writer.join();
    Check(writeResult, "real named-pipe handshake request is written");
    Check(received.has_value(), "real named-pipe handshake request is read asynchronously");
    Check(received && received->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::Handshake),
          "handshake request message type crosses the real pipe");
    Check(received && received->payload.size() == sizeof(request), "handshake request payload is bounded and complete");
    if (received && received->payload.size() == sizeof(request)) {
        ffprotocol::HandshakeRequest parsed{};
        std::memcpy(&parsed, received->payload.data(), sizeof(parsed));
        Check(ffprotocol::IsVersionCompatible(ffprotocol::kCurrentProtocolVersion, parsed.clientVersion),
              "handshake request version is compatible");
    }

    const ffprotocol::HandshakeAckPayload ack{ffprotocol::kCurrentProtocolVersion};
    Check(ffipc::WriteFrameOverlapped(pipe.server,
                                      static_cast<uint16_t>(ffprotocol::MessageType::HandshakeAck), &ack, sizeof(ack)),
          "real named-pipe handshake acknowledgement is written asynchronously");
    const auto reply = ffipc::ReadFrame(pipe.client);
    Check(reply && reply->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::HandshakeAck),
          "handshake acknowledgement message type crosses the real pipe");
    Check(reply && reply->payload.size() == sizeof(ack), "handshake acknowledgement payload is complete");
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

void TestPartialHeaderTimesOut() {
    NamedPipePair pipe = MakeOverlappedNamedPipe();
    Check(pipe.server != nullptr && pipe.client != nullptr, "partial-header pipe is connected");
    if (pipe.server == nullptr || pipe.client == nullptr) return;

    ffprotocol::FrameHeader header{};
    header.totalLength = sizeof(header);
    header.structVersion = ffprotocol::kCurrentStructVersion;
    header.messageType = static_cast<uint16_t>(ffprotocol::MessageType::Heartbeat);
    DWORD written = 0;
    Check(WriteFile(pipe.client, &header, sizeof(header) - 1, &written, nullptr) != FALSE
              && written == sizeof(header) - 1,
          "partial frame header bytes are delivered");
    const auto started = std::chrono::steady_clock::now();
    const auto received = ffipc::ReadFrameOverlapped(
        pipe.server, started + std::chrono::milliseconds(100));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Check(!received.has_value(), "partial frame header returns no frame");
    Check(elapsed < std::chrono::seconds(2), "partial header timeout has a real wall-clock bound");
}

void TestPeerDeathDuringFrameReturns() {
    NamedPipePair pipe = MakeOverlappedNamedPipe();
    Check(pipe.server != nullptr && pipe.client != nullptr, "peer-death pipe is connected");
    if (pipe.server == nullptr || pipe.client == nullptr) return;

    ffprotocol::FrameHeader header{};
    header.totalLength = static_cast<uint32_t>(sizeof(header) + sizeof(uint32_t));
    header.structVersion = ffprotocol::kCurrentStructVersion;
    header.messageType = static_cast<uint16_t>(ffprotocol::MessageType::Heartbeat);
    DWORD written = 0;
    Check(WriteFile(pipe.client, &header, sizeof(header), &written, nullptr) != FALSE
              && written == sizeof(header),
          "peer-death frame header is delivered");
    CloseHandle(pipe.client);
    pipe.client = nullptr;
    Check(!ffipc::ReadFrameOverlapped(
              pipe.server, std::chrono::steady_clock::now() + std::chrono::seconds(2)).has_value(),
          "peer death during a payload returns a bounded read failure");
}

void TestOverlappedWriteTimesOutWhenPeerDoesNotRead() {
    NamedPipePair pipe = MakeOverlappedNamedPipe();
    Check(pipe.server != nullptr && pipe.client != nullptr, "stalled-write pipe is connected");
    if (pipe.server == nullptr || pipe.client == nullptr) return;

    std::vector<uint8_t> payload(ffprotocol::kMaxFrameSize - sizeof(ffprotocol::FrameHeader), 0x5A);
    const auto started = std::chrono::steady_clock::now();
    const bool written = ffipc::WriteFrameOverlapped(
        pipe.server, static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch), payload.data(),
        static_cast<uint32_t>(payload.size()), started + std::chrono::milliseconds(100));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Check(!written, "stalled overlapped write is cancelled");
    Check(elapsed < std::chrono::seconds(2), "stalled write has a real wall-clock bound");
}

void TestPartialFrameReadTimesOutWithoutAHelperThread() {
    NamedPipePair pipe = MakeOverlappedNamedPipe();
    Check(pipe.server != nullptr && pipe.client != nullptr, "partial-frame timeout pipe is connected");
    if (pipe.server == nullptr || pipe.client == nullptr) return;

    ffprotocol::FrameHeader header{};
    header.totalLength = static_cast<uint32_t>(sizeof(header) + sizeof(uint32_t));
    header.structVersion = ffprotocol::kCurrentStructVersion;
    header.messageType = static_cast<uint16_t>(ffprotocol::MessageType::Heartbeat);
    DWORD written = 0;
    Check(WriteFile(pipe.client, &header, sizeof(header), &written, nullptr) != FALSE
               && written == sizeof(header),
           "partial-frame header is delivered before the payload stalls");
    const auto started = std::chrono::steady_clock::now();
    const auto received = ffipc::ReadFrameOverlapped(
        pipe.server, started + std::chrono::milliseconds(100));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Check(!received.has_value(), "partial-frame payload stall returns no frame");
    Check(elapsed < std::chrono::seconds(2), "partial-frame timeout has a real wall-clock bound");
}

// close-independent-validation-gaps deferred follow-up: the synchronous reader
// (ReadExact loop) must reassemble a frame whose header and payload arrive as
// separate pipe writes and must not require a single monolithic ReadFile.
void TestSynchronousReadReassemblesAcrossChunkedWrites() {
    AnonymousPipe pipe = MakePipe();
    Check(pipe.read != nullptr && pipe.write != nullptr, "chunked-write pipe is created");
    if (pipe.read == nullptr || pipe.write == nullptr) return;

    const size_t payloadSize = 4096;
    std::vector<uint8_t> payload(payloadSize);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i % 253);

    ffprotocol::FrameHeader header{};
    header.totalLength = static_cast<uint32_t>(sizeof(header) + payloadSize);
    header.structVersion = ffprotocol::kCurrentStructVersion;
    header.messageType = static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch);

    std::thread writer([&] {
        DWORD written = 0;
        Check(WriteFile(pipe.write, &header, sizeof(header), &written, nullptr) != FALSE
                   && written == sizeof(header),
               "chunked header delivered before payload");
        // Deliver the payload in small pieces so the reader must accumulate
        // short reads rather than receiving one full buffer.
        size_t offset = 0;
        while (offset < payloadSize) {
            const size_t chunk = (std::min)(static_cast<size_t>(37), payloadSize - offset);
            DWORD chunkWritten = 0;
            if (!WriteFile(pipe.write, payload.data() + offset, static_cast<DWORD>(chunk),
                           &chunkWritten, nullptr)) {
                break;
            }
            offset += chunkWritten;
        }
        Check(offset == payloadSize, "chunked payload fully delivered");
    });

    const auto received = ffipc::ReadFrame(pipe.read);
    writer.join();
    Check(received.has_value(), "synchronous reader reassembles a frame split across chunked writes");
    Check(received && received->header.totalLength == sizeof(header) + payloadSize,
          "reassembled frame reports its exact declared length");
    Check(received && received->payload == payload,
          "reassembled payload matches byte-for-byte across short reads");
}

// close-independent-validation-gaps deferred follow-up: a clean peer disconnect
// (write handle closed) must end the synchronous reader with nullopt for any
// subsequent frame, while a fully-delivered frame before the disconnect is still
// returned intact.
void TestSynchronousReadReturnsOnBrokenPipe() {
    AnonymousPipe pipe = MakePipe();
    Check(pipe.read != nullptr && pipe.write != nullptr, "broken-pipe test pipe is created");
    if (pipe.read == nullptr || pipe.write == nullptr) return;

    const uint32_t sentinel = 0x12345678u;
    Check(ffipc::WriteFrame(pipe.write,
             static_cast<uint16_t>(ffprotocol::MessageType::Heartbeat), &sentinel, sizeof(sentinel)),
           "pre-disconnect frame is written");
    const auto first = ffipc::ReadFrame(pipe.read);
    Check(first.has_value(), "frame delivered before disconnect is read intact");
    Check(first && first->payload.size() == sizeof(sentinel)
               && std::memcmp(first->payload.data(), &sentinel, sizeof(sentinel)) == 0,
           "pre-disconnect frame payload is read intact");

    CloseHandle(pipe.write);
    pipe.write = nullptr;
    const auto afterClose = ffipc::ReadFrame(pipe.read);
    Check(!afterClose.has_value(), "synchronous reader returns nullopt on a broken pipe");
}

// close-independent-validation-gaps deferred follow-up (IPC concurrency): two
// complete frames delivered in a single coalesced write must be read back as two
// distinct, correctly-delimited frames -- the reader must not stop at the first
// frame or merge the two payloads.
void TestBackToBackFramesAreDelimited() {
    AnonymousPipe pipe = MakePipe();
    Check(pipe.read != nullptr && pipe.write != nullptr, "back-to-back pipe is created");
    if (pipe.read == nullptr || pipe.write == nullptr) return;

    std::vector<uint8_t> frame1(1024, 0x11);
    std::vector<uint8_t> frame2(2048, 0x22);
    ffprotocol::FrameHeader header1{};
    header1.totalLength = static_cast<uint32_t>(sizeof(header1) + frame1.size());
    header1.structVersion = ffprotocol::kCurrentStructVersion;
    header1.messageType = static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch);
    ffprotocol::FrameHeader header2{};
    header2.totalLength = static_cast<uint32_t>(sizeof(header2) + frame2.size());
    header2.structVersion = ffprotocol::kCurrentStructVersion;
    header2.messageType = static_cast<uint16_t>(ffprotocol::MessageType::UsnJournalOpened);

    std::vector<uint8_t> coalesced;
    coalesced.reserve(header1.totalLength + header2.totalLength);
    coalesced.insert(coalesced.end(), reinterpret_cast<const uint8_t*>(&header1),
                     reinterpret_cast<const uint8_t*>(&header1) + sizeof(header1));
    coalesced.insert(coalesced.end(), frame1.begin(), frame1.end());
    coalesced.insert(coalesced.end(), reinterpret_cast<const uint8_t*>(&header2),
                     reinterpret_cast<const uint8_t*>(&header2) + sizeof(header2));
    coalesced.insert(coalesced.end(), frame2.begin(), frame2.end());

    DWORD written = 0;
    Check(WriteFile(pipe.write, coalesced.data(), static_cast<DWORD>(coalesced.size()),
                    &written, nullptr) != FALSE && written == coalesced.size(),
          "two frames are delivered coalesced in one write");

    const auto first = ffipc::ReadFrame(pipe.read);
    const auto second = ffipc::ReadFrame(pipe.read);
    Check(first.has_value(), "first coalesced frame is read");
    Check(first && first->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch),
          "first frame keeps its message type");
    Check(first && first->payload == frame1, "first frame payload is exactly its own bytes");
    Check(second.has_value(), "second coalesced frame is read after the first");
    Check(second && second->header.messageType == static_cast<uint16_t>(ffprotocol::MessageType::UsnJournalOpened),
          "second frame keeps its message type");
    Check(second && second->payload == frame2, "second frame payload is exactly its own bytes");
}

// close-independent-validation-gaps deferred follow-up (IPC concurrency): a burst
// of many distinct frames must all round-trip in order with no loss, duplication,
// or cross-frame payload bleed.
void TestManyFramesRoundTripDeterministically() {
    AnonymousPipe pipe = MakePipe();
    Check(pipe.read != nullptr && pipe.write != nullptr, "burst pipe is created");
    if (pipe.read == nullptr || pipe.write == nullptr) return;

    constexpr int kFrameCount = 200;
    std::vector<std::vector<uint8_t>> payloads(kFrameCount);
    std::thread writer([&] {
        for (int i = 0; i < kFrameCount; ++i) {
            payloads[i].assign(static_cast<size_t>(1 + (i % 64)), static_cast<uint8_t>(i & 0xFF));
            const bool ok = ffipc::WriteFrame(pipe.write,
                static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch),
                payloads[i].data(), static_cast<uint32_t>(payloads[i].size()));
            Check(ok, "burst frame is written");
            if (!ok) break;
        }
    });

    int receivedCount = 0;
    for (int i = 0; i < kFrameCount; ++i) {
        const auto received = ffipc::ReadFrame(pipe.read);
        if (!received.has_value()) break;
        Check(received->payload == payloads[i], "burst frame arrives in order with intact payload");
        ++receivedCount;
    }
    writer.join();
    Check(receivedCount == kFrameCount, "every burst frame is received exactly once");
}

} // namespace

int main() {
    TestMaximumValidPayloadRoundTrips();
    TestOversizedWriteRejectedBeforeIo();
    TestMalformedHeadersRejectedWithoutPayloadRead();
    TestRealNamedPipeHandshakeRoundTrips();
    TestOverlappedReadTimesOut();
    TestPartialHeaderTimesOut();
    TestPeerDeathDuringFrameReturns();
    TestOverlappedWriteTimesOutWhenPeerDoesNotRead();
    TestPartialFrameReadTimesOutWithoutAHelperThread();
    TestSynchronousReadReassemblesAcrossChunkedWrites();
    TestSynchronousReadReturnsOnBrokenPipe();
    TestBackToBackFramesAreDelimited();
    TestManyFramesRoundTripDeterministically();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
