#pragma once
#include <cstdint>
#include <optional>
#include "ffprotocol/Version.h"

namespace ffprotocol {

// The closed engine<->service command surface (design.md D4, spec
// "Closed Command Protocol"). No message type accepts an arbitrary
// client-supplied path, handle, or device string.
enum class MessageType : uint16_t {
    Handshake = 1,
    HandshakeAck = 2,
    HandshakeReject = 3,
    IncompatibleVersion = 4,
    EnumerateVolumes = 5,
    VolumeList = 6,
    StartVolumeScan = 7,
    StopVolumeScan = 8,
    OpenUsnJournal = 9,
    CloseUsnJournal = 10,
    NotYetImplemented = 11,
    Heartbeat = 12,
    HeartbeatAck = 13,

    // index-storage-and-scanning: additive extensions replacing the
    // NotYetImplemented stub replies for StartVolumeScan/OpenUsnJournal
    // (design.md "Migration Plan" -- greenfield, no deployed clients to
    // stay wire-compatible with yet, so these are plain new values rather
    // than a versioned/negotiated extension). Streamed asynchronously by
    // the service on the same Ctrl connection the request arrived on,
    // interleaved with Heartbeat/Stop/Close traffic from the client.
    ScanRecordBatch = 14,  // service -> client, zero or more per scan
    ScanComplete = 15,     // service -> client, exactly once per scan that runs to completion
    UsnJournalOpened = 16, // service -> client, once, acks OpenUsnJournal with the current JournalId
    JournalRecordBatch = 17, // service -> client, zero or more per open journal
};

// Bounds-checked conversion: never index a jump table with an untrusted
// integer directly (spec "Frame and Input Validation" / task 2.5).
std::optional<MessageType> ToMessageType(uint16_t raw) noexcept;

// Opaque, service-assigned, connection-scoped (spec "Opaque,
// Connection-Scoped Handles"). Never constructed from client input.
struct VolumeId {
    uint32_t value;
};

struct JournalId {
    uint32_t value;
};

constexpr bool operator==(VolumeId a, VolumeId b) noexcept { return a.value == b.value; }
constexpr bool operator==(JournalId a, JournalId b) noexcept { return a.value == b.value; }

#pragma pack(push, 1)

struct HandshakeRequest {
    ProtocolVersion clientVersion;
};

struct HandshakeAckPayload {
    ProtocolVersion negotiatedVersion;
};

enum class HandshakeRejectReason : uint16_t {
    UnverifiedImagePath = 1,
    UnverifiedSignature = 2,
    NotAuthorizedGroup = 3,
};

struct HandshakeRejectPayload {
    HandshakeRejectReason reason;
};

struct IncompatibleVersionPayload {
    ProtocolVersion serverVersion;
};

// EnumerateVolumes request has no payload beyond the frame header.

struct VolumeInfo {
    VolumeId id;
    wchar_t driveLetter;
};

// Variable-length: a fixed VolumeListHeader followed by `count` VolumeInfo
// entries. Kept in Records.h alongside the other batch-validation helpers.
struct VolumeListHeader {
    uint32_t count;
};

// index-storage-and-scanning tasks.md 4.5/D8: additive optional
// resume-cursor argument. `resumeCursorLengthBytes` trailing opaque bytes
// (previously issued by this same service via ScanRecordBatchHeader,
// tasks.md 6.3) immediately follow this fixed struct on the wire; 0 means
// "no cursor supplied -- scan from the beginning" (spec "Omitted cursor
// performs a full scan").
constexpr uint16_t kMaxScanCursorLengthBytes = 256;

struct StartVolumeScanRequest {
    VolumeId volumeId;
    uint16_t resumeCursorLengthBytes;
};

bool IsScanCursorLengthValid(uint16_t lengthBytes) noexcept;

struct StopVolumeScanRequest {
    VolumeId volumeId;
};

struct OpenUsnJournalRequest {
    VolumeId volumeId;
    uint64_t resumeUsn;
};

struct CloseUsnJournalRequest {
    VolumeId volumeId;
};

struct NotYetImplementedPayload {
    uint16_t requestMessageType;
};

// ScanRecordBatch payload: this fixed header, `resumeCursorLengthBytes`
// opaque cursor bytes (the scan's position as of just after this batch --
// tasks.md 6.4), then a serialized MFT record batch in the same wire
// format ParseMftBatch/SerializeMftBatch (Records.h) already define.
struct ScanRecordBatchHeader {
    VolumeId volumeId;
    uint32_t recordCount;
    uint16_t resumeCursorLengthBytes;
};

struct ScanCompletePayload {
    VolumeId volumeId;
};

struct UsnJournalOpenedPayload {
    VolumeId volumeId;
    uint64_t journalId;
};

// JournalRecordBatch payload: this fixed header followed by a serialized
// USN delta batch (Records.h's ParseUsnDeltaBatch/SerializeUsnDeltaBatch
// wire format). `latestUsn` is the position to persist as this volume's
// new ResumeUsn once the batch has been durably applied.
struct JournalRecordBatchHeader {
    VolumeId volumeId;
    uint32_t recordCount;
    uint64_t latestUsn;
};

// Handshake/Heartbeat requests carry no payload beyond the frame header,
// aside from HandshakeRequest above.

#pragma pack(pop)

} // namespace ffprotocol
