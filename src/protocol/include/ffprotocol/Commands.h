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

    // index-storage-and-scanning: real scan/journal batch streaming.
    // Pushed asynchronously by the service on the same Ctrl connection
    // that issued StartVolumeScan/OpenUsnJournal -- interleaved with
    // Heartbeat, never on a separate connection, so the existing
    // connection-scoped VolumeId ownership rules (ConnectionRegistry)
    // keep applying unchanged (design.md non-goal: this change does not
    // reopen the transport design).
    ScanBatch = 14,       // service -> engine, zero or more per active scan
    ScanComplete = 15,    // service -> engine, terminal for one StartVolumeScan
    JournalOpened = 16,   // service -> engine, immediate reply to OpenUsnJournal (carries JournalId)
    UsnBatch = 17,        // service -> engine, streamed for the life of an open journal
    JournalResumeInvalid = 18, // service -> engine: the supplied ResumeUsn is no longer valid (D6/task 7.6)
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

// Upper bound on an opaque resume cursor's wire length (task 4.5, D8).
// Cursors this protocol actually produces are a handful of bytes (an MFT
// enumeration position); this ceiling exists only to bound the pre-parse
// allocation, the same discipline as every other length-prefixed field
// (spec "Frame and Input Validation").
constexpr uint16_t kMaxScanCursorLengthBytes = 256;

// Additive extension over the closed command surface (design.md D8): a
// fixed header followed by `resumeCursorLengthBytes` opaque bytes.
// resumeCursorLengthBytes == 0 requests a full scan from the beginning,
// exactly as StartVolumeScan behaved before this field existed.
struct StartVolumeScanRequestHeader {
    VolumeId volumeId;
    uint16_t resumeCursorLengthBytes;
};

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

// service -> engine, asynchronously streamed while a scan is active
// (ScanBatch). Layout on the wire: this fixed header, then
// resumeCursorLengthBytes opaque cursor bytes (reflecting progress AFTER
// this batch, for persisting per task 6.4), then the MFT record batch
// itself in the same layout ParseMftBatch/SerializeMftBatch already
// define (Records.h) -- placed last since its size is exactly implied by
// recordCount rather than being separately length-prefixed.
struct ScanBatchHeader {
    VolumeId volumeId;
    uint32_t recordCount;
    uint16_t resumeCursorLengthBytes;
};

struct ScanCompletePayload {
    VolumeId volumeId;
};

// service -> engine, immediate reply to OpenUsnJournal (spec "USN Journal
// Identity Reported for Resume Validation").
struct JournalOpenedPayload {
    VolumeId volumeId;
    uint64_t journalId;
};

// service -> engine, asynchronously streamed for the life of an open
// journal. The USN delta batch (Records.h layout) follows this header.
struct UsnBatchHeader {
    VolumeId volumeId;
    uint32_t recordCount;
    uint64_t resumeUsnAfterBatch; // persist as the volume's ResumeUsn (D8) once this batch is committed
};

// service -> engine: sent instead of further UsnBatch frames when the
// caller-supplied ResumeUsn (or the journal's position after a long-open
// stream wraps mid-read) falls outside the journal's currently retained
// range. The caller falls back to a reconciliation sweep for this volume
// rather than resuming (D6, task 7.6).
struct JournalResumeInvalidPayload {
    VolumeId volumeId;
};

struct NotYetImplementedPayload {
    uint16_t requestMessageType;
};

// Handshake/Heartbeat requests carry no payload beyond the frame header,
// aside from HandshakeRequest above.

#pragma pack(pop)

} // namespace ffprotocol
