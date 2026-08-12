#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace ffindexsvc {

// The NTFS USN journal can return V2 records with 64-bit file references or
// V3 records with FILE_ID_128 fields. NTFS uses the low 64 bits of a V3 ID for
// the same file reference accepted by FSCTL_GET_NTFS_FILE_RECORD; a non-zero
// high half is not representable by the current NTFS wire format and must be
// rejected rather than truncated.
struct DecodedUsnRecord {
    uint64_t usn = 0;
    uint64_t fileReferenceNumber = 0;
    uint64_t parentFileReferenceNumber = 0;
    uint32_t reason = 0;
    uint64_t timestamp = 0;
    uint32_t fileAttributes = 0;
    uint16_t fileNameLength = 0;
    uint16_t fileNameOffset = 0;
};

// Decodes one complete USN_RECORD_V2 or USN_RECORD_V3. The supplied byte
// range is treated as untrusted: the declared RecordLength, fixed prefix, and
// filename range are all checked before any field is consumed.
std::optional<DecodedUsnRecord> DecodeUsnRecord(const uint8_t* recordBytes, size_t recordSize) noexcept;

} // namespace ffindexsvc
