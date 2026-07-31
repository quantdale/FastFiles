#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace ffindexsvc {

// index-storage-and-scanning tasks.md 4.1/4.2: parses raw on-disk NTFS
// MFT record bytes, forwarding only the $STANDARD_INFORMATION/$FILE_NAME
// allowlisted fields -- never $DATA or any other attribute type. This
// file is deliberately free of any Win32/DeviceIoControl dependency so
// the byte-parsing logic itself (fixup application, attribute-record
// walking, bounds validation) is independently testable off-Windows;
// VolumeScanner.h/.cpp is the Windows-only layer that retrieves raw
// record bytes via FSCTL_GET_NTFS_FILE_RECORD and calls into this parser.
//
// Every raw byte here is untrusted input (design.md D4: "a plugged-in
// USB/VHD reaches the parser with no admin action") -- every declared
// length is validated against the buffer's actual size before use, and a
// malformed record is reported to the caller for skip-and-continue
// handling (tasks.md 4.3 / "Malformed MFT ... Records Are Isolated, Not
// Fatal") rather than trusted or causing undefined behavior.

enum class FixupResult {
    Ok,
    NotAFileRecord,  // missing "FILE" signature
    NotInUse,        // record's FILE_RECORD_SEGMENT_IN_USE flag is clear (free/deleted record)
    Malformed,       // internally inconsistent lengths, or a fixup mismatch (torn/corrupt sector)
};

// Applies the NTFS Update Sequence Array fixup in place (restoring each
// sector's true last two bytes) and validates the record's signature and
// in-use flag. Must be called, and must return Ok, before
// ParseMftAttributes is called on the same buffer.
FixupResult ApplyFixupAndValidate(uint8_t* record, size_t recordSize, uint32_t bytesPerSector);

struct ParsedMftRecord {
    // Not filled in by ParseMftAttributes -- the caller (VolumeScanner)
    // already knows the FileReferenceNumber it requested this record for.
    uint64_t fileReferenceNumber = 0;

    uint64_t parentFileReferenceNumber = 0;
    uint64_t creationTime = 0;
    uint64_t lastModifiedTime = 0;
    uint64_t lastAccessTime = 0;
    uint64_t sizeBytes = 0;
    uint32_t fileAttributes = 0;
    std::u16string fileName;
};

// Parses $STANDARD_INFORMATION and $FILE_NAME from an already fixed-up
// and validated record buffer (see ApplyFixupAndValidate). Returns
// std::nullopt if the record has no usable $FILE_NAME attribute, or if
// any attribute's declared length is internally inconsistent with the
// buffer -- the caller skips this one record and continues with the
// next (tasks.md 4.3), it never aborts the surrounding scan.
std::optional<ParsedMftRecord> ParseMftAttributes(const uint8_t* record, size_t recordSize);

} // namespace ffindexsvc
