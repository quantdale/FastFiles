#include "MftRecordParser.h"

#include <cstring>

namespace ffindexsvc {

namespace {

// NTFS MFT record/attribute constants (stable, public on-disk format --
// unchanged since NTFS 3.0). Named per Microsoft's own attribute-type
// naming convention for readability.
constexpr uint32_t kFileRecordMagic = 0x454C4946; // 'FILE' little-endian
constexpr uint32_t kAttributeTypeStandardInformation = 0x10;
constexpr uint32_t kAttributeTypeFileName = 0x30;
constexpr uint32_t kAttributeTypeEnd = 0xFFFFFFFFu;
constexpr uint16_t kRecordHeaderFlagInUse = 0x0001;
constexpr uint16_t kRecordHeaderFlagDirectory = 0x0002;
constexpr uint8_t kFileNameNamespaceDos = 2;

// Bounds-checked little-endian reads -- every offset is validated against
// the buffer size before use; there is no code path in this file that
// reads past `buffer.size()`.
bool ReadU16(const std::vector<uint8_t>& buffer, size_t offset, uint16_t& out) {
    if (offset + sizeof(uint16_t) > buffer.size()) return false;
    uint16_t value;
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    out = value;
    return true;
}

bool ReadU32(const std::vector<uint8_t>& buffer, size_t offset, uint32_t& out) {
    if (offset + sizeof(uint32_t) > buffer.size()) return false;
    uint32_t value;
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    out = value;
    return true;
}

bool ReadU64(const std::vector<uint8_t>& buffer, size_t offset, uint64_t& out) {
    if (offset + sizeof(uint64_t) > buffer.size()) return false;
    uint64_t value;
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    out = value;
    return true;
}

bool ReadU8(const std::vector<uint8_t>& buffer, size_t offset, uint8_t& out) {
    if (offset >= buffer.size()) return false;
    out = buffer[offset];
    return true;
}

// Applies (and validates) the Update Sequence Array fixup in place:
// each sector's last 2 bytes were overwritten on disk with the "update
// sequence number" for torn-write detection; the true bytes live in the
// USA. A mismatch means a torn/corrupt write -- treated as a malformed
// record (task 4.3), never silently trusted.
bool ApplyUpdateSequenceFixup(std::vector<uint8_t>& buffer, uint32_t bytesPerSector) {
    if (bytesPerSector == 0 || bytesPerSector % 2 != 0) {
        return false;
    }
    uint16_t usaOffset = 0, usaCount = 0;
    if (!ReadU16(buffer, 4, usaOffset) || !ReadU16(buffer, 6, usaCount)) {
        return false;
    }
    if (usaCount == 0) {
        return false;
    }
    const uint32_t expectedSectorCount = static_cast<uint32_t>(buffer.size()) / bytesPerSector;
    // usaCount includes the update-sequence-number slot itself plus one
    // replacement slot per sector.
    if (static_cast<uint32_t>(usaCount) != expectedSectorCount + 1) {
        return false;
    }

    uint16_t updateSequenceNumber = 0;
    if (!ReadU16(buffer, usaOffset, updateSequenceNumber)) {
        return false;
    }

    for (uint32_t sector = 0; sector < expectedSectorCount; ++sector) {
        const size_t sectorLastTwoBytesOffset = static_cast<size_t>(sector + 1) * bytesPerSector - 2;
        uint16_t onDiskMarker = 0;
        if (!ReadU16(buffer, sectorLastTwoBytesOffset, onDiskMarker)) {
            return false;
        }
        if (onDiskMarker != updateSequenceNumber) {
            return false; // torn write / corruption: reject the whole record
        }
        uint16_t replacement = 0;
        if (!ReadU16(buffer, usaOffset + 2 + static_cast<size_t>(sector) * 2, replacement)) {
            return false;
        }
        std::memcpy(buffer.data() + sectorLastTwoBytesOffset, &replacement, sizeof(replacement));
    }
    return true;
}

struct AttributeHeader {
    uint32_t typeCode = 0;
    uint32_t recordLength = 0;
    bool nonResident = false;
    uint32_t valueLength = 0;
    uint16_t valueOffset = 0;
};

bool ReadAttributeHeader(const std::vector<uint8_t>& buffer, size_t offset, AttributeHeader& out) {
    if (!ReadU32(buffer, offset + 0, out.typeCode)) return false;
    if (out.typeCode == kAttributeTypeEnd) return true; // caller checks typeCode before using the rest
    if (!ReadU32(buffer, offset + 4, out.recordLength)) return false;
    if (out.recordLength < 16 || offset + out.recordLength > buffer.size()) {
        return false; // declared length runs past the buffer -- malformed
    }
    uint8_t nonResidentFlag = 0;
    if (!ReadU8(buffer, offset + 8, nonResidentFlag)) return false;
    out.nonResident = nonResidentFlag != 0;
    if (!out.nonResident) {
        if (!ReadU32(buffer, offset + 16, out.valueLength)) return false;
        if (!ReadU16(buffer, offset + 20, out.valueOffset)) return false;
        if (static_cast<uint64_t>(out.valueOffset) + out.valueLength > out.recordLength) {
            return false; // resident value would run past this attribute's own declared bounds
        }
    }
    return true;
}

// $STANDARD_INFORMATION's fixed-layout prefix (present in every version,
// NTFS 1.2 through 3.1+): 4 FILETIMEs, then a 4-byte DOS attributes field.
constexpr size_t kStandardInformationMinSize = 36;

bool ParseStandardInformation(const std::vector<uint8_t>& buffer, size_t valueOffset, size_t valueLength, ParsedMftRecord& out) {
    if (valueLength < kStandardInformationMinSize) {
        return false;
    }
    uint64_t lastModificationTime = 0;
    uint32_t dosAttributes = 0;
    if (!ReadU64(buffer, valueOffset + 8, lastModificationTime)) return false;
    if (!ReadU32(buffer, valueOffset + 32, dosAttributes)) return false;

    out.hasStandardInformation = true;
    out.lastWriteTime = lastModificationTime;
    out.dosAttributes = dosAttributes;
    return true;
}

// $FILE_NAME's fixed-layout prefix: ParentDirectory FRN, 4 duplicated
// FILETIMEs, AllocatedSize, RealSize, Flags, NameLength, Namespace, then
// NameLength UTF-16 code units.
constexpr size_t kFileNameFixedSize = 66;

bool ParseFileNameAttribute(const std::vector<uint8_t>& buffer, size_t valueOffset, size_t valueLength,
                             uint64_t& outParentFrn, uint64_t& outRealSize, uint8_t& outNamespace, std::u16string& outName) {
    if (valueLength < kFileNameFixedSize) {
        return false;
    }
    if (!ReadU64(buffer, valueOffset + 0, outParentFrn)) return false;
    if (!ReadU64(buffer, valueOffset + 48, outRealSize)) return false;

    uint8_t nameLengthChars = 0;
    if (!ReadU8(buffer, valueOffset + 64, nameLengthChars)) return false;
    if (!ReadU8(buffer, valueOffset + 65, outNamespace)) return false;

    const size_t nameBytes = static_cast<size_t>(nameLengthChars) * sizeof(char16_t);
    if (kFileNameFixedSize + nameBytes > valueLength) {
        return false; // declared name length runs past this attribute's resident value
    }
    if (nameLengthChars == 0) {
        return false; // an empty name is never valid for a non-root $FILE_NAME
    }

    outName.resize(nameLengthChars);
    std::memcpy(outName.data(), buffer.data() + valueOffset + kFileNameFixedSize, nameBytes);
    return true;
}

} // namespace

std::optional<ParsedMftRecord> ParseMftRecord(const std::vector<uint8_t>& recordBufferIn, uint32_t bytesPerSector) {
    std::vector<uint8_t> buffer = recordBufferIn; // fixup mutates in place; caller's copy is left untouched

    uint32_t magic = 0;
    if (!ReadU32(buffer, 0, magic) || magic != kFileRecordMagic) {
        return std::nullopt; // not a valid ("FILE"-signed) record, or a free/unused slot
    }

    if (!ApplyUpdateSequenceFixup(buffer, bytesPerSector)) {
        return std::nullopt;
    }

    uint16_t recordFlags = 0;
    uint16_t firstAttributeOffset = 0;
    uint32_t usedSize = 0;
    if (!ReadU16(buffer, 22, recordFlags) || !ReadU16(buffer, 20, firstAttributeOffset) || !ReadU32(buffer, 24, usedSize)) {
        return std::nullopt;
    }
    if ((recordFlags & kRecordHeaderFlagInUse) == 0) {
        return std::nullopt; // deallocated MFT slot -- nothing to report
    }
    if (usedSize > buffer.size() || firstAttributeOffset >= usedSize) {
        return std::nullopt;
    }

    ParsedMftRecord result;
    result.isDirectory = (recordFlags & kRecordHeaderFlagDirectory) != 0;

    // $FILE_NAME candidate selection (see header comment): prefer a
    // non-DOS-namespace name over a DOS-only one, first-seen otherwise.
    bool haveNonDosName = false;

    size_t offset = firstAttributeOffset;
    // Bounded by the record's own used size, which is itself bounded by
    // the buffer -- this loop always terminates even on adversarial input,
    // since every iteration either advances by a validated recordLength
    // >= 16 or returns.
    while (offset + 4 <= usedSize) {
        AttributeHeader header;
        if (!ReadAttributeHeader(buffer, offset, header)) {
            return std::nullopt; // malformed attribute header -- isolate the whole record (task 4.3)
        }
        if (header.typeCode == kAttributeTypeEnd) {
            break;
        }

        // Allowlist enforced right here: every attribute type other than
        // $STANDARD_INFORMATION/$FILE_NAME is skipped using only its own
        // declared header length -- its payload (including $DATA, even
        // small-file resident content) is never read (spec "MFT Attribute
        // Allowlisting Enforced at the Parser").
        if (!header.nonResident) {
            if (header.typeCode == kAttributeTypeStandardInformation && !result.hasStandardInformation) {
                // A malformed $STANDARD_INFORMATION isolates the record --
                // every real record has exactly one, so a broken one means
                // the record itself is inconsistent.
                if (!ParseStandardInformation(buffer, offset + header.valueOffset, header.valueLength, result)) {
                    return std::nullopt;
                }
            } else if (header.typeCode == kAttributeTypeFileName && !haveNonDosName) {
                uint64_t parentFrn = 0, realSize = 0;
                uint8_t nameNamespace = 0;
                std::u16string name;
                if (ParseFileNameAttribute(buffer, offset + header.valueOffset, header.valueLength, parentFrn, realSize,
                                            nameNamespace, name)) {
                    const bool isDosOnly = nameNamespace == kFileNameNamespaceDos;
                    if (!result.hasFileName || (isDosOnly == false && haveNonDosName == false)) {
                        result.hasFileName = true;
                        result.parentFileReferenceNumber = parentFrn;
                        result.realSizeBytes = realSize;
                        result.name = std::move(name);
                        haveNonDosName = !isDosOnly;
                    }
                }
                // An individual malformed $FILE_NAME does not reject the
                // whole record on its own -- a record with one bad name
                // attribute alongside a good one should still surface the
                // good one; if none ever parses, hasFileName stays false
                // and the caller (VolumeScanner/UsnJournalReader) treats
                // that as "skip this record".
            }
        }

        offset += header.recordLength;
    }

    if (!result.hasStandardInformation || !result.hasFileName) {
        return std::nullopt;
    }
    return result;
}

} // namespace ffindexsvc
