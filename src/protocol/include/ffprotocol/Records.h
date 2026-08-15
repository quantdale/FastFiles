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

// allocated-size-storage-accuracy D3: additive MFT record for peers that
// negotiated protocol {2,1}+ (ProtocolSupportsAllocatedSize). A parsed
// $FILE_NAME always yields both cached sizes from the same validated
// attribute, so V2 carries allocatedSizeBytes unconditionally -- no presence
// mask is needed for full scan records. MftRecordFixedV1 is unchanged.
struct MftRecordFixedV2 {
    uint64_t fileReferenceNumber;
    uint64_t parentFileReferenceNumber;
    uint64_t creationTime;
    uint64_t lastModifiedTime;
    uint64_t lastAccessTime;
    uint64_t sizeBytes;
    uint64_t allocatedSizeBytes;
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
// allocated-size-storage-accuracy D3: authoritative allocated size, set only
// after successful MFT re-enrichment (which yields both cached $FILE_NAME
// sizes); never derived or zero-fabricated.
constexpr UsnDeltaFieldMask kUsnFieldAllocatedSizeBytes = 1u << 7;
constexpr UsnDeltaFieldMask kUsnFieldMaskAll =
    kUsnFieldParentId | kUsnFieldName | kUsnFieldSizeBytes |
    kUsnFieldCreationTime | kUsnFieldLastModifiedTime |
    kUsnFieldLastAccessTime | kUsnFieldAttributes | kUsnFieldAllocatedSizeBytes;
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

// allocated-size-storage-accuracy D3: additive USN record for peers that
// negotiated protocol {2,1}+. Identical to V2 plus allocatedSizeBytes, which
// participates in the same authoritative presence-mask semantics (the V2
// layout itself is a compatibility contract and is never mutated).
struct UsnDeltaFixedV3 {
    uint64_t usn;
    uint64_t fileReferenceNumber;
    uint64_t parentFileReferenceNumber;
    uint32_t reason;
    uint64_t timestamp;
    uint64_t creationTime;
    uint64_t lastModifiedTime;
    uint64_t lastAccessTime;
    uint64_t sizeBytes;
    uint64_t allocatedSizeBytes;
    uint32_t fileAttributes;
    UsnDeltaFieldMask fieldMask;
    uint16_t fileNameLengthChars;
};
#pragma pack(pop)

struct MftRecordV1 {
    MftRecordFixedV1 fixed;
    std::u16string fileName;
};

struct MftRecordV2 {
    MftRecordFixedV2 fixed;
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

struct UsnDeltaV3 {
    UsnDeltaFixedV3 fixed;
    std::u16string fileName;
};

// The V1/V2 layouts are compatibility contracts: their binary size must
// never change. New fields ride only in V2/V3 records.
static_assert(sizeof(MftRecordFixedV1) == 8 * 6 + 4 + 2, "MftRecordFixedV1 layout is a compatibility contract");
static_assert(sizeof(UsnDeltaFixedV1) == 8 * 5 + 4 * 2 + 2, "UsnDeltaFixedV1 layout is a compatibility contract");
static_assert(sizeof(UsnDeltaFixedV2) == 8 * 8 + 4 * 3 + 2, "UsnDeltaFixedV2 layout is a compatibility contract");

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
// allocated-size-storage-accuracy 2.1: V2 MFT batch (fixed layout +
// allocatedSizeBytes). Same count-vs-bytes and malformed-input discipline
// as ParseMftBatch, over the larger fixed record size.
std::optional<std::vector<MftRecordV2>> ParseMftBatchV2(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount);

std::optional<std::vector<UsnDeltaV1>> ParseUsnDeltaBatch(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount);
std::optional<std::vector<UsnDeltaV2>> ParseUsnDeltaV2Batch(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount);
// allocated-size-storage-accuracy 2.1: V3 USN batch (V2 + allocatedSizeBytes
// under kUsnFieldAllocatedSizeBytes). An unknown mask bit rejects the whole
// batch exactly as V2 does.
std::optional<std::vector<UsnDeltaV3>> ParseUsnDeltaV3Batch(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount);

// Compatibility adapter for peers that still send the original V1 USN
// record. V1 has no creation/access fields, so those bits remain absent;
// callers must not infer presence from the numeric values in the struct.
UsnDeltaV2 UpgradeUsnDeltaV1(const UsnDeltaV1& record);

// allocated-size-storage-accuracy 2.1: V2 -> V3 adapter. The allocated-size
// bit stays absent (V2 cannot carry it); absence is preserved, never
// synthesized.
UsnDeltaV3 UpgradeUsnDeltaV2(const UsnDeltaV2& record);

// Lossy compatibility conversion used only after the service has confirmed
// that a V2 record contains every field the V1 layout can represent. V1 has
// no creation/access fields and no presence mask; callers must filter partial
// non-delete records before invoking this function.
UsnDeltaV1 DowngradeUsnDeltaV2(const UsnDeltaV2& record);

// allocated-size-storage-accuracy 2.1/2.5: true when a V3 record can be
// carried by a V2 peer -- i.e. the allocated-size bit is absent. A record
// with the bit present must be omitted for a V2 peer (non-delete updates are
// recovered by reconciliation), never zero-filled.
bool IsRepresentableInUsnV2(const UsnDeltaV3& record) noexcept;
UsnDeltaV2 DowngradeUsnDeltaV3(const UsnDeltaV3& record);

// allocated-size-storage-accuracy 2.5: V3 -> V1 downgrade for major-1 peers.
// Unlike the V2 case, V1 simply has no allocated-size field at all, so the
// bit is dropped losslessly (the engine's V1 upgrade path marks it absent);
// the V1-required presence-mask check remains the caller's gate
// (IsRepresentableInLegacyV1 in the service).
UsnDeltaV1 DowngradeUsnDeltaV3ToV1(const UsnDeltaV3& record);

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
std::vector<uint8_t> SerializeMftBatchV2(const std::vector<MftRecordV2>& records);
std::vector<uint8_t> SerializeUsnDeltaBatch(const std::vector<UsnDeltaV1>& records);
std::vector<uint8_t> SerializeUsnDeltaV2Batch(const std::vector<UsnDeltaV2>& records);
std::vector<uint8_t> SerializeUsnDeltaV3Batch(const std::vector<UsnDeltaV3>& records);

} // namespace ffprotocol
