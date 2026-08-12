#include <windows.h>
#include <winioctl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "UsnRecordParser.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

size_t AlignedRecordLength(size_t filenameOffset, size_t filenameBytes) {
    return (filenameOffset + filenameBytes + 7u) & ~size_t{7u};
}

std::vector<uint8_t> BuildV2Record(const std::u16string& name) {
    const size_t nameOffset = offsetof(USN_RECORD_V2, FileName);
    const size_t recordLength = AlignedRecordLength(nameOffset, name.size() * sizeof(char16_t));
    std::vector<uint8_t> bytes(recordLength);
    USN_RECORD_V2 record{};
    record.RecordLength = static_cast<DWORD>(recordLength);
    record.MajorVersion = 2;
    record.MinorVersion = 0;
    record.FileReferenceNumber = 0x0007000000123456ULL;
    record.ParentFileReferenceNumber = 0x0007000000000005ULL;
    record.Usn = 1234;
    record.TimeStamp.QuadPart = 5678;
    record.Reason = USN_REASON_FILE_CREATE;
    record.FileAttributes = FILE_ATTRIBUTE_ARCHIVE;
    record.FileNameLength = static_cast<WORD>(name.size() * sizeof(char16_t));
    record.FileNameOffset = static_cast<WORD>(nameOffset);
    std::memcpy(bytes.data(), &record, nameOffset);
    std::memcpy(bytes.data() + nameOffset, name.data(), name.size() * sizeof(char16_t));
    return bytes;
}

std::vector<uint8_t> BuildV3Record(const std::u16string& name, uint64_t fileIdLow, uint64_t fileIdHigh) {
    const size_t nameOffset = offsetof(USN_RECORD_V3, FileName);
    const size_t recordLength = AlignedRecordLength(nameOffset, name.size() * sizeof(char16_t));
    std::vector<uint8_t> bytes(recordLength);
    USN_RECORD_V3 record{};
    record.RecordLength = static_cast<DWORD>(recordLength);
    record.MajorVersion = 3;
    record.MinorVersion = 0;
    std::array<uint8_t, sizeof(FILE_ID_128)> fileId{};
    std::memcpy(fileId.data(), &fileIdLow, sizeof(fileIdLow));
    std::memcpy(fileId.data() + sizeof(fileIdLow), &fileIdHigh, sizeof(fileIdHigh));
    std::memcpy(&record.FileReferenceNumber, fileId.data(), fileId.size());
    std::memcpy(&record.ParentFileReferenceNumber, fileId.data(), fileId.size());
    record.Usn = 4321;
    record.TimeStamp.QuadPart = 8765;
    record.Reason = USN_REASON_FILE_CREATE | USN_REASON_CLOSE;
    record.FileAttributes = FILE_ATTRIBUTE_ARCHIVE;
    record.FileNameLength = static_cast<WORD>(name.size() * sizeof(char16_t));
    record.FileNameOffset = static_cast<WORD>(nameOffset);
    std::memcpy(bytes.data(), &record, nameOffset);
    std::memcpy(bytes.data() + nameOffset, name.data(), name.size() * sizeof(char16_t));
    return bytes;
}

void TestV2RecordDecodes() {
    const auto bytes = BuildV2Record(u"legacy.txt");
    const auto decoded = ffindexsvc::DecodeUsnRecord(bytes.data(), bytes.size());
    Check(decoded.has_value(), "a USN V2 record decodes");
    Check(decoded && decoded->usn == 1234 && decoded->fileReferenceNumber == 0x0007000000123456ULL,
          "V2 preserves the USN and 64-bit file reference");
    Check(decoded && decoded->parentFileReferenceNumber == 0x0007000000000005ULL
              && decoded->reason == USN_REASON_FILE_CREATE,
          "V2 preserves the parent reference and reason");
    Check(decoded && decoded->fileNameLength == 20 && decoded->fileNameOffset == offsetof(USN_RECORD_V2, FileName),
          "V2 preserves the UTF-16 filename range");
}

void TestV3RecordDecodesLowNtfsIds() {
    const auto bytes = BuildV3Record(u"modern.txt", 0x005300000009374CULL, 0);
    const auto decoded = ffindexsvc::DecodeUsnRecord(bytes.data(), bytes.size());
    Check(decoded.has_value(), "a USN V3 record decodes");
    Check(decoded && decoded->usn == 4321 && decoded->fileReferenceNumber == 0x005300000009374CULL,
          "V3 preserves the low 64-bit NTFS file reference");
    Check(decoded && decoded->parentFileReferenceNumber == 0x005300000009374CULL
              && decoded->reason == (USN_REASON_FILE_CREATE | USN_REASON_CLOSE),
          "V3 preserves the parent reference and reason");
    Check(decoded && decoded->fileNameLength == 20 && decoded->fileNameOffset == offsetof(USN_RECORD_V3, FileName),
          "V3 preserves the UTF-16 filename range");
}

void TestV3HighIdAndMalformedRangesReject() {
    const auto highId = BuildV3Record(u"not-ntfs.txt", 7, 1);
    Check(!ffindexsvc::DecodeUsnRecord(highId.data(), highId.size()).has_value(),
          "a V3 ID with a non-zero high half is rejected instead of truncated");

    auto malformed = BuildV3Record(u"bad.txt", 7, 0);
    auto* record = reinterpret_cast<USN_RECORD_V3*>(malformed.data());
    record->FileNameOffset = static_cast<WORD>(record->RecordLength - 1);
    Check(!ffindexsvc::DecodeUsnRecord(malformed.data(), malformed.size()).has_value(),
          "a filename range outside the declared record is rejected");

    record->FileNameOffset = static_cast<WORD>(offsetof(USN_RECORD_V3, FileName));
    record->FileNameLength = 3;
    Check(!ffindexsvc::DecodeUsnRecord(malformed.data(), malformed.size()).has_value(),
          "an odd UTF-16 filename byte count is rejected");
}

void TestUnsupportedVersionRejects() {
    auto bytes = BuildV2Record(u"unknown.txt");
    auto* record = reinterpret_cast<USN_RECORD_V2*>(bytes.data());
    record->MajorVersion = 4;
    Check(!ffindexsvc::DecodeUsnRecord(bytes.data(), bytes.size()).has_value(),
          "an unsupported USN record version is rejected");
}

} // namespace

int main() {
    TestV2RecordDecodes();
    TestV3RecordDecodesLowNtfsIds();
    TestV3HighIdAndMalformedRangesReject();
    TestUnsupportedVersionRejects();
    return FailureCount() == 0 ? 0 : 1;
}
