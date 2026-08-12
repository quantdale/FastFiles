#include "ffipc/PipeFraming.h"

#include <algorithm>
#include <thread>

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

bool ReadExactOverlapped(
    HANDLE pipeHandle, void* buffer, uint32_t size,
    const std::chrono::steady_clock::time_point deadline) noexcept {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) return false;

    uint8_t* cursor = static_cast<uint8_t*>(buffer);
    uint32_t remaining = size;
    while (remaining > 0) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        ResetEvent(event);

        DWORD bytesRead = 0;
        const BOOL completed = ReadFile(pipeHandle, cursor, remaining, &bytesRead, &overlapped);
        if (!completed) {
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                CloseHandle(event);
                return false;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                CancelIoEx(pipeHandle, &overlapped);
                DWORD ignored = 0;
                GetOverlappedResult(pipeHandle, &overlapped, &ignored, TRUE);
                CloseHandle(event);
                return false;
            }
            const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            const DWORD waitMs = static_cast<DWORD>((std::min)(remainingMs, static_cast<long long>(INFINITE - 1)));
            const DWORD waitResult = WaitForSingleObject(event, waitMs == 0 ? 1 : waitMs);
            if (waitResult != WAIT_OBJECT_0) {
                CancelIoEx(pipeHandle, &overlapped);
                DWORD ignored = 0;
                GetOverlappedResult(pipeHandle, &overlapped, &ignored, TRUE);
                CloseHandle(event);
                return false;
            }
            if (!GetOverlappedResult(pipeHandle, &overlapped, &bytesRead, FALSE)) {
                CloseHandle(event);
                return false;
            }
        }
        if (bytesRead == 0) {
            CloseHandle(event);
            return false;
        }
        cursor += bytesRead;
        remaining -= bytesRead;
    }

    CloseHandle(event);
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

bool WriteExactOverlapped(HANDLE pipeHandle, const void* buffer, uint32_t size) noexcept {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) return false;

    const uint8_t* cursor = static_cast<const uint8_t*>(buffer);
    uint32_t remaining = size;
    while (remaining > 0) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        ResetEvent(event);

        DWORD bytesWritten = 0;
        const BOOL completed = WriteFile(pipeHandle, cursor, remaining, &bytesWritten, &overlapped);
        if (!completed) {
            if (GetLastError() != ERROR_IO_PENDING
                || !GetOverlappedResult(pipeHandle, &overlapped, &bytesWritten, TRUE)) {
                CloseHandle(event);
                return false;
            }
        }
        if (bytesWritten == 0) {
            CloseHandle(event);
            return false;
        }
        cursor += bytesWritten;
        remaining -= bytesWritten;
    }

    CloseHandle(event);
    return true;
}

ffprotocol::FrameHeader MakeHeader(uint16_t messageType, uint32_t payloadSize) noexcept {
    ffprotocol::FrameHeader header{};
    header.totalLength = static_cast<uint32_t>(sizeof(ffprotocol::FrameHeader) + payloadSize);
    header.structVersion = ffprotocol::kCurrentStructVersion;
    header.messageType = messageType;
    return header;
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

std::optional<ReceivedFrame> ReadFrameOverlapped(
    HANDLE pipeHandle, const std::chrono::steady_clock::time_point deadline) noexcept {
    ffprotocol::FrameHeader header{};
    if (!ReadExactOverlapped(pipeHandle, &header, sizeof(header), deadline)) {
        return std::nullopt;
    }
    if (!ffprotocol::IsFrameLengthValid(header.totalLength) || header.structVersion != ffprotocol::kCurrentStructVersion) {
        return std::nullopt;
    }

    ReceivedFrame frame;
    frame.header = header;
    const uint32_t payloadSize = header.totalLength - static_cast<uint32_t>(sizeof(header));
    frame.payload.resize(payloadSize);
    if (payloadSize > 0 && !ReadExactOverlapped(pipeHandle, frame.payload.data(), payloadSize, deadline)) {
        return std::nullopt;
    }
    return frame;
}

std::optional<ReceivedFrame> ReadFrameWithTimeout(
    HANDLE pipeHandle, const std::chrono::steady_clock::time_point deadline) noexcept {
    std::optional<ReceivedFrame> result;
    HANDLE doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (doneEvent == nullptr) return std::nullopt;

    std::thread reader([&] {
        result = ReadFrame(pipeHandle);
        SetEvent(doneEvent);
    });
    const auto now = std::chrono::steady_clock::now();
    const auto remainingMs = now >= deadline
        ? 0LL
        : std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    const DWORD waitMs = static_cast<DWORD>((std::min)(remainingMs, static_cast<long long>(INFINITE - 1)));
    const DWORD waitResult = WaitForSingleObject(doneEvent, waitMs);
    if (waitResult != WAIT_OBJECT_0) {
        CancelSynchronousIo(reader.native_handle());
    }
    reader.join();
    CloseHandle(doneEvent);
    return waitResult == WAIT_OBJECT_0 ? result : std::nullopt;
}

bool WriteFrame(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize) noexcept {
    const uint64_t totalLength64 = sizeof(ffprotocol::FrameHeader) + static_cast<uint64_t>(payloadSize);
    if (totalLength64 > ffprotocol::kMaxFrameSize) {
        return false;
    }

    const ffprotocol::FrameHeader header = MakeHeader(messageType, payloadSize);

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

bool WriteFrameOverlapped(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize) noexcept {
    const uint64_t totalLength64 = sizeof(ffprotocol::FrameHeader) + static_cast<uint64_t>(payloadSize);
    if (totalLength64 > ffprotocol::kMaxFrameSize) {
        return false;
    }

    const ffprotocol::FrameHeader header = MakeHeader(messageType, payloadSize);
    if (!WriteExactOverlapped(pipeHandle, &header, sizeof(header))) {
        return false;
    }
    return payloadSize == 0 || WriteExactOverlapped(pipeHandle, payload, payloadSize);
}

bool WriteFrameOverlapped(HANDLE pipeHandle, uint16_t messageType) noexcept {
    return WriteFrameOverlapped(pipeHandle, messageType, nullptr, 0);
}

} // namespace ffipc
