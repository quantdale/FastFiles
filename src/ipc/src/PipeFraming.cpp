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

DWORD WaitForOperation(HANDLE operationEvent, HANDLE shutdownEvent, DWORD timeoutMs) noexcept {
    if (shutdownEvent == nullptr || shutdownEvent == INVALID_HANDLE_VALUE) {
        return WaitForSingleObject(operationEvent, timeoutMs);
    }
    HANDLE handles[2] = {operationEvent, shutdownEvent};
    return WaitForMultipleObjects(2, handles, FALSE, timeoutMs);
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

bool ReadExactOverlappedInternal(
    HANDLE pipeHandle, void* buffer, uint32_t size, HANDLE shutdownEvent,
    const std::chrono::steady_clock::time_point deadline, FrameReadStatus* outStatus) noexcept {
    ReapQuarantinedOperations();
    uint8_t* cursor = static_cast<uint8_t*>(buffer);
    uint32_t remaining = size;
    const uint32_t initialSize = size;
    while (remaining > 0) {
        std::unique_ptr<OverlappedOperation> operation;
        try {
            operation = std::make_unique<OverlappedOperation>();
        } catch (...) {
            if (outStatus) *outStatus = FrameReadStatus::ResourceFailure;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
        operation->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (operation->event == nullptr) {
            if (outStatus) *outStatus = FrameReadStatus::ResourceFailure;
            return false;
        }
        operation->overlapped.hEvent = operation->event;
        try {
            operation->buffer.resize(remaining);
        } catch (...) {
            if (outStatus) *outStatus = FrameReadStatus::ResourceFailure;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }

        DWORD bytesRead = 0;
        const BOOL completed = ReadFile(pipeHandle, operation->buffer.data(), remaining, &bytesRead,
                                        &operation->overlapped);
        if (!completed) {
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                if (outStatus) {
                    *outStatus = (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
                        ? FrameReadStatus::Disconnected
                        : FrameReadStatus::IoError;
                }
                return false;
            }

            const DWORD waitResult = WaitForOperation(operation->event, shutdownEvent, WaitMilliseconds(deadline));
            if (waitResult == WAIT_OBJECT_0) {
                if (!GetOverlappedResult(pipeHandle, &operation->overlapped, &bytesRead, FALSE)) {
                    const DWORD resultError = GetLastError();
                    if (outStatus) {
                        if (resultError == ERROR_OPERATION_ABORTED) *outStatus = FrameReadStatus::Cancelled;
                        else if (resultError == ERROR_BROKEN_PIPE) *outStatus = FrameReadStatus::Disconnected;
                        else *outStatus = FrameReadStatus::IoError;
                    }
                    return false;
                }
            } else if (waitResult == WAIT_OBJECT_0 + 1) {
                CollectCancelledOperation(pipeHandle, std::move(operation));
                if (outStatus) *outStatus = FrameReadStatus::Cancelled;
                SetLastError(ERROR_OPERATION_ABORTED);
                return false;
            } else if (waitResult == WAIT_TIMEOUT) {
                CollectCancelledOperation(pipeHandle, std::move(operation));
                // Partial consumption before timeout is a framing desync
                // hazard. Treat it as a connection-level failure rather
                // than a retriable idle expiry so callers do not re-parse
                // half a header as a new frame.
                const bool hadPartialBytes = remaining != initialSize;
                // Also true if we already consumed some bytes in a prior
                // iteration of this ReadExact (remaining != initialSize) or
                // the caller will track header vs payload separately.
                if (outStatus) {
                    *outStatus = hadPartialBytes ? FrameReadStatus::Disconnected : FrameReadStatus::Timeout;
                }
                SetLastError(hadPartialBytes ? ERROR_OPERATION_ABORTED : ERROR_TIMEOUT);
                return false;
            } else {
                CollectCancelledOperation(pipeHandle, std::move(operation));
                if (outStatus) *outStatus = FrameReadStatus::IoError;
                SetLastError(ERROR_IO_DEVICE);
                return false;
            }
        }
        if (bytesRead == 0) {
            if (outStatus) *outStatus = FrameReadStatus::Disconnected;
            SetLastError(ERROR_BROKEN_PIPE);
            return false;
        }
        std::memcpy(cursor, operation->buffer.data(), bytesRead);
        cursor += bytesRead;
        remaining -= bytesRead;
        CloseOperationEvent(*operation);
    }
    if (outStatus) *outStatus = FrameReadStatus::Success;
    return true;
}

bool ReadExactOverlapped(
    HANDLE pipeHandle, void* buffer, uint32_t size,
    const std::chrono::steady_clock::time_point deadline) noexcept {
    return ReadExactOverlappedInternal(pipeHandle, buffer, size, nullptr, deadline, nullptr);
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

FrameReadStatus ReadFrameOverlappedDetailed(
    HANDLE pipeHandle, HANDLE shutdownEvent,
    const std::chrono::steady_clock::time_point deadline, ReceivedFrame* outFrame) noexcept {
    if (outFrame == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FrameReadStatus::ResourceFailure;
    }
    FrameReadStatus headerStatus = FrameReadStatus::Success;
    ffprotocol::FrameHeader header{};
    if (!ReadExactOverlappedInternal(pipeHandle, &header, sizeof(header), shutdownEvent, deadline, &headerStatus)) {
        if (headerStatus == FrameReadStatus::Timeout) SetLastError(ERROR_TIMEOUT);
        else if (headerStatus == FrameReadStatus::Cancelled) SetLastError(ERROR_OPERATION_ABORTED);
        else if (headerStatus == FrameReadStatus::Disconnected) {
            const DWORD last = GetLastError();
            if (last == ERROR_TIMEOUT) SetLastError(ERROR_OPERATION_ABORTED);
            if (GetLastError() == 0 || GetLastError() == ERROR_SUCCESS) SetLastError(ERROR_BROKEN_PIPE);
        }
        return headerStatus;
    }
    if (!ffprotocol::IsFrameLengthValid(header.totalLength) || header.structVersion != ffprotocol::kCurrentStructVersion) {
        SetLastError(ERROR_INVALID_DATA);
        return FrameReadStatus::InvalidFrame;
    }
    ReceivedFrame frame;
    frame.header = header;
    const uint32_t payloadSize = header.totalLength - static_cast<uint32_t>(sizeof(header));
    try {
        frame.payload.resize(payloadSize);
    } catch (...) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FrameReadStatus::ResourceFailure;
    }
    if (payloadSize > 0) {
        FrameReadStatus payloadStatus = FrameReadStatus::Success;
        if (!ReadExactOverlappedInternal(pipeHandle, frame.payload.data(), payloadSize, shutdownEvent, deadline, &payloadStatus)) {
            if (payloadStatus == FrameReadStatus::Timeout) {
                // Payload timeout after header already consumed => framing
                // is lost. Treat as disconnect so caller cannot retry as if
                // at a fresh frame boundary.
                SetLastError(ERROR_OPERATION_ABORTED);
                return FrameReadStatus::Disconnected;
            }
            if (payloadStatus == FrameReadStatus::Cancelled) SetLastError(ERROR_OPERATION_ABORTED);
            return payloadStatus;
        }
    }
    *outFrame = std::move(frame);
    SetLastError(ERROR_SUCCESS);
    return FrameReadStatus::Success;
}

std::optional<ReceivedFrame> ReadFrameOverlapped(
    HANDLE pipeHandle, HANDLE shutdownEvent,
    const std::chrono::steady_clock::time_point deadline) noexcept {
    ReceivedFrame frame;
    const FrameReadStatus status = ReadFrameOverlappedDetailed(pipeHandle, shutdownEvent, deadline, &frame);
    if (status == FrameReadStatus::Success) return frame;
    return std::nullopt;
}

std::optional<ReceivedFrame> ReadFrameOverlapped(
    HANDLE pipeHandle, const std::chrono::steady_clock::time_point deadline) noexcept {
    return ReadFrameOverlapped(pipeHandle, nullptr, deadline);
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
