#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ffprotocol {

// NTFS's own on-disk maximum for a $FILE_NAME component, in UTF-16 code
// units. Raw MFT bytes are untrusted input (design.md D4) -- a value
// beyond this is rejected outright, never clamped.
constexpr uint16_t kMaxFileNameLengthChars = 255;

// Upper bound on how many records a single batch frame may declare,
// independent of kMaxFrameSize -- bounds the size of the pre-parse
// reserve() before any per-record validation runs.
constexpr uint32_t kMaxBatchRecordCount = 8192;

// $STANDARD_INFORMATION + $FILE_NAME fields only (spec "Never forward file
// content" -- $DATA is never read or forwarded). FileNameLength is a
// length-prefixed field: the trailing `FileNameLength` UTF-16 code units
// immediately follow this fixed struct on the wire.
// index-storage-and-scanning tasks.md 1.2/D2: entries need a persisted
// size, matching $STANDARD_INFORMATION's AllocatedLength/FileSize fields
// -- fileReferenceNumber/parentFileReferenceNumber are 64-bit here
// (NTFS); a 128-bit ReFS identifier is out of scope for this wire format
// version (design.md "Open Questions" -- left to a future extension, not
// resolved here).
#pragma pack(push, 1)
struct MftRecordFixedV1 {
    uint64_t fileReferenceNumber;
    uint64_t parentFileReferenceNumber;
    uint64_t creationTime;
    uint64_t lastModifiedTime;
    uint64_t lastAccessTime;
    uint64_t sizeBytes;
    uint32_t fileAttributes;
    uint16_t fileNameLengthChars;
};

struct UsnDeltaFixedV1 {
    uint64_t usn;
    uint64_t fileReferenceNumber;
    uint64_t parentFileReferenceNumber;
    uint32_t reason;
    uint64_t timestamp;
    uint64_t sizeBytes;
    uint32_t fileAttributes;
    uint16_t fileNameLengthChars;
};

// Additive USN representation. The explicit field mask is authoritative:
// zero is a valid NTFS value and is never used as an absence sentinel. V2 is
// emitted in JournalRecordBatchV2 frames after MFT re-enrichment; a legacy V1
// frame is converted with only the fields V1 actually carries marked present.
// The service marks a field present only after obtaining it from the
// corresponding source, so an MFT read failure cannot manufacture metadata.
using UsnDeltaFieldMask = uint32_t;
constexpr UsnDeltaFieldMask kUsnFieldParentId = 1u << 0;
constexpr UsnDeltaFieldMask kUsnFieldName = 1u << 1;
constexpr UsnDeltaFieldMask kUsnFieldSizeBytes = 1u << 2;
constexpr UsnDeltaFieldMask kUsnFieldCreationTime = 1u << 3;
constexpr UsnDeltaFieldMask kUsnFieldLastModifiedTime = 1u << 4;
constexpr UsnDeltaFieldMask kUsnFieldLastAccessTime = 1u << 5;
constexpr UsnDeltaFieldMask kUsnFieldAttributes = 1u << 6;
constexpr UsnDeltaFieldMask kUsnFieldMaskAll =
    kUsnFieldParentId | kUsnFieldName | kUsnFieldSizeBytes |
    kUsnFieldCreationTime | kUsnFieldLastModifiedTime |
    kUsnFieldLastAccessTime | kUsnFieldAttributes;
// The legacy V1 wire record has no presence mask. A V2 record may be
// downgraded only when every field V1 can carry is authoritative; otherwise
// the service must omit that non-delete update for a V1 peer rather than
// turning an absent value into a destructive zero.
constexpr UsnDeltaFieldMask kUsnFieldMaskRequiredForV1 =
    kUsnFieldParentId | kUsnFieldName | kUsnFieldSizeBytes |
    kUsnFieldLastModifiedTime | kUsnFieldAttributes;

struct UsnDeltaFixedV2 {
    uint64_t usn;
    uint64_t fileReferenceNumber;
    uint64_t parentFileReferenceNumber;
    uint32_t reason;
    uint64_t timestamp;
    uint64_t creationTime;
    uint64_t lastModifiedTime;
    uint64_t lastAccessTime;
    uint64_t sizeBytes;
    uint32_t fileAttributes;
    UsnDeltaFieldMask fieldMask;
    uint16_t fileNameLengthChars;
};
#pragma pack(pop)

struct MftRecordV1 {
    MftRecordFixedV1 fixed;
    std::u16string fileName;
};

struct UsnDeltaV1 {
    UsnDeltaFixedV1 fixed;
    std::u16string fileName;
};

struct UsnDeltaV2 {
    UsnDeltaFixedV2 fixed;
    std::u16string fileName;
};

// Rejects out-of-range length -- never silently clamps (spec "Frame and
// Input Validation").
bool IsFileNameLengthValid(uint16_t lengthChars) noexcept;

// Cheap upfront bound, checked before any per-record parsing or output
// reserve(): every record contributes at least sizeof(FixedV1) bytes plus
// one filename code unit, so a payload smaller than
// declaredCount * (sizeof(FixedV1) + sizeof(char16_t)) can never hold
// declaredCount valid records.
bool IsBatchCountPlausible(uint32_t declaredCount, size_t payloadSize, size_t fixedRecordSize) noexcept;

// Parses every record in the batch. Malformed input (out-of-range
// filename length, truncated trailing bytes, or leftover bytes after the
// declared count is consumed) rejects the WHOLE batch -- callers never
// see a partially-parsed result (spec: "not a crash or best-effort
// continuation").
std::optional<std::vector<MftRecordV1>> ParseMftBatch(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount);

std::optional<std::vector<UsnDeltaV1>> ParseUsnDeltaBatch(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount);
std::optional<std::vector<UsnDeltaV2>> ParseUsnDeltaV2Batch(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount);

// Compatibility adapter for peers that still send the original V1 USN
// record. V1 has no creation/access fields, so those bits remain absent;
// callers must not infer presence from the numeric values in the struct.
UsnDeltaV2 UpgradeUsnDeltaV1(const UsnDeltaV1& record);

// Lossy compatibility conversion used only after the service has confirmed
// that a V2 record contains every field the V1 layout can represent. V1 has
// no creation/access fields and no presence mask; callers must filter partial
// non-delete records before invoking this function.
UsnDeltaV1 DowngradeUsnDeltaV2(const UsnDeltaV2& record);

// Serializes records into the same wire layout ParseMftBatch/
// ParseUsnDeltaBatch consume -- used by `FastFilesIndexSvc` to build
// StartVolumeScan/OpenUsnJournal batch payloads. Records whose filename
// length is out of range (per IsFileNameLengthValid) are silently
// dropped, not serialized -- the service allowlist-parser is expected to
// have already validated each record before reaching here (Records.h is
// the wire-format layer, not the semantic-validity layer), but this
// keeps the two directions symmetric under fuzzing/round-trip tests
// rather than emitting a frame the receiver's own parser would reject.
std::vector<uint8_t> SerializeMftBatch(const std::vector<MftRecordV1>& records);
std::vector<uint8_t> SerializeUsnDeltaBatch(const std::vector<UsnDeltaV1>& records);
std::vector<uint8_t> SerializeUsnDeltaV2Batch(const std::vector<UsnDeltaV2>& records);

} // namespace ffprotocol
