#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
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
    RequestIndexedSearch = 17,   // UI -> Engine: bounded query against the durable projection
    IndexedSearchResult = 18,    // Engine -> UI: bounded candidate page/result
    CancelIndexedSearch = 19,    // audit-remediation FF-AUDIT-06: cancel one request on this UI connection
    // allocated-size-storage-accuracy D6: additive aggregate response
    // carrying the allocated-size subtotal plus the explicit completeness
    // pair. Message 13 keeps its exact legacy payload; a UI that understands
    // both parses either, treating a legacy result as allocated-unknown.
    FolderAggregateResultV2 = 20, // Engine -> UI: subtree aggregate with allocated-size fields
    // indexed-search-allocated-size D5: additive extended indexed-search result
    // carrying on-disk allocated size + explicit knownness per candidate. Message
    // 18 keeps its exact legacy payload and decodes as allocated-unknown; a UI
    // that understands both parses either, never reinterpreting the legacy layout.
    IndexedSearchResultV2 = 21, // Engine -> UI: indexed result with allocated-size fields
};

std::optional<UiMessageType> ToUiMessageType(uint16_t raw) noexcept;

enum class DirectoryErrorReason : uint16_t {
    AccessDenied = 1,
    NoLongerAvailable = 2,
    // recovery-degraded-browsing 4.1: the enumeration succeeded but the
    // snapshot publish failed (e.g. slot capacity exceeded); the client must
    // not keep showing stale data for this path. Additive value -- both
    // peers ship together and the UI handles it explicitly.
    PublishFailed = 3,
    EnumerationFailed = 4,
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

// Search requests are typed into a fixed-size Win32 edit control today, but
// the protocol owns its own bound so a future client cannot turn a pipe frame
// into an unbounded parser allocation.
constexpr uint16_t kMaxIndexedSearchQueryLengthChars = 4095;
constexpr uint32_t kMaxIndexedSearchRequestResults = 10000;
// A response may be smaller than the UI's display cap because candidate paths
// are variable-length and every UI frame is capped at kMaxFrameSize.
constexpr uint32_t kMaxIndexedSearchResultCount = 4096;

#pragma pack(push, 1)

struct SubscribeAckPayload {
    uint64_t generationId;
    // Snapshot section name for one committed generation, e.g.
    // "Local\FastFiles.IndexSnapshot.<SessionId>.<Instance>.<Generation>"
    // (composition: ffsetup::kSnapshotSectionNameFormat + instance +
    // GenerationSectionName); follows as a length-prefixed UTF-16 string.
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

// allocated-size-storage-accuracy D6/D7: extended aggregate result (message
// 20). totalAllocatedSizeBytes is the subtotal over descendants whose
// allocated size is known; allocatedBytesKnownCount/allocatedBytesTotalCount
// are the completeness pair -- `Complete` only when the counts are equal,
// so a partial subtotal is never presented as an exact total. Legacy
// FolderAggregateResult (message 13) remains byte-identical and maps to
// this shape with the allocated fields unknown.
struct FolderAggregateResultV2Payload {
    uint64_t requestId;
    FolderAggregateStatus status;
    uint64_t itemCount;
    uint64_t totalSizeBytes;
    uint64_t totalAllocatedSizeBytes;
    uint64_t allocatedBytesKnownCount;
    uint64_t allocatedBytesTotalCount;
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

// Search scope/sort values intentionally mirror ffsearch's closed enum values
// without making the wire protocol depend on the search library. Peers must
// validate the raw values before casting them to an internal enum.
struct IndexedSearchRequestHeader {
    uint64_t requestId;
    uint8_t scope;       // 0..3: CurrentFolder .. AllIndexedLocations
    uint8_t sortField;   // 0..6: Relevance .. AllocatedSize (Size on Disk)
    uint8_t descending;  // 0 or 1
    uint8_t reserved;
    uint16_t queryLengthChars;
    uint16_t scopePathLengthChars;
    uint32_t maxResults;
};

struct CancelIndexedSearchPayload {
    uint64_t requestId;
};

enum class IndexedSearchStatus : uint16_t {
    Completed = 1,
    Unavailable = 2,
    InvalidRequest = 3,
    Cancelled = 4,
};

struct IndexedSearchResultHeader {
    uint64_t requestId;
    IndexedSearchStatus status;
    uint8_t truncated;
    uint8_t reserved;
    uint32_t count;
};

struct IndexedSearchCandidateHeader {
    uint16_t nameLengthChars;
    uint16_t folderLengthChars;
    uint8_t flags; // bit 0 = directory, bit 1 = persistent-indexed
    uint8_t reserved[3];
    int64_t volumeId;
    uint64_t id;
    uint64_t parentId;
    uint64_t sizeBytes;
    uint64_t modifiedTime;
    uint64_t createdTime;
};

// indexed-search-allocated-size D5: additive extended candidate header carried
// by message 21. Every legacy field is preserved byte-for-byte; the only new
// wire data is the trailing `allocatedSizeBytes` and the knownness bit (0x04 in
// `flags`). The legacy header is intentionally NOT reinterpreted -- a UI that
// only knows message 18 still decodes it and maps allocated size to unknown.
struct IndexedSearchCandidateHeaderV2 {
    uint16_t nameLengthChars;
    uint16_t folderLengthChars;
    uint8_t flags; // bit 0 = directory, bit 1 = persistent-indexed, bit 2 = allocated-known
    uint8_t reserved[3];
    int64_t volumeId;
    uint64_t id;
    uint64_t parentId;
    uint64_t sizeBytes;
    uint64_t modifiedTime;
    uint64_t createdTime;
    uint64_t allocatedSizeBytes;
};

#pragma pack(pop)

struct IndexedSearchRequest {
    uint64_t requestId = 0;
    uint8_t scope = 0;
    uint8_t sortField = 0;
    bool descending = false;
    uint32_t maxResults = 0;
    std::wstring query;
    std::wstring scopePath;
};

struct IndexedSearchCandidate {
    std::wstring name;
    std::wstring folder;
    uint64_t sizeBytes = 0;
    uint64_t modifiedTime = 0;
    bool isDirectory = false;
    uint64_t createdTime = 0;
    int64_t volumeId = 0;
    uint64_t id = 0;
    uint64_t parentId = 0;
    bool persistentIndexed = true;
    // indexed-search-allocated-size D1/D5: on-disk allocated size + explicit
    // knownness. A legacy (message 18) decode leaves both at their zero/false
    // defaults, so a legacy candidate is always allocated-unknown; the V2
    // decoder (message 21) fills them from the extended header. Appended so the
    // existing positional aggregate initializers keep mapping to the members
    // they always did.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
};

struct IndexedSearchResponse {
    uint64_t requestId = 0;
    IndexedSearchStatus status = IndexedSearchStatus::Unavailable;
    bool truncated = false;
    std::vector<IndexedSearchCandidate> candidates;
};

bool IsUiPathLengthValid(uint16_t lengthChars) noexcept;
bool IsForgetUnavailableVolumeStatusValid(ForgetUnavailableVolumeStatus status) noexcept;
bool IsFolderAggregateStatusValid(FolderAggregateStatus status) noexcept;
bool IsIndexedSearchStatusValid(IndexedSearchStatus status) noexcept;
bool IsIndexedSearchScopeValid(uint8_t scope) noexcept;
bool IsIndexedSearchSortValid(uint8_t sortField) noexcept;

// Exact, overflow-checked request parser. The returned strings are copied only
// after all fixed fields, lengths, and total payload size have been validated.
std::optional<IndexedSearchRequest> ParseIndexedSearchRequest(const uint8_t* payload,
                                                              size_t payloadSize);

// Exact response parser used by the UI reader. It bounds count before reserve,
// validates every candidate's fixed flags/lengths, and rejects trailing bytes.
std::optional<IndexedSearchResponse> ParseIndexedSearchResponse(const uint8_t* payload,
                                                                size_t payloadSize);

// indexed-search-allocated-size D5: additive V2 response parser. Identical
// bounds/length/flag policy to the legacy parser, plus the extended
// `allocatedSizeBytes` field and knownness bit (0x04). A legacy (message 18)
// payload must NOT be fed here; this decodes only the message-21 layout and maps
// the knownness bit into IndexedSearchCandidate.allocatedSizeKnown.
std::optional<IndexedSearchResponse> ParseIndexedSearchResponseV2(const uint8_t* payload,
                                                                  size_t payloadSize);

// Serializes a trusted engine response while enforcing the protocol-wide frame
// budget. `outTruncated` reports byte/count truncation performed by the
// serializer in addition to response.truncated.
std::vector<uint8_t> SerializeIndexedSearchRequest(const IndexedSearchRequest& request);
std::vector<uint8_t> SerializeIndexedSearchResponse(const IndexedSearchResponse& response,
                                                    bool* outTruncated = nullptr);

// indexed-search-allocated-size D5: additive V2 response serializer. Emits the
// message-21 layout; when a candidate's allocated size is unknown it writes
// zero on-disk bytes (never a fabricated value). Same frame-budget/truncation
// contract as the legacy serializer.
std::vector<uint8_t> SerializeIndexedSearchResponseV2(const IndexedSearchResponse& response,
                                                      bool* outTruncated = nullptr);

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
