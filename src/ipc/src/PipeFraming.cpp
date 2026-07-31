#include "ffipc/PipeFraming.h"

namespace ffipc {

namespace {

// Named pipes in byte-stream mode can deliver a ReadFile completion
// short of the requested length; every read here must loop until exactly
// `size` bytes are collected or the pipe errors/disconnects.
bool ReadExact(HANDLE pipeHandle, void* buffer, uint32_t size) noexcept {
    uint8_t* cursor = static_cast<uint8_t*>(buffer);
    uint32_t remaining = size;
    while (remaining > 0) {
        DWORD bytesRead = 0;
        if (!ReadFile(pipeHandle, cursor, remaining, &bytesRead, nullptr) || bytesRead == 0) {
            return false;
        }
        cursor += bytesRead;
        remaining -= bytesRead;
    }
    return true;
}

bool WriteExact(HANDLE pipeHandle, const void* buffer, uint32_t size) noexcept {
    const uint8_t* cursor = static_cast<const uint8_t*>(buffer);
    uint32_t remaining = size;
    while (remaining > 0) {
        DWORD bytesWritten = 0;
        if (!WriteFile(pipeHandle, cursor, remaining, &bytesWritten, nullptr) || bytesWritten == 0) {
            return false;
        }
        cursor += bytesWritten;
        remaining -= bytesWritten;
    }
    return true;
}

} // namespace

std::optional<ReceivedFrame> ReadFrame(HANDLE pipeHandle) noexcept {
    ffprotocol::FrameHeader header{};
    if (!ReadExact(pipeHandle, &header, sizeof(header))) {
        return std::nullopt;
    }

    // Must be checked -- including in u64 arithmetic -- before any payload
    // buffer is sized from header data (spec "Oversized frame is
    // rejected"). structVersion is checked here too since it's the same
    // constant across both IPC seams; messageType validity is
    // seam-specific and left to the caller.
    if (!ffprotocol::IsFrameLengthValid(header.totalLength) || header.structVersion != ffprotocol::kCurrentStructVersion) {
        return std::nullopt;
    }

    ReceivedFrame frame;
    frame.header = header;
    const uint32_t payloadSize = header.totalLength - static_cast<uint32_t>(sizeof(header));
    frame.payload.resize(payloadSize);
    if (payloadSize > 0 && !ReadExact(pipeHandle, frame.payload.data(), payloadSize)) {
        return std::nullopt;
    }

    return frame;
}

bool WriteFrame(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize) noexcept {
    const uint64_t totalLength64 = sizeof(ffprotocol::FrameHeader) + static_cast<uint64_t>(payloadSize);
    if (totalLength64 > ffprotocol::kMaxFrameSize) {
        return false;
    }

    ffprotocol::FrameHeader header{};
    header.totalLength = static_cast<uint32_t>(totalLength64);
    header.structVersion = ffprotocol::kCurrentStructVersion;
    header.messageType = messageType;

    if (!WriteExact(pipeHandle, &header, sizeof(header))) {
        return false;
    }
    if (payloadSize > 0 && !WriteExact(pipeHandle, payload, payloadSize)) {
        return false;
    }
    return true;
}

bool WriteFrame(HANDLE pipeHandle, uint16_t messageType) noexcept {
    return WriteFrame(pipeHandle, messageType, nullptr, 0);
}

} // namespace ffipc
