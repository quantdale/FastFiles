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
#pragma pack(push, 1)
struct MftRecordFixedV1 {
    uint64_t fileReferenceNumber;
    uint64_t parentFileReferenceNumber;
    uint64_t creationTime;
    uint64_t lastModifiedTime;
    uint64_t lastAccessTime;
    uint64_t sizeBytes; // real (logical) size, from $FILE_NAME's cached RealSize field
    uint32_t fileAttributes;
    uint16_t fileNameLengthChars;
};

struct UsnDeltaFixedV1 {
    uint64_t usn;
    uint64_t fileReferenceNumber;
    uint64_t parentFileReferenceNumber;
    uint32_t reason;
    uint64_t timestamp;
    uint64_t sizeBytes; // as MftRecordFixedV1::sizeBytes -- USN_RECORD itself never carries size
    uint32_t fileAttributes;
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

// Inverse of ParseMftBatch/ParseUsnDeltaBatch: produces the flat records
// blob those functions expect (fixed struct + trailing filename per
// record, back to back). The record count itself travels separately on
// the wire (e.g. ScanBatchHeader::recordCount) -- these functions do not
// emit a count prefix.
std::vector<uint8_t> SerializeMftBatch(const std::vector<MftRecordV1>& records);
std::vector<uint8_t> SerializeUsnDeltaBatch(const std::vector<UsnDeltaV1>& records);

} // namespace ffprotocol
