#include "UsnRecordParser.h"

#include <windows.h>
#include <winioctl.h>

#include <cstring>

namespace ffindexsvc {

namespace {

bool DecodeNtfsFileId(const FILE_ID_128& source, uint64_t& low) noexcept {
    uint64_t high = 0;
    std::memcpy(&low, &source, sizeof(low));
    std::memcpy(&high, reinterpret_cast<const uint8_t*>(&source) + sizeof(low), sizeof(high));
    return high == 0;
}

bool ValidateFilenameRange(uint16_t filenameOffset, uint16_t filenameLength, size_t recordLength,
                           size_t minimumFilenameOffset) noexcept {
    if (filenameOffset < minimumFilenameOffset || (filenameLength % sizeof(char16_t)) != 0) {
        return false;
    }
    return static_cast<size_t>(filenameOffset) + static_cast<size_t>(filenameLength) <= recordLength;
}

} // namespace

std::optional<DecodedUsnRecord> DecodeUsnRecord(const uint8_t* recordBytes, size_t recordSize) noexcept {
    if (recordBytes == nullptr || recordSize < sizeof(USN_RECORD_COMMON_HEADER)) {
        return std::nullopt;
    }

    USN_RECORD_COMMON_HEADER common{};
    std::memcpy(&common, recordBytes, sizeof(common));
    const size_t declaredRecordLength = static_cast<size_t>(common.RecordLength);
    if (declaredRecordLength < sizeof(common) || declaredRecordLength > recordSize) {
        return std::nullopt;
    }

    DecodedUsnRecord decoded{};
    size_t minimumFilenameOffset = 0;
    switch (common.MajorVersion) {
        case 2: {
            minimumFilenameOffset = offsetof(USN_RECORD_V2, FileName);
            if (declaredRecordLength < minimumFilenameOffset) {
                return std::nullopt;
            }
            USN_RECORD_V2 record{};
            std::memcpy(&record, recordBytes, minimumFilenameOffset);
            decoded.usn = static_cast<uint64_t>(record.Usn);
            decoded.fileReferenceNumber = record.FileReferenceNumber;
            decoded.parentFileReferenceNumber = record.ParentFileReferenceNumber;
            decoded.reason = record.Reason;
            decoded.timestamp = static_cast<uint64_t>(record.TimeStamp.QuadPart);
            decoded.fileAttributes = record.FileAttributes;
            decoded.fileNameLength = record.FileNameLength;
            decoded.fileNameOffset = record.FileNameOffset;
            break;
        }
        case 3: {
            minimumFilenameOffset = offsetof(USN_RECORD_V3, FileName);
            if (declaredRecordLength < minimumFilenameOffset) {
                return std::nullopt;
            }
            USN_RECORD_V3 record{};
            std::memcpy(&record, recordBytes, minimumFilenameOffset);
            if (!DecodeNtfsFileId(record.FileReferenceNumber, decoded.fileReferenceNumber)
                || !DecodeNtfsFileId(record.ParentFileReferenceNumber, decoded.parentFileReferenceNumber)) {
                return std::nullopt;
            }
            decoded.usn = static_cast<uint64_t>(record.Usn);
            decoded.reason = record.Reason;
            decoded.timestamp = static_cast<uint64_t>(record.TimeStamp.QuadPart);
            decoded.fileAttributes = record.FileAttributes;
            decoded.fileNameLength = record.FileNameLength;
            decoded.fileNameOffset = record.FileNameOffset;
            break;
        }
        default:
            return std::nullopt;
    }

    if (!ValidateFilenameRange(decoded.fileNameOffset, decoded.fileNameLength, declaredRecordLength,
                               minimumFilenameOffset)) {
        return std::nullopt;
    }
    return decoded;
}

} // namespace ffindexsvc
