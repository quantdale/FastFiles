#pragma once
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "ffprotocol/Dispatch.h"
#include "ffprotocol/Frame.h"

namespace ffipc {

// A fully-read frame: header.totalLength/structVersion have already passed
// the generic, protocol-agnostic checks (IsFrameLengthValid, current
// StructVersion), and payload holds exactly (header.totalLength -
// sizeof(FrameHeader)) bytes -- never more, never less (spec "Frame and
// Input Validation"). header.messageType is NOT yet checked against any
// specific closed command set -- there are two different closed sets on
// this codebase's two IPC seams (engine<->service: ffprotocol::MessageType
// / Dispatch.h; engine<->UI: ffprotocol::UiMessageType / UiProtocol.h), so
// callers validate messageType against whichever set applies to their pipe
// via ToMessageType/ToUiMessageType before dispatching on it.
struct ReceivedFrame {
    ffprotocol::FrameHeader header;
    std::vector<uint8_t> payload;
};

// Reads one frame from a synchronous (not overlapped) pipe handle. Rejects
// and returns std::nullopt -- WITHOUT allocating a payload buffer -- for
// an oversized or wrong-StructVersion header, exactly mirroring the
// unit-tested validation path in IsFrameLengthValid (never trusts
// header.totalLength before that check). Also returns std::nullopt on a
// pipe I/O error or clean disconnect.
std::optional<ReceivedFrame> ReadFrame(HANDLE pipeHandle) noexcept;

// Explicit per-frame result classification. Idle expiry (deadline hit before
// any byte of the frame was consumed) is distinguishable from provider
// cancellation (Stop()/CancelIoEx), disconnect, malformed framing, and
// generic I/O failure without relying on stale thread-local GetLastError().
enum class FrameReadStatus : uint8_t {
    Success = 0,
    Timeout,
    Cancelled,
    Disconnected,
    InvalidFrame,
    IoError,
    ResourceFailure,
};

// Reads one frame from a FILE_FLAG_OVERLAPPED pipe. The read is issued with
// ReadFile and completed from an explicit event; the absolute deadline bounds
// a frame header or payload that never arrives. Cancellation is collected for
// a bounded grace period. If a provider does not complete a cancelled I/O in
// that period, its heap-owned OVERLAPPED state is quarantined until the event
// is signaled, so returning on the deadline never leaves a stack buffer or
// event handle referenced by the kernel.
std::optional<ReceivedFrame> ReadFrameOverlapped(
    HANDLE pipeHandle, std::chrono::steady_clock::time_point deadline) noexcept;

// Shutdown-aware variant. Waits on both the overlapped completion event and
// `shutdownEvent` (manual-reset, signaled by Stop()). An idle-but-connected
// peer never needs a periodic deadline to make Stop() join promptly; the
// reader blocks on INFINITE (deadline==max) and is woken by the shutdown
// event via WaitForMultipleObjects. This preserves byte-stream framing
// across partial-header/payload timeouts because no spurious per-slice retry
// re-parses a half-consumed frame.
std::optional<ReceivedFrame> ReadFrameOverlapped(
    HANDLE pipeHandle, HANDLE shutdownEvent,
    std::chrono::steady_clock::time_point deadline) noexcept;

// Structured result wrapper. Sets LastError consistently (TIMEOUT / ABORTED /
// BROKEN_PIPE / INVALID_DATA / etc.) for backwards-compat callers that
// still inspect GetLastError(), but callers should prefer `status`.
FrameReadStatus ReadFrameOverlappedDetailed(
    HANDLE pipeHandle, HANDLE shutdownEvent,
    std::chrono::steady_clock::time_point deadline, ReceivedFrame* outFrame) noexcept;

// Writes a complete frame (header + payload) to a synchronous pipe handle.
// payloadSize must match what `messageType` implies; this function does
// not validate payload shape, only that the resulting frame does not
// exceed ffprotocol::kMaxFrameSize.
bool WriteFrame(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize) noexcept;

// Convenience overload for messages with no payload.
bool WriteFrame(HANDLE pipeHandle, uint16_t messageType) noexcept;

// Overlapped-pipe counterpart used by both product seams. The complete frame
// is serialized under the caller's write mutex and each WriteFile is
// completed from an explicit event. The deadline overload is used by bounded
// request/handshake paths; the legacy overload intentionally has no deadline
// and is retained for the service's existing write-deadline watchdog.
bool WriteFrameOverlapped(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize) noexcept;

bool WriteFrameOverlapped(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize,
                          std::chrono::steady_clock::time_point deadline) noexcept;

bool WriteFrameOverlapped(HANDLE pipeHandle, uint16_t messageType) noexcept;

#if defined(FF_IPC_TESTING)
namespace testing {
// Arms the next overlapped frame write in this thread to emit exactly this
// many real named-pipe bytes, then fail its following write operation. This
// API exists only in the focused IPC/scanner fixture builds.
void FailNextOverlappedFrameWriteAfterBytes(uint32_t byteCount) noexcept;
} // namespace testing
#endif

} // namespace ffipc
