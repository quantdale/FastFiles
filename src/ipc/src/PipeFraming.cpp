#include "ffipc/PipeFraming.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

namespace ffipc {

namespace {

constexpr std::chrono::milliseconds kCancellationCollectionGrace{250};
constexpr size_t kMaximumQuarantinedOperations = 32;

struct OverlappedOperation {
    OVERLAPPED overlapped{};
    HANDLE event = nullptr;
    std::vector<uint8_t> buffer;

    ~OverlappedOperation() {
        if (event != nullptr) {
            CloseHandle(event);
        }
    }
};

void CloseOperationEvent(OverlappedOperation& operation) noexcept {
    if (operation.event != nullptr) {
        CloseHandle(operation.event);
        operation.event = nullptr;
    }
}

// A provider that ignores CancelIoEx must not be allowed to retain a stack
// OVERLAPPED, caller buffer, or event after this function returns. Normally
// cancellation completes within the bounded grace period and this collection
// stays empty. The intentionally process-lifetime-owned quarantine is bounded;
// reaching its cap fails closed rather than allowing unbounded memory/handle
// growth under repeated hostile pipe stalls.
std::mutex& QuarantineMutex() {
    static auto* mutex = new std::mutex();
    return *mutex;
}

std::vector<std::unique_ptr<OverlappedOperation>>& Quarantine() {
    static auto* operations = new std::vector<std::unique_ptr<OverlappedOperation>>();
    return *operations;
}

void ReapQuarantinedOperations() noexcept {
    std::lock_guard<std::mutex> lock(QuarantineMutex());
    auto& operations = Quarantine();
    for (auto it = operations.begin(); it != operations.end();) {
        if (WaitForSingleObject((*it)->event, 0) == WAIT_OBJECT_0) {
            CloseOperationEvent(*(*it));
            it = operations.erase(it);
        } else {
            ++it;
        }
    }
}

// Returns false only when the bounded quarantine is full. In that case the
// operation is intentionally leaked for process lifetime below: freeing its
// buffer or event would be a use-after-free if the provider completes later.
bool QuarantineOperation(std::unique_ptr<OverlappedOperation>& operation) noexcept {
    std::lock_guard<std::mutex> lock(QuarantineMutex());
    auto& operations = Quarantine();
    if (operations.size() >= kMaximumQuarantinedOperations) {
        return false;
    }
    try {
        operations.push_back(std::move(operation));
        return true;
    } catch (...) {
        return false;
    }
}

DWORD WaitMilliseconds(const std::chrono::steady_clock::time_point deadline) noexcept {
    if (deadline == std::chrono::steady_clock::time_point::max()) {
        return INFINITE;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    return static_cast<DWORD>((std::min)(remaining, static_cast<long long>(INFINITE - 1)));
}

bool CollectCancelledOperation(HANDLE pipeHandle, std::unique_ptr<OverlappedOperation> operation) noexcept {
    CancelIoEx(pipeHandle, &operation->overlapped);
    const DWORD waitResult = WaitForSingleObject(
        operation->event, static_cast<DWORD>(kCancellationCollectionGrace.count()));
    if (waitResult == WAIT_OBJECT_0) {
        CloseOperationEvent(*operation);
        return false;
    }
    if (QuarantineOperation(operation)) {
        return false;
    }
    // Do not free state that the kernel may still reference. The caller must
    // abandon this pipe after the cap is reached; process teardown reclaims
    // the intentionally leaked event/buffer if the provider never completes.
    operation.release();
    return false;
}

// Named pipes in byte-stream mode can deliver a ReadFile completion short of
// the requested length; every read here must loop until exactly `size` bytes
// are collected or the pipe errors/disconnects.
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
    ReapQuarantinedOperations();
    uint8_t* cursor = static_cast<uint8_t*>(buffer);
    uint32_t remaining = size;
    while (remaining > 0) {
        std::unique_ptr<OverlappedOperation> operation;
        try {
            operation = std::make_unique<OverlappedOperation>();
        } catch (...) {
            return false;
        }
        operation->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (operation->event == nullptr) return false;
        operation->overlapped.hEvent = operation->event;
        try {
            operation->buffer.resize(remaining);
        } catch (...) {
            return false;
        }

        DWORD bytesRead = 0;
        const BOOL completed = ReadFile(pipeHandle, operation->buffer.data(), remaining, &bytesRead,
                                        &operation->overlapped);
        if (!completed) {
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                return false;
            }

            const DWORD waitResult = WaitForSingleObject(operation->event, WaitMilliseconds(deadline));
            if (waitResult != WAIT_OBJECT_0) {
                const bool timedOut = waitResult == WAIT_TIMEOUT;
                CollectCancelledOperation(pipeHandle, std::move(operation));
                if (timedOut) {
                    // A deadline expiry is not a disconnect: overlapped
                    // readers that re-arm a bounded wait distinguish the two
                    // via the last error so an idle-but-connected peer is
                    // never torn down as if the pipe had closed.
                    SetLastError(ERROR_TIMEOUT);
                }
                return false;
            }
            if (!GetOverlappedResult(pipeHandle, &operation->overlapped, &bytesRead, FALSE)) {
                return false;
            }
        }
        if (bytesRead == 0) {
            return false;
        }
        std::memcpy(cursor, operation->buffer.data(), bytesRead);
        cursor += bytesRead;
        remaining -= bytesRead;
        CloseOperationEvent(*operation);
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

bool WriteExactOverlapped(HANDLE pipeHandle, const void* buffer, uint32_t size,
                          const std::chrono::steady_clock::time_point deadline) noexcept {
    ReapQuarantinedOperations();
    const uint8_t* cursor = static_cast<const uint8_t*>(buffer);
    uint32_t remaining = size;
    while (remaining > 0) {
        std::unique_ptr<OverlappedOperation> operation;
        try {
            operation = std::make_unique<OverlappedOperation>();
        } catch (...) {
            return false;
        }
        operation->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (operation->event == nullptr) return false;
        operation->overlapped.hEvent = operation->event;
        try {
            operation->buffer.assign(cursor, cursor + remaining);
        } catch (...) {
            return false;
        }

        DWORD bytesWritten = 0;
        const BOOL completed = WriteFile(pipeHandle, operation->buffer.data(), remaining, &bytesWritten,
                                         &operation->overlapped);
        if (!completed) {
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                return false;
            }
            const DWORD waitResult = WaitForSingleObject(operation->event, WaitMilliseconds(deadline));
            if (waitResult != WAIT_OBJECT_0) {
                const bool timedOut = waitResult == WAIT_TIMEOUT;
                CollectCancelledOperation(pipeHandle, std::move(operation));
                if (timedOut) {
                    // Mirror the read path: a write deadline expiry is a
                    // bounded-operation timeout, not a generic I/O error.
                    SetLastError(ERROR_TIMEOUT);
                }
                return false;
            }
            if (!GetOverlappedResult(pipeHandle, &operation->overlapped, &bytesWritten, FALSE)) {
                return false;
            }
        }
        if (bytesWritten == 0) {
            return false;
        }
        cursor += bytesWritten;
        remaining -= bytesWritten;
        CloseOperationEvent(*operation);
    }
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
    // constant across both IPC seams; messageType validity is seam-specific
    // and left to the caller.
    if (!ffprotocol::IsFrameLengthValid(header.totalLength) || header.structVersion != ffprotocol::kCurrentStructVersion) {
        return std::nullopt;
    }

    ReceivedFrame frame;
    frame.header = header;
    const uint32_t payloadSize = header.totalLength - static_cast<uint32_t>(sizeof(header));
    try {
        frame.payload.resize(payloadSize);
    } catch (...) {
        return std::nullopt;
    }
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
    try {
        frame.payload.resize(payloadSize);
    } catch (...) {
        return std::nullopt;
    }
    if (payloadSize > 0 && !ReadExactOverlapped(pipeHandle, frame.payload.data(), payloadSize, deadline)) {
        return std::nullopt;
    }
    return frame;
}

bool WriteFrame(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize) noexcept {
    if (payloadSize > 0 && payload == nullptr) {
        return false;
    }
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

bool WriteFrameOverlapped(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize,
                          const std::chrono::steady_clock::time_point deadline) noexcept {
    const uint64_t totalLength64 = sizeof(ffprotocol::FrameHeader) + static_cast<uint64_t>(payloadSize);
    if (totalLength64 > ffprotocol::kMaxFrameSize) {
        return false;
    }
    if (payloadSize > 0 && payload == nullptr) {
        return false;
    }

    const ffprotocol::FrameHeader header = MakeHeader(messageType, payloadSize);
    std::vector<uint8_t> frame;
    try {
        frame.resize(sizeof(header) + payloadSize);
    } catch (...) {
        return false;
    }
    std::memcpy(frame.data(), &header, sizeof(header));
    if (payloadSize > 0) {
        std::memcpy(frame.data() + sizeof(header), payload, payloadSize);
    }
    return WriteExactOverlapped(pipeHandle, frame.data(), static_cast<uint32_t>(frame.size()), deadline);
}

bool WriteFrameOverlapped(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize) noexcept {
    return WriteFrameOverlapped(pipeHandle, messageType, payload, payloadSize,
                                std::chrono::steady_clock::time_point::max());
}

bool WriteFrameOverlapped(HANDLE pipeHandle, uint16_t messageType) noexcept {
    return WriteFrameOverlapped(pipeHandle, messageType, nullptr, 0);
}

} // namespace ffipc
