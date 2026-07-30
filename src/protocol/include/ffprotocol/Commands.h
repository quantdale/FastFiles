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

struct StartVolumeScanRequest {
    VolumeId volumeId;
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

struct NotYetImplementedPayload {
    uint16_t requestMessageType;
};

// Handshake/Heartbeat requests carry no payload beyond the frame header,
// aside from HandshakeRequest above.

#pragma pack(pop)

} // namespace ffprotocol
