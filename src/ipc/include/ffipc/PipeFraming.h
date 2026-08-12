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

// Reads one frame from a FILE_FLAG_OVERLAPPED pipe. The read is issued with
// ReadFile and completed with GetOverlappedResult; the absolute deadline
// bounds a frame header or payload that never arrives.
std::optional<ReceivedFrame> ReadFrameOverlapped(
    HANDLE pipeHandle, std::chrono::steady_clock::time_point deadline) noexcept;

// Bounded synchronous fallback for test and diagnostic handles that cannot
// be opened with FILE_FLAG_OVERLAPPED. A reader thread is cancelled at the
// absolute deadline so a synchronous ReadFile cannot wedge the test process.
std::optional<ReceivedFrame> ReadFrameWithTimeout(
    HANDLE pipeHandle, std::chrono::steady_clock::time_point deadline) noexcept;

// Writes a complete frame (header + payload) to a synchronous pipe handle.
// payloadSize must match what `messageType` implies; this function does
// not validate payload shape, only that the resulting frame does not
// exceed ffprotocol::kMaxFrameSize.
bool WriteFrame(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize) noexcept;

// Convenience overload for messages with no payload.
bool WriteFrame(HANDLE pipeHandle, uint16_t messageType) noexcept;

// Overlapped-pipe counterpart used by the privileged service. The complete
// frame is serialized under the caller's write mutex and each WriteFile is
// completed with GetOverlappedResult so a CancelIoEx from the write deadline
// watchdog can unwind a stalled client.
bool WriteFrameOverlapped(HANDLE pipeHandle, uint16_t messageType, const void* payload, uint32_t payloadSize) noexcept;

bool WriteFrameOverlapped(HANDLE pipeHandle, uint16_t messageType) noexcept;

} // namespace ffipc
