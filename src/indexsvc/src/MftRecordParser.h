#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ffindexsvc {

// Everything this parser extracts from one raw MFT record -- exactly the
// allowlisted fields (spec "MFT Attribute Allowlisting Enforced at the
// Parser": $STANDARD_INFORMATION + $FILE_NAME only, never $DATA or
// anything else, resident or not).
struct ParsedMftRecord {
    bool isDirectory = false; // from the record header's own flags, not either attribute's DOS bits

    bool hasStandardInformation = false;
    uint64_t lastWriteTime = 0; // $STANDARD_INFORMATION's LastModificationTime (FILETIME)
    uint32_t dosAttributes = 0; // raw READONLY/HIDDEN/SYSTEM/ARCHIVE/... bits (never includes DIRECTORY)

    bool hasFileName = false;
    uint64_t parentFileReferenceNumber = 0; // packed (sequence-number-in-high-16-bits) form, same as our own FRNs
    uint64_t realSizeBytes = 0;
    std::u16string name;
};

// Parses one raw MFT record buffer -- `recordBuffer` is the exact bytes
// FSCTL_GET_NTFS_FILE_RECORD (or an equivalent raw read) returned for a
// single record, still carrying its Update Sequence Array "fixup" bytes.
// `bytesPerSector` is the volume's sector size (governs where each
// per-sector fixup slot lives).
//
// This is the security-critical boundary the rest of the codebase's
// "treat raw on-disk NTFS attribute bytes as fully untrusted" invariant
// rests on (CLAUDE.md) -- every offset is bounds-checked against the
// buffer before use, and a std::nullopt return means "isolate and skip
// this one record" (task 4.3/5.3's "malformed record is skipped, not
// fatal"), never a crash or an out-of-bounds read.
//
// A record can carry more than one $FILE_NAME attribute (hard links, or a
// separate short-DOS-name attribute); this returns exactly one, preferring
// a Win32 or Win32+DOS namespace name over a DOS-only one, consistent with
// this store's one-row-per-(volume, FileReferenceNumber) design (D7) not
// modeling multiple hard-link parents for a single entry.
std::optional<ParsedMftRecord> ParseMftRecord(const std::vector<uint8_t>& recordBuffer, uint32_t bytesPerSector);

} // namespace ffindexsvc
