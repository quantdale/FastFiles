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
    RequestFolderAggregate = 12, // UI -> Engine: async subtree size/count request
    FolderAggregateResult = 13,  // Engine -> UI: subtree aggregate response
    RequestVolumeStatus = 14,    // UI -> Engine: request the per-volume index-health report
    VolumeStatus = 15,           // Engine -> UI: fixed-record per-volume condition report
    SetIndexingPaused = 16,      // UI -> Engine: pause/resume indexing, global (scope=0) or per-volume
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

// file-preview-and-properties §6 / storage-analysis §1.2: request identity
// lets the UI reject stale results that arrive after the user has navigated
// or selected a different folder.
struct RequestFolderAggregatePayload {
    uint64_t requestId;
    int64_t volumeRowId;
    uint64_t parentFrnLow;
    uint64_t parentFrnHigh;
};

enum class FolderAggregateStatus : uint16_t {
    Resolved = 1,  // itemCount + totalSizeBytes are valid
    Pending = 2,   // engine is computing asynchronously; result follows later
    NotFound = 3,  // the requested folder is not known to the index
};

struct FolderAggregateResultPayload {
    uint64_t requestId;
    FolderAggregateStatus status;
    uint64_t itemCount;
    uint64_t totalSizeBytes;
};

// Per-volume index-health condition report (settings-and-appearance §7.3).
// The engine owns the underlying scan/reconciliation state and reports the
// discrete conditions (IndexHealth.h's VolumeIndexConditions) per volume
// over the UI seam; the UI applies the existing pure derivation
// (DeriveIndexHealth / ApplicableIndexConditions) to present the headline
// status and detail conditions. A single record carries a drive letter and
// one byte of condition flags.
enum VolumeStatusFlags : uint8_t {
    VolumeStatusReachable = 1 << 0,              // volume currently mounted/present
    VolumeStatusScanning = 1 << 1,               // initial scan or catch-up in progress
    VolumeStatusNeedsReconciliation = 1 << 2,    // reconciliation sweep required/pending
    VolumeStatusPartiallyIndexed = 1 << 3,       // some configured subtrees complete, not all
    VolumeStatusPaused = 1 << 4,                 // indexing paused for this volume (or globally)
    VolumeStatusPendingDecision = 1 << 5,        // engine observed the volume; not yet in the persisted list
};

struct VolumeStatusHeader {
    uint32_t count;
};

struct VolumeStatusRecord {
    uint8_t driveLetter; // 'C', 'D', ... 0 == unknown
    uint8_t flags;       // bitwise OR of VolumeStatusFlags
};

// settings-and-appearance §9.1/D9: pause/resume indexing control-plane
// request. scope == 0 means global (all volumes); otherwise it is an
// uppercase drive letter ('C'...'Z') limiting the action to one volume.
// Pause is transient engine state, deliberately never persisted (resume
// continues from the engine's last-applied cursor/USN position -- the
// spec's "continue from where it left off, without restarting from zero").
struct SetIndexingPausedPayload {
    uint8_t scope;   // 0 == global, otherwise a drive letter
    uint8_t paused;  // 0 == resume, 1 == pause
};

#pragma pack(pop)

bool IsUiPathLengthValid(uint16_t lengthChars) noexcept;
bool IsForgetUnavailableVolumeStatusValid(ForgetUnavailableVolumeStatus status) noexcept;
bool IsFolderAggregateStatusValid(FolderAggregateStatus status) noexcept;

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
