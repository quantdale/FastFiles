#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <utility>
#include <vector>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Commands.h"
#include "ffprotocol/Frame.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    } else {
        std::printf("ok: %s\n", description);
    }
}

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

} // namespace

int main() {
    TestMaximumValidPayloadRoundTrips();
    TestOversizedWriteRejectedBeforeIo();
    TestMalformedHeadersRejectedWithoutPayloadRead();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
