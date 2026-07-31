#include "MftParser.h"

#include <cstring>

namespace ffindexsvc {

namespace {

uint16_t ReadU16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
uint32_t ReadU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
uint64_t ReadU64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

constexpr uint16_t kFileRecordInUseFlag = 0x0001;
constexpr uint16_t kFileRecordDirectoryFlag = 0x0002;
constexpr uint32_t kFileAttributeDirectory = 0x00000010;

// FILE_RECORD_SEGMENT_HEADER field byte offsets (stable since NTFS's
// original NT4 on-disk format; the version-dependent trailing fields
// this parser doesn't need are ignored).
constexpr size_t kOffsetSignature = 0;
constexpr size_t kOffsetUpdateSeqArrayOffset = 4;
constexpr size_t kOffsetUpdateSeqArraySize = 6;
constexpr size_t kOffsetFlags = 22;
constexpr size_t kOffsetBytesInUse = 24;
constexpr size_t kOffsetFirstAttributeOffset = 20;

constexpr uint32_t kAttributeTypeStandardInformation = 0x10;
constexpr uint32_t kAttributeTypeFileName = 0x30;
constexpr uint32_t kAttributeTypeEnd = 0xFFFFFFFFu;

constexpr uint8_t kFileNameNamespaceDos = 2;

} // namespace

FixupResult ApplyFixupAndValidate(uint8_t* record, size_t recordSize, uint32_t bytesPerSector) {
    if (bytesPerSector == 0 || recordSize < kOffsetBytesInUse + sizeof(uint32_t)) {
        return FixupResult::Malformed;
    }
    if (std::memcmp(record + kOffsetSignature, "FILE", 4) != 0) {
        return FixupResult::NotAFileRecord;
    }

    const uint16_t flags = ReadU16(record + kOffsetFlags);
    if ((flags & kFileRecordInUseFlag) == 0) {
        return FixupResult::NotInUse;
    }

    const uint16_t usaOffset = ReadU16(record + kOffsetUpdateSeqArrayOffset);
    const uint16_t usaSize = ReadU16(record + kOffsetUpdateSeqArraySize); // includes the USN entry itself
    if (usaSize == 0 || static_cast<size_t>(usaOffset) + static_cast<size_t>(usaSize) * sizeof(uint16_t) > recordSize) {
        return FixupResult::Malformed;
    }

    const size_t sectorCount = recordSize / bytesPerSector;
    if (sectorCount + 1 != usaSize) {
        return FixupResult::Malformed;
    }

    const uint8_t* usa = record + usaOffset;
    const uint16_t usn = ReadU16(usa);

    for (size_t sector = 0; sector < sectorCount; ++sector) {
        const size_t lastTwoBytesOffset = (sector + 1) * static_cast<size_t>(bytesPerSector) - sizeof(uint16_t);
        if (lastTwoBytesOffset + sizeof(uint16_t) > recordSize) {
            return FixupResult::Malformed;
        }
        uint8_t* sectorTail = record + lastTwoBytesOffset;
        if (ReadU16(sectorTail) != usn) {
            // A torn write: the sector's saved marker doesn't match the
            // record's USN -- the record is inconsistent and must not be
            // trusted (this is exactly what the fixup mechanism exists to
            // detect).
            return FixupResult::Malformed;
        }
        const uint8_t* trueValue = usa + (sector + 1) * sizeof(uint16_t);
        std::memcpy(sectorTail, trueValue, sizeof(uint16_t));
    }

    return FixupResult::Ok;
}

std::optional<ParsedMftRecord> ParseMftAttributes(const uint8_t* record, size_t recordSize) {
    if (recordSize < kOffsetBytesInUse + sizeof(uint32_t)) {
        return std::nullopt;
    }

    const uint16_t firstAttributeOffset = ReadU16(record + kOffsetFirstAttributeOffset);
    const uint32_t bytesInUse = ReadU32(record + kOffsetBytesInUse);
    const uint16_t headerFlags = ReadU16(record + kOffsetFlags);
    const size_t recordExtent = bytesInUse < recordSize ? bytesInUse : recordSize;
    if (firstAttributeOffset >= recordExtent) {
        return std::nullopt;
    }

    bool haveStandardInformation = false;
    uint64_t creationTime = 0, lastModifiedTime = 0, lastAccessTime = 0;
    uint32_t standardInfoAttributes = 0;

    bool haveName = false;
    uint64_t parentRef = 0;
    uint64_t realSize = 0;
    uint8_t bestNamespace = 0xFF;
    std::u16string bestName;

    size_t offset = firstAttributeOffset;
    while (offset + 8 <= recordExtent) {
        const uint32_t typeCode = ReadU32(record + offset);
        if (typeCode == kAttributeTypeEnd) {
            break;
        }
        if (offset + 24 > recordExtent) {
            return std::nullopt; // truncated attribute header -- malformed
        }
        const uint32_t recordLength = ReadU32(record + offset + 4);
        if (recordLength < 24 || offset + recordLength > recordExtent) {
            return std::nullopt;
        }
        const uint8_t formCode = record[offset + 8];

        if (formCode == 0 /* resident */ && (typeCode == kAttributeTypeStandardInformation || typeCode == kAttributeTypeFileName)) {
            const uint32_t valueLength = ReadU32(record + offset + 16);
            const uint16_t valueOffset = ReadU16(record + offset + 20);
            if (static_cast<uint64_t>(valueOffset) + valueLength > recordLength) {
                return std::nullopt;
            }
            const uint8_t* value = record + offset + valueOffset;

            if (typeCode == kAttributeTypeStandardInformation) {
                // A malformed $STANDARD_INFORMATION isolates the whole
                // record rather than being silently skipped -- every real
                // record has exactly one, so a broken one means the
                // record itself is inconsistent.
                if (valueLength < 36) {
                    return std::nullopt;
                }
                creationTime = ReadU64(value + 0);
                lastModifiedTime = ReadU64(value + 8);
                lastAccessTime = ReadU64(value + 24);
                standardInfoAttributes = ReadU32(value + 32);
                haveStandardInformation = true;
            } else if (typeCode == kAttributeTypeFileName && valueLength >= 66) {
                // Unlike $STANDARD_INFORMATION, an individual malformed
                // $FILE_NAME does not reject the record on its own -- a
                // record with one bad name attribute alongside a good one
                // should still surface the good one; if none ever parses,
                // haveName stays false and the record is skipped below.
                const uint8_t nameLengthChars = value[64];
                const uint8_t nsType = value[65];
                const size_t nameBytes = static_cast<size_t>(nameLengthChars) * sizeof(char16_t);
                if (nameLengthChars > 0 && 66u + nameBytes <= valueLength) {
                    // Prefer a Win32-usable name over a DOS-only 8.3 alias
                    // if this record happens to have both -- first
                    // Win32/Win32AndDos name wins, otherwise fall back to
                    // whatever name was seen first.
                    if (!haveName || (bestNamespace == kFileNameNamespaceDos && nsType != kFileNameNamespaceDos)) {
                        parentRef = ReadU64(value + 0);
                        realSize = ReadU64(value + 48);
                        bestNamespace = nsType;
                        bestName.assign(reinterpret_cast<const char16_t*>(value + 66), nameLengthChars);
                        haveName = true;
                    }
                }
            }
        }

        offset += recordLength;
    }

    if (!haveStandardInformation || !haveName) {
        return std::nullopt;
    }

    ParsedMftRecord result;
    result.parentFileReferenceNumber = parentRef;
    result.creationTime = creationTime;
    result.lastModifiedTime = lastModifiedTime;
    result.lastAccessTime = lastAccessTime;
    result.sizeBytes = realSize;
    // $STANDARD_INFORMATION's FileAttributes never carries
    // FILE_ATTRIBUTE_DIRECTORY for a directory record (a well-known NTFS
    // on-disk quirk) -- the record header's own directory flag is the
    // authoritative source, so it's merged in here rather than trusting
    // the attribute value alone.
    result.fileAttributes = standardInfoAttributes | ((headerFlags & kFileRecordDirectoryFlag) != 0 ? kFileAttributeDirectory : 0);
    result.fileName = std::move(bestName);
    return result;
}

} // namespace ffindexsvc
