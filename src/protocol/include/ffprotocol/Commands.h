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
    Heartbeat = 12,
    HeartbeatAck = 13,

    // index-storage-and-scanning: additive streaming responses for
    // StartVolumeScan/OpenUsnJournal
    // (design.md "Migration Plan" -- greenfield, no deployed clients to
    // stay wire-compatible with yet, so these are plain new values rather
    // than a versioned/negotiated extension). Streamed asynchronously by
    // the service on the same Ctrl connection the request arrived on,
    // interleaved with Heartbeat/Stop/Close traffic from the client.
    ScanRecordBatch = 14,  // service -> client, zero or more per scan
    ScanComplete = 15,     // service -> client, exactly once per scan that runs to
                           // completion; a scan that cannot run to completion sends
                           // ScanFailed instead (index-durability-false-success-closure)
    UsnJournalOpened = 16, // service -> client, once, acks OpenUsnJournal with the current JournalId
    JournalRecordBatch = 17, // service -> client, zero or more per open journal
    JournalResumeInvalid = 18, // service -> engine: the supplied ResumeUsn is no longer valid (D6/task 7.6)
    JournalRecordBatchV2 = 19, // service -> engine, explicit USN field-presence format
    // allocated-size-storage-accuracy D3: additive batch message types
    // carrying allocated (on-disk) size. Sent only to peers that negotiated
    // protocol {2,1}+ (ProtocolSupportsAllocatedSize); a legacy peer receives
    // the V1/V2 messages above with byte-identical records. Distinct message
    // types (not a re-used type) so a legacy parser can never reinterpret a
    // new payload as the old fixed layout -- the same pattern as
    // JournalRecordBatchV2.
    ScanRecordBatchV2 = 20,    // service -> client, MFT record layout with allocatedSizeBytes
    JournalRecordBatchV3 = 21, // service -> engine, USN V3 layout with allocatedSizeBytes presence bit
    // index-durability-false-success-closure D1, corrected by
    // legacy-protocol-correctness-and-raw-volume-cancellation-closure D3:
    // additive terminal failure message. Sent by the service *instead of*
    // ScanComplete when a scan cannot run to completion (setup denial,
    // volume-data failure, mid-scan non-EOF I/O fault) and the negotiated peer
    // can parse it ({2,2}+). A peer below {2,2} has no failure terminal at all:
    // the service writes nothing for a failed scan and ends that connection
    // instead of emitting a false-success ScanComplete. Distinct type (not a
    // re-used/extended ScanComplete) so a legacy exact-length parser can never
    // misread it, and completion and failure stay mutually exclusive signals
    // the engine can trust independently.
    ScanFailed = 22, // service -> engine, exactly once per scan that failed to complete
    // scan-generation-integrity-and-residual-certification-closure 3.4:
    // additive ordered barrier for a replacement scan. A {2,3}+ service
    // writes this after joining a superseded worker and before starting its
    // replacement, so the engine can separate old output from the
    // replacement without resizing either terminal message.
    ScanAttemptStarted = 23, // service -> engine, exact-length VolumeId payload
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
// performs a full scan"). `inline` because a header-declared constant is not
// used by every including translation unit (it is consumed by
// protocol/src/Commands.cpp's validators and by the engine's request builder).
inline constexpr uint16_t kMaxScanCursorLengthBytes = 256;

// StartVolumeScan flags. A reconciliation sweep is deliberately run in
// Windows background mode so its CPU and I/O work yield to foreground
// browsing and initial indexing.
constexpr uint16_t kStartVolumeScanLowPriority = 0x0001;
inline constexpr uint16_t kKnownStartVolumeScanFlags = kStartVolumeScanLowPriority;

// Protocol v1 ended after the cursor length. Keep its exact packed layout
// so a v2 engine can still talk to a negotiated v1 service.
struct StartVolumeScanRequestV1 {
    VolumeId volumeId;
    uint16_t resumeCursorLengthBytes;
};

struct StartVolumeScanRequest {
    VolumeId volumeId;
    uint16_t resumeCursorLengthBytes;
    uint16_t flags;
};

bool IsScanCursorLengthValid(uint16_t lengthBytes) noexcept;
bool AreStartVolumeScanFlagsValid(uint16_t flags) noexcept;

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

// ScanRecordBatch payload: this fixed header, `resumeCursorLengthBytes`
// opaque cursor bytes (the scan's position as of just after this batch --
// tasks.md 6.4), then a serialized MFT record batch in the same wire
// format ParseMftBatch/SerializeMftBatch (Records.h) already define.
struct ScanRecordBatchHeader {
    VolumeId volumeId;
    uint32_t recordCount;
    uint16_t resumeCursorLengthBytes;
};

// allocated-size-storage-accuracy D3: ScanRecordBatchV2 payload --
// identical shape plus an explicit recordFormatVersion so a parser cannot
// mistake the V2 record stream (fixed layout + allocatedSizeBytes) for the
// V1 one. The records are serialized MftRecordV2 batches
// (SerializeMftBatchV2/ParseMftBatchV2).
struct ScanRecordBatchV2Header {
    VolumeId volumeId;
    uint32_t recordCount;
    uint16_t resumeCursorLengthBytes;
    uint16_t recordFormatVersion;
};

struct ScanCompletePayload {
    VolumeId volumeId;
};

// index-durability-false-success-closure D1: terminal failure carries the same
// volume identity as ScanComplete. Same size on purpose: the engine dispatches
// it with the identical exact-length check, so a truncated/extended frame is a
// protocol violation exactly as ScanComplete already is.
struct ScanFailedPayload {
    VolumeId volumeId;
};

// scan-generation-integrity-and-residual-certification-closure 3.4: this is
// deliberately a separate reply-only message, rather than an extension of
// ScanComplete or ScanFailed. Its placement in the serialized service stream
// is the authority barrier; the old terminal payloads stay byte-for-byte
// compatible for peers that do not negotiate this capability.
struct ScanAttemptStartedPayload {
    VolumeId volumeId;
};

struct UsnJournalOpenedPayload {
    VolumeId volumeId;
    uint64_t journalId;
    // The journal's current NextUsn as of the open (i.e. where a
    // start-fresh-from-now read would begin). Lets the caller recover
    // safely if its own persisted ResumeUsn turns out to be stale for
    // this JournalId (design.md D6): when UsnJournalReader.cpp reports
    // the resume position as out of the journal's retained range (see
    // JournalResumeInvalid below), reopen with resumeUsn = this value
    // rather than retrying the same invalid position.
    uint64_t currentUsn;
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

// V2 has a distinct message type and an explicit format version so a parser
// cannot accidentally interpret a V1 payload as a complete metadata record.
struct JournalRecordBatchV2Header {
    VolumeId volumeId;
    uint32_t recordCount;
    uint64_t latestUsn;
    uint16_t recordFormatVersion;
};

// allocated-size-storage-accuracy D3: JournalRecordBatchV3 payload --
// same shape as V2 with recordFormatVersion 3 selecting the UsnDeltaFixedV3
// record stream (V2 + allocatedSizeBytes under kUsnFieldAllocatedSizeBytes).
struct JournalRecordBatchV3Header {
    VolumeId volumeId;
    uint32_t recordCount;
    uint64_t latestUsn;
    uint16_t recordFormatVersion;
};

// service -> engine: sent instead of further JournalRecordBatch frames
// when the caller-supplied ResumeUsn (or the journal's position after a
// long-open stream wraps mid-read) falls outside the journal's currently
// retained range. The caller falls back to a reconciliation sweep for
// this volume rather than resuming (D6, task 7.6).
struct JournalResumeInvalidPayload {
    VolumeId volumeId;
};

// Handshake/Heartbeat requests carry no payload beyond the frame header,
// aside from HandshakeRequest above.

#pragma pack(pop)

} // namespace ffprotocol
