#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "ffprotocol/Frame.h"

namespace ffprotocol {

// The same-privilege, control-plane-only seam between FastFiles (UI) and
// FastFilesEngine (design.md D3). Deliberately just as closed a surface as
// the engine<->service protocol, even though both processes run at the
// same privilege level -- there's no reason to accept arbitrary requests
// just because elevation isn't at stake here.
enum class UiMessageType : uint16_t {
    Subscribe = 1,          // UI -> Engine: begin receiving generation notifications
    SubscribeAck = 2,       // Engine -> UI: current generation + snapshot section name
    RequestDirectory = 3,   // UI -> Engine: enumerate + watch this path
    DirectoryError = 4,     // Engine -> UI: the requested path is inaccessible or gone
    NewGeneration = 5,      // Engine -> UI: unsolicited, a new snapshot generation is ready
    EngineStatus = 6,       // Engine -> UI: unsolicited, connection-state badge update
    ReloadIndexingConfig = 7, // UI -> Engine: settings.json was atomically replaced
    RequestUnavailableVolumes = 8, // UI -> Engine: enumerate forget-eligible volume rows
    UnavailableVolumes = 9, // Engine -> UI: fixed-record list response
    ForgetUnavailableVolume = 10, // UI -> Engine: explicit destructive action by durable row id
    ForgetUnavailableVolumeResult = 11, // Engine -> UI: explicit outcome for the requested row
};

std::optional<UiMessageType> ToUiMessageType(uint16_t raw) noexcept;

enum class DirectoryErrorReason : uint16_t {
    AccessDenied = 1,
    NoLongerAvailable = 2,
};

enum class PrivilegedPathStatus : uint16_t {
    Unavailable = 1, // degraded mode: no service, declined, incompatible, or disconnected
    Active = 2,      // privileged path connected and authenticated
};

// NTFS's own maximum full path length in UTF-16 code units (long-path
// aware). A length-prefixed field like every other variable-length field
// in this protocol (Records.h) -- out-of-range rejects the whole message,
// never clamps.
constexpr uint16_t kMaxPathLengthChars = 32767;

#pragma pack(push, 1)

struct SubscribeAckPayload {
    uint64_t generationId;
    // Snapshot section name (e.g. "Local\FastFiles.IndexSnapshot.<SessionId>")
    // follows as a length-prefixed UTF-16 string.
    uint16_t sectionNameLengthChars;
};

// RequestDirectory payload: a length-prefixed UTF-16 path follows this
// (empty fixed header -- the whole payload is the path).
struct RequestDirectoryHeader {
    uint16_t pathLengthChars;
};

struct DirectoryErrorPayload {
    DirectoryErrorReason reason;
    uint16_t pathLengthChars; // the path follows, length-prefixed
};

struct NewGenerationPayload {
    uint64_t generationId;
};

struct EngineStatusPayload {
    PrivilegedPathStatus status;
};

struct UnavailableVolumesHeader {
    uint32_t count;
};

struct UnavailableVolumeRecord {
    int64_t volumeRowId;
    uint8_t volumeGuid[16];
    uint32_t serialNumber;
    uint64_t entryCount;
};

struct ForgetUnavailableVolumePayload {
    int64_t volumeRowId;
};

enum class ForgetUnavailableVolumeStatus : uint16_t {
    Removed = 1,
    NotFound = 2,
    VolumeAvailable = 3,
    Failed = 4,
};

bool IsForgetUnavailableVolumeStatusValid(ForgetUnavailableVolumeStatus status) noexcept;

struct ForgetUnavailableVolumeResultPayload {
    int64_t volumeRowId;
    ForgetUnavailableVolumeStatus status;
};

#pragma pack(pop)

bool IsUiPathLengthValid(uint16_t lengthChars) noexcept;

enum class UiFrameValidationResult {
    Valid,
    RejectedOversizedFrame,
    RejectedUnknownMessageType,
    RejectedUnsupportedStructVersion,
};

// Mirrors ffprotocol::ValidateFrame (Dispatch.h) for the UI seam's own
// closed message set -- validated the same way (bounds-checked lookup, no
// raw jump table) even though both peers run at the same privilege level.
UiFrameValidationResult ValidateUiFrame(const FrameHeader& header) noexcept;

} // namespace ffprotocol
