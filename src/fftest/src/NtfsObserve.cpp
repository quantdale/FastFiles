#include "NtfsObserve.h"

#include <winioctl.h>

#include <cstdio>
#include <cstring>

#include "MftParser.h"
#include "PrivilegeVerification.h"
#include "UsnRecordParser.h"
#include "VolumeEnumeration.h"

namespace ffntfsobserve {

namespace {

constexpr uint64_t kMftSegmentNumberMask = 0x0000FFFFFFFFFFFFull;
constexpr uint64_t kRootDirectorySegment = 5;
constexpr size_t kEnumBufferSize = 64 * 1024;
constexpr size_t kJournalBufferSize = 64 * 1024;
constexpr size_t kMaxParentDepth = 64;
constexpr size_t kMaxSemanticsRecords = 16;
constexpr size_t kMaxJournalBuffers = 128;
constexpr DWORD kProbeObserved = 0;
constexpr DWORD kProbeFailed = 1;
constexpr DWORD kProbeBlocked = 2;

class HandleGuard {
public:
    explicit HandleGuard(HANDLE handle) : handle_(handle) {}
    ~HandleGuard() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;
    HandleGuard(HandleGuard&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    HandleGuard& operator=(HandleGuard&& other) noexcept {
        if (this != &other) {
            if (handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    HANDLE get() const { return handle_; }
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE handle_;
};

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::string JsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 16);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20) {
                char escaped[7];
                sprintf_s(escaped, "\\u%04x", character);
                result += escaped;
            } else {
                result += static_cast<char>(character);
            }
        }
    }
    return result;
}

std::wstring U16ToWide(std::u16string_view value) {
    std::wstring result;
    result.reserve(value.size());
    for (const char16_t c : value) {
        result.push_back(static_cast<wchar_t>(c));
    }
    return result;
}

const char* JsonBool(bool value) { return value ? "true" : "false"; }

// Production record fetch: FSCTL_GET_NTFS_FILE_RECORD + segment check +
// scratch-copy fixup + allowlisted parse (mirrors VolumeScanner.cpp).
// Returns nullopt for unreadable/stale/malformed records (skip, not fatal).
std::optional<ffindexsvc::ParsedMftRecord> FetchParsedRecord(HANDLE volumeHandle,
                                                             const std::vector<uint8_t>& recordTemplate,
                                                             uint32_t recordSize, uint32_t bytesPerSector,
                                                             uint64_t fileReferenceNumber,
                                                             uint64_t* outRequestedFrn) {
    std::vector<uint8_t> outputBuffer(recordTemplate.size());
    NTFS_FILE_RECORD_INPUT_BUFFER input{};
    input.FileReferenceNumber.QuadPart = static_cast<LONGLONG>(fileReferenceNumber);
    DWORD recordBytesReturned = 0;
    if (!DeviceIoControl(volumeHandle, FSCTL_GET_NTFS_FILE_RECORD, &input, sizeof(input),
                         outputBuffer.data(), static_cast<DWORD>(outputBuffer.size()),
                         &recordBytesReturned, nullptr)) {
        return std::nullopt;
    }
    const auto* output =
        reinterpret_cast<const NTFS_FILE_RECORD_OUTPUT_BUFFER*>(outputBuffer.data());
    if ((static_cast<uint64_t>(output->FileReferenceNumber.QuadPart) & kMftSegmentNumberMask) !=
        (fileReferenceNumber & kMftSegmentNumberMask)) {
        return std::nullopt;
    }
    const uint32_t returnedLength = output->FileRecordLength;
    if (returnedLength == 0 || returnedLength > recordSize) {
        return std::nullopt;
    }
    std::vector<uint8_t> scratch(recordSize);
    std::memcpy(scratch.data(), output->FileRecordBuffer, returnedLength);
    if (ffindexsvc::ApplyFixupAndValidate(scratch.data(), returnedLength, bytesPerSector) !=
        ffindexsvc::FixupResult::Ok) {
        return std::nullopt;
    }
    auto parsed = ffindexsvc::ParseMftAttributes(scratch.data(), returnedLength);
    if (!parsed) {
        return std::nullopt;
    }
    parsed->fileReferenceNumber = fileReferenceNumber;
    if (outRequestedFrn != nullptr) {
        *outRequestedFrn = fileReferenceNumber;
    }
    return parsed;
}

// Walks parent references to the volume root building "X:\a\b".
// Each step is one production record fetch; unresolvable chains yield "".
std::wstring BuildFullPath(HANDLE volumeHandle, const std::vector<uint8_t>& recordTemplate,
                           uint32_t recordSize, uint32_t bytesPerSector, wchar_t driveLetter,
                           uint64_t fileReferenceNumber, const std::wstring& leafName) {
    std::vector<std::wstring> names;
    names.push_back(leafName);
    uint64_t cursor = fileReferenceNumber;
    for (size_t depth = 0; depth < kMaxParentDepth; ++depth) {
        auto rec = FetchParsedRecord(volumeHandle, recordTemplate, recordSize, bytesPerSector,
                                     cursor, nullptr);
        if (!rec) {
            return L"";
        }
        const uint64_t parent = rec->parentFileReferenceNumber;
        if ((cursor & kMftSegmentNumberMask) == kRootDirectorySegment) {
            break;
        }
        if (parent == 0 || parent == cursor) {
            return L"";
        }
        auto parentRec = FetchParsedRecord(volumeHandle, recordTemplate, recordSize,
                                           bytesPerSector, parent, nullptr);
        if (!parentRec) {
            return L"";
        }
        const std::wstring parentName = U16ToWide(parentRec->fileName);
        cursor = parent;
        if ((cursor & kMftSegmentNumberMask) == kRootDirectorySegment) {
            break;
        }
        if (parentName.empty() || parentName == L".") {
            return L"";
        }
        names.push_back(parentName);
    }
    if ((cursor & kMftSegmentNumberMask) != kRootDirectorySegment) {
        return L"";
    }
    std::vector<std::wstring> rootFirst(names.rbegin(), names.rend());
    return JoinParentPath(driveLetter, rootFirst);
}

void PrintObservedRecord(const ObservedRecord& record, bool first) {
    std::printf("%s{\"name\":\"%s\",\"size\":%llu,\"parentPath\":\"%s\",\"fileId\":\"\",\"found\":%s}",
                first ? "" : ",", JsonEscape(WideToUtf8(record.name)).c_str(),
                static_cast<unsigned long long>(record.sizeBytes),
                JsonEscape(WideToUtf8(record.parentPath)).c_str(), JsonBool(record.found));
}

bool IsNtfsVolume(wchar_t driveLetter, std::wstring* fileSystemNameOut) {
    const wchar_t rootPath[] = {driveLetter, L':', L'\\', L'\0'};
    wchar_t fsName[MAX_PATH + 1] = {};
    if (!GetVolumeInformationW(rootPath, nullptr, 0, nullptr, nullptr, nullptr, fsName,
                               static_cast<DWORD>(std::size(fsName)))) {
        return false;
    }
    if (fileSystemNameOut != nullptr) {
        *fileSystemNameOut = fsName;
    }
    return _wcsicmp(fsName, L"NTFS") == 0;
}

} // namespace

const char* ObserveStatusName(ObserveStatus status) {
    switch (status) {
    case ObserveStatus::Observed: return "OBSERVED";
    case ObserveStatus::Blocked: return "BLOCKED";
    case ObserveStatus::Failed: return "FAILED";
    }
    return "FAILED";
}

ObserveStatus ClassifyObserveStatus(bool volumeOpened, DWORD volumeOpenError, bool isNtfs,
                                    bool enumOk, size_t parsedRecords, bool journalQueried) {
    if (!volumeOpened) {
        (void)volumeOpenError;
        return ObserveStatus::Blocked;
    }
    if (!isNtfs) {
        return ObserveStatus::Blocked;
    }
    if (!enumOk || parsedRecords == 0 || !journalQueried) {
        return ObserveStatus::Failed;
    }
    return ObserveStatus::Observed;
}

std::optional<uint64_t> ParseHexU64(const std::wstring& text) {
    if (text.empty() || text.size() > 20) {
        return std::nullopt;
    }
    size_t index = 0;
    int base = 10;
    if (text.size() > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        index = 2;
        base = 16;
    }
    if (index >= text.size()) {
        return std::nullopt;
    }
    uint64_t value = 0;
    for (; index < text.size(); ++index) {
        const wchar_t c = text[index];
        unsigned digit = 0;
        if (c >= L'0' && c <= L'9') {
            digit = static_cast<unsigned>(c - L'0');
        } else if (base == 16 && c >= L'a' && c <= L'f') {
            digit = static_cast<unsigned>(c - L'a') + 10;
        } else if (base == 16 && c >= L'A' && c <= L'F') {
            digit = static_cast<unsigned>(c - L'A') + 10;
        } else {
            return std::nullopt;
        }
        if (digit >= static_cast<unsigned>(base)) {
            return std::nullopt;
        }
        if (value > (UINT64_MAX - digit) / static_cast<uint64_t>(base)) {
            return std::nullopt;
        }
        value = value * static_cast<uint64_t>(base) + digit;
    }
    return value;
}

std::string FormatHexU64(uint64_t value) {
    char buffer[19];
    sprintf_s(buffer, "0x%llX", static_cast<unsigned long long>(value));
    return buffer;
}

std::wstring JoinParentPath(wchar_t driveLetter, const std::vector<std::wstring>& names) {
    std::wstring path;
    path.push_back(driveLetter);
    path.append(L":\\");
    for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            path.push_back(L'\\');
        }
        path.append(names[i]);
    }
    return path;
}

size_t WalkEnumBuffer(const uint8_t* buffer, size_t bufferSize,
                      const std::function<void(uint64_t fileReferenceNumber)>& callback) {
    if (buffer == nullptr || bufferSize < sizeof(uint64_t)) {
        return 0;
    }
    size_t visited = 0;
    size_t offset = sizeof(uint64_t);
    while (offset + sizeof(USN_RECORD_V2) <= bufferSize) {
        const auto* entry = reinterpret_cast<const USN_RECORD_V2*>(buffer + offset);
        const DWORD recordLength = entry->RecordLength;
        if (recordLength == 0 || static_cast<size_t>(recordLength) > bufferSize - offset) {
            break;
        }
        callback(static_cast<uint64_t>(entry->FileReferenceNumber));
        ++visited;
        offset += recordLength;
    }
    return visited;
}

namespace {

struct OpenVolumeResult {
    HandleGuard handle{INVALID_HANDLE_VALUE};
    DWORD openError = ERROR_SUCCESS;
};

OpenVolumeResult OpenRawVolume(wchar_t driveLetter) {
    ffindexsvc::EnsureBackupPrivilegeEnabled();
    const wchar_t volumePath[] = {L'\\', L'\\', L'.', L'\\', driveLetter, L':', L'\0'};
    HANDLE raw = CreateFileW(volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    OpenVolumeResult result{HandleGuard(raw)};
    if (!result.handle.valid()) {
        result.openError = GetLastError();
    }
    return result;
}

struct SemanticsEvidence {
    bool volumeOpened = false;
    DWORD volumeOpenError = ERROR_SUCCESS;
    bool isNtfs = false;
    std::wstring filesystem;
    bool volumeDataOk = false;
    uint32_t bytesPerSector = 0;
    uint32_t bytesPerRecord = 0;
    unsigned long long volumeSerial = 0;
    unsigned long long mftValidDataLength = 0;
    bool enumOk = false;
    DWORD enumError = ERROR_SUCCESS;
    size_t enumRecords = 0;
    uint64_t firstFileReference = 0;
    size_t parsedRecords = 0;
    ObservedRecord firstParsed;
    bool journalQueried = false;
    DWORD journalQueryError = ERROR_SUCCESS;
    uint64_t journalId = 0;
    uint64_t nextUsn = 0;
    bool journalRead = false;
    DWORD journalReadError = ERROR_SUCCESS;
    unsigned long journalReadBytes = 0;
    size_t journalDecodedRecords = 0;
    std::string blockReason;
};

SemanticsEvidence CollectSemantics(HANDLE volumeHandle, bool isNtfs, wchar_t driveLetter) {
    SemanticsEvidence ev;
    ev.isNtfs = isNtfs;
    NTFS_VOLUME_DATA_BUFFER volumeData{};
    DWORD bytesReturned = 0;
    ev.volumeDataOk = DeviceIoControl(volumeHandle, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0,
                                      &volumeData, sizeof(volumeData), &bytesReturned,
                                      nullptr) != FALSE;
    if (!ev.volumeDataOk) {
        return ev;
    }
    ev.bytesPerSector = static_cast<uint32_t>(volumeData.BytesPerSector);
    ev.bytesPerRecord = static_cast<uint32_t>(volumeData.BytesPerFileRecordSegment);
    ev.volumeSerial = static_cast<unsigned long long>(volumeData.VolumeSerialNumber.QuadPart);
    ev.mftValidDataLength =
        static_cast<unsigned long long>(volumeData.MftValidDataLength.QuadPart);
    if (ev.bytesPerSector == 0 || ev.bytesPerRecord == 0) {
        ev.volumeDataOk = false;
        return ev;
    }

    std::vector<uint8_t> enumBuffer(kEnumBufferSize);
    MFT_ENUM_DATA_V0 enumData{};
    enumData.StartFileReferenceNumber = 0;
    enumData.LowUsn = 0;
    enumData.HighUsn = MAXLONGLONG;
    DWORD enumBytesReturned = 0;
    ev.enumOk = DeviceIoControl(volumeHandle, FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData),
                                enumBuffer.data(), static_cast<DWORD>(enumBuffer.size()),
                                &enumBytesReturned, nullptr) != FALSE;
    if (!ev.enumOk) {
        ev.enumError = GetLastError();
        return ev;
    }
    std::vector<uint64_t> fileReferences;
    ev.enumRecords = WalkEnumBuffer(enumBuffer.data(), enumBytesReturned,
                                    [&](uint64_t frn) { fileReferences.push_back(frn); });
    if (!fileReferences.empty()) {
        ev.firstFileReference = fileReferences.front();
    }

    const size_t recordTemplateSize =
        sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) - 1 + ev.bytesPerRecord;
    std::vector<uint8_t> recordTemplate(recordTemplateSize);
    for (size_t i = 0; i < fileReferences.size() && ev.parsedRecords < kMaxSemanticsRecords;
         ++i) {
        uint64_t requested = 0;
        auto parsed = FetchParsedRecord(volumeHandle, recordTemplate, ev.bytesPerRecord,
                                        ev.bytesPerSector, fileReferences[i], &requested);
        if (!parsed) {
            continue;
        }
        ++ev.parsedRecords;
        if (ev.parsedRecords == 1) {
            ev.firstParsed.name = U16ToWide(parsed->fileName);
            ev.firstParsed.sizeBytes = parsed->sizeBytes;
            ev.firstParsed.fileReferenceNumber = requested;
            ev.firstParsed.found = true;
            ev.firstParsed.parentPath =
                BuildFullPath(volumeHandle, recordTemplate, ev.bytesPerRecord,
                              ev.bytesPerSector, driveLetter, requested, ev.firstParsed.name);
        }
    }

    USN_JOURNAL_DATA_V0 journalData{};
    DWORD journalBytes = 0;
    ev.journalQueried =
        DeviceIoControl(volumeHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journalData,
                        sizeof(journalData), &journalBytes, nullptr) != FALSE;
    if (!ev.journalQueried) {
        ev.journalQueryError = GetLastError();
        return ev;
    }
    ev.journalId = static_cast<uint64_t>(journalData.UsnJournalID);
    ev.nextUsn = static_cast<uint64_t>(journalData.NextUsn);

    READ_USN_JOURNAL_DATA_V0 readRequest{};
    readRequest.StartUsn = journalData.NextUsn;
    readRequest.ReasonMask = 0xFFFFFFFF;
    readRequest.ReturnOnlyOnClose = FALSE;
    readRequest.Timeout = 0;
    readRequest.BytesToWaitFor = 0;
    readRequest.UsnJournalID = journalData.UsnJournalID;
    std::vector<uint8_t> readBuffer(kJournalBufferSize);
    DWORD readBytesReturned = 0;
    ev.journalRead =
        DeviceIoControl(volumeHandle, FSCTL_READ_USN_JOURNAL, &readRequest, sizeof(readRequest),
                        readBuffer.data(), static_cast<DWORD>(readBuffer.size()),
                        &readBytesReturned, nullptr) != FALSE;
    if (!ev.journalRead) {
        ev.journalReadError = GetLastError();
        return ev;
    }
    ev.journalReadBytes = static_cast<unsigned long>(readBytesReturned);
    size_t offset = sizeof(USN);
    while (offset + sizeof(USN_RECORD_V2) <= readBytesReturned) {
        const size_t remaining = readBytesReturned - offset;
        auto decoded = ffindexsvc::DecodeUsnRecord(readBuffer.data() + offset, remaining);
        if (!decoded) {
            break;
        }
        ++ev.journalDecodedRecords;
        const DWORD recordLength =
            *reinterpret_cast<const DWORD*>(readBuffer.data() + offset);
        if (recordLength == 0 || recordLength > remaining) {
            break;
        }
        offset += recordLength;
    }
    return ev;
}

void PrintSemanticsJson(wchar_t driveLetter, ObserveStatus status, const SemanticsEvidence& ev) {
    std::printf("{\"tool\":\"fftest\",\"probe\":\"ntfs-observe\",\"mode\":\"semantics\","
                "\"status\":\"%s\",\"accessMode\":\"read-only\","
                "\"requestedAccess\":[\"GENERIC_READ\"],\"mutationOperations\":0,",
                ObserveStatusName(status));
    std::printf("\"driveLetter\":\"%c\",\"filesystem\":\"%s\",",
                static_cast<char>(driveLetter),
                JsonEscape(WideToUtf8(ev.filesystem)).c_str());
    std::printf("\"volumeOpened\":%s,\"volumeOpenError\":%lu,",
                JsonBool(ev.volumeOpened), ev.volumeOpenError);
    std::printf("\"volumeDataOk\":%s,\"bytesPerSector\":%u,\"bytesPerRecord\":%u,",
                JsonBool(ev.volumeDataOk), ev.bytesPerSector, ev.bytesPerRecord);
    std::printf("\"volumeSerial\":\"%s\",\"mftValidDataLength\":%llu,",
                FormatHexU64(ev.volumeSerial).c_str(), ev.mftValidDataLength);
    std::printf("\"enumOk\":%s,\"enumError\":%lu,\"enumRecords\":%llu,\"firstFileReference\":\"%s\",",
                JsonBool(ev.enumOk), ev.enumError,
                static_cast<unsigned long long>(ev.enumRecords),
                FormatHexU64(ev.firstFileReference).c_str());
    std::printf("\"parsedRecords\":%llu,\"parsedName\":\"%s\",\"parsedSize\":%llu,"
                "\"parsedFileReference\":\"%s\",\"parsedParentPath\":\"%s\",",
                static_cast<unsigned long long>(ev.parsedRecords),
                JsonEscape(WideToUtf8(ev.firstParsed.name)).c_str(),
                static_cast<unsigned long long>(ev.firstParsed.sizeBytes),
                FormatHexU64(ev.firstParsed.fileReferenceNumber).c_str(),
                JsonEscape(WideToUtf8(ev.firstParsed.parentPath)).c_str());
    std::printf("\"journalQueried\":%s,\"journalQueryError\":%lu,\"journalId\":\"%s\","
                "\"nextUsn\":\"%s\",",
                JsonBool(ev.journalQueried), ev.journalQueryError,
                FormatHexU64(ev.journalId).c_str(), FormatHexU64(ev.nextUsn).c_str());
    std::printf("\"journalRead\":%s,\"journalReadError\":%lu,\"journalReadBytes\":%lu,"
                "\"journalDecodedRecords\":%llu,",
                JsonBool(ev.journalRead), ev.journalReadError, ev.journalReadBytes,
                static_cast<unsigned long long>(ev.journalDecodedRecords));
    std::printf("\"fileIdOmitted\":\"normal-filesystem FileId coupling avoided; "
                "Name/Size/ParentPath enforced; targeted fetch proves FRN identity\",");
    std::printf("\"reason\":\"%s\"}\n", JsonEscape(ev.blockReason).c_str());
}

int RunSemantics(wchar_t driveLetter) {
    SemanticsEvidence ev;
    std::wstring fsName;
    if (!IsNtfsVolume(driveLetter, &fsName)) {
        ev.filesystem = fsName;
        ev.blockReason = IsNtfsVolume(driveLetter, nullptr)
                             ? "unexpected filesystem re-query failure"
                             : "volume is not NTFS or not queryable";
        PrintSemanticsJson(driveLetter, ObserveStatus::Blocked, ev);
        return kProbeBlocked;
    }
    ev.filesystem = fsName;
    ev.isNtfs = true;
    auto opened = OpenRawVolume(driveLetter);
    ev.volumeOpened = opened.handle.valid();
    ev.volumeOpenError = opened.openError;
    if (!ev.volumeOpened) {
        ev.blockReason = (ev.volumeOpenError == ERROR_ACCESS_DENIED)
                             ? "raw volume open denied (elevation required for production observer)"
                             : "raw volume open failed";
        PrintSemanticsJson(driveLetter, ObserveStatus::Blocked, ev);
        return kProbeBlocked;
    }
    ev = CollectSemantics(opened.handle.get(), true, driveLetter);
    ev.filesystem = fsName;
    ev.volumeOpened = true;
    const ObserveStatus status =
        ClassifyObserveStatus(true, ERROR_SUCCESS, true, ev.enumOk, ev.parsedRecords,
                              ev.journalQueried);
    if (!ev.volumeDataOk) {
        ev.blockReason = "NTFS volume metadata query failed on an opened volume";
    } else if (!ev.enumOk) {
        ev.blockReason = "MFT enumeration (FSCTL_ENUM_USN_DATA) failed on an opened volume";
    } else if (ev.parsedRecords == 0) {
        ev.blockReason = "MFT enumeration returned no parseable record";
    } else if (!ev.journalQueried) {
        ev.blockReason = "USN journal query failed on an opened volume";
    }
    PrintSemanticsJson(driveLetter, status, ev);
    return (status == ObserveStatus::Observed)   ? kProbeObserved
           : (status == ObserveStatus::Blocked) ? kProbeBlocked
                                                : kProbeFailed;
}

struct VolumeGeometry {
    uint32_t bytesPerSector = 0;
    uint32_t bytesPerRecord = 0;
    std::vector<uint8_t> recordTemplate;
};

std::optional<VolumeGeometry> QueryGeometry(HANDLE volumeHandle) {
    NTFS_VOLUME_DATA_BUFFER volumeData{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(volumeHandle, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &volumeData,
                         sizeof(volumeData), &bytesReturned, nullptr)) {
        return std::nullopt;
    }
    const uint32_t recordSize = static_cast<uint32_t>(volumeData.BytesPerFileRecordSegment);
    const uint32_t sectorSize = static_cast<uint32_t>(volumeData.BytesPerSector);
    if (recordSize == 0 || sectorSize == 0) {
        return std::nullopt;
    }
    VolumeGeometry geometry;
    geometry.bytesPerSector = sectorSize;
    geometry.bytesPerRecord = recordSize;
    geometry.recordTemplate.resize(sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) - 1 + recordSize);
    return geometry;
}

ObservedRecord ObserveOneFrn(HANDLE volumeHandle, const VolumeGeometry& geometry,
                             wchar_t driveLetter, uint64_t wantedFrn) {
    ObservedRecord record;
    record.fileReferenceNumber = wantedFrn;
    uint64_t requested = 0;
    auto parsed = FetchParsedRecord(volumeHandle, geometry.recordTemplate,
                                    geometry.bytesPerRecord, geometry.bytesPerSector, wantedFrn,
                                    &requested);
    if (!parsed) {
        return record;
    }
    record.name = U16ToWide(parsed->fileName);
    record.sizeBytes = parsed->sizeBytes;
    record.parentPath =
        BuildFullPath(volumeHandle, geometry.recordTemplate, geometry.bytesPerRecord,
                      geometry.bytesPerSector, driveLetter, requested, record.name);
    record.found = !record.name.empty() && !record.parentPath.empty();
    return record;
}

int RunFind(wchar_t driveLetter, int argc, wchar_t** argv) {
    std::vector<uint64_t> wanted;
    for (int i = 0; i < argc; ++i) {
        auto parsed = ParseHexU64(argv[i]);
        if (!parsed) {
            std::fprintf(stderr, "ntfs-observe: malformed file reference '%ls'\n", argv[i]);
            return kProbeFailed;
        }
        wanted.push_back(*parsed);
    }
    if (!IsNtfsVolume(driveLetter, nullptr)) {
        std::printf("{\"tool\":\"fftest\",\"probe\":\"ntfs-observe\",\"mode\":\"find\","
                    "\"status\":\"BLOCKED\",\"accessMode\":\"read-only\","
                    "\"requestedAccess\":[\"GENERIC_READ\"],\"mutationOperations\":0,"
                    "\"driveLetter\":\"%c\",\"reason\":\"volume is not NTFS or not queryable\","
                    "\"observed\":[]}\n",
                    static_cast<char>(driveLetter));
        return kProbeBlocked;
    }
    auto opened = OpenRawVolume(driveLetter);
    if (!opened.handle.valid()) {
        std::printf("{\"tool\":\"fftest\",\"probe\":\"ntfs-observe\",\"mode\":\"find\","
                    "\"status\":\"BLOCKED\",\"accessMode\":\"read-only\","
                    "\"requestedAccess\":[\"GENERIC_READ\"],\"mutationOperations\":0,"
                    "\"driveLetter\":\"%c\",\"volumeOpenError\":%lu,"
                    "\"reason\":\"raw volume open failed\",\"observed\":[]}\n",
                    static_cast<char>(driveLetter), opened.openError);
        return kProbeBlocked;
    }
    auto geometry = QueryGeometry(opened.handle.get());
    if (!geometry) {
        std::printf("{\"tool\":\"fftest\",\"probe\":\"ntfs-observe\",\"mode\":\"find\","
                    "\"status\":\"FAILED\",\"accessMode\":\"read-only\","
                    "\"requestedAccess\":[\"GENERIC_READ\"],\"mutationOperations\":0,"
                    "\"driveLetter\":\"%c\",\"reason\":\"volume metadata query failed\","
                    "\"observed\":[]}\n",
                    static_cast<char>(driveLetter));
        return kProbeFailed;
    }
    std::printf("{\"tool\":\"fftest\",\"probe\":\"ntfs-observe\",\"mode\":\"find\","
                "\"status\":\"OBSERVED\",\"accessMode\":\"read-only\","
                "\"requestedAccess\":[\"GENERIC_READ\"],\"mutationOperations\":0,"
                "\"driveLetter\":\"%c\",\"reason\":\"\",\"observed\":[",
                static_cast<char>(driveLetter));
    for (size_t i = 0; i < wanted.size(); ++i) {
        PrintObservedRecord(
            ObserveOneFrn(opened.handle.get(), *geometry, driveLetter, wanted[i]), i == 0);
    }
    std::printf("]}\n");
    return kProbeObserved;
}

std::wstring ExtractUsnName(const uint8_t* recordBytes, size_t recordSize) {
    auto decoded = ffindexsvc::DecodeUsnRecord(recordBytes, recordSize);
    if (!decoded) {
        return L"";
    }
    const size_t nameEnd =
        static_cast<size_t>(decoded->fileNameOffset) + decoded->fileNameLength;
    if (nameEnd > recordSize || (decoded->fileNameLength % sizeof(char16_t)) != 0) {
        return L"";
    }
    const size_t chars = decoded->fileNameLength / sizeof(char16_t);
    std::u16string name(chars, u'\0');
    std::memcpy(name.data(), recordBytes + decoded->fileNameOffset, decoded->fileNameLength);
    return U16ToWide(name);
}

int RunUsn(wchar_t driveLetter, uint64_t startUsn, int argc, wchar_t** argv) {
    std::vector<uint64_t> wanted;
    for (int i = 0; i < argc; ++i) {
        auto parsed = ParseHexU64(argv[i]);
        if (!parsed) {
            std::fprintf(stderr, "ntfs-observe: malformed file reference '%ls'\n", argv[i]);
            return kProbeFailed;
        }
        wanted.push_back(*parsed);
    }
    if (!IsNtfsVolume(driveLetter, nullptr)) {
        std::printf("{\"tool\":\"fftest\",\"probe\":\"ntfs-observe\",\"mode\":\"usn\","
                    "\"status\":\"BLOCKED\",\"accessMode\":\"read-only\","
                    "\"requestedAccess\":[\"GENERIC_READ\"],\"mutationOperations\":0,"
                    "\"driveLetter\":\"%c\",\"reason\":\"volume is not NTFS or not queryable\","
                    "\"observed\":[]}\n",
                    static_cast<char>(driveLetter));
        return kProbeBlocked;
    }
    auto opened = OpenRawVolume(driveLetter);
    if (!opened.handle.valid()) {
        std::printf("{\"tool\":\"fftest\",\"probe\":\"ntfs-observe\",\"mode\":\"usn\","
                    "\"status\":\"BLOCKED\",\"accessMode\":\"read-only\","
                    "\"requestedAccess\":[\"GENERIC_READ\"],\"mutationOperations\":0,"
                    "\"driveLetter\":\"%c\",\"volumeOpenError\":%lu,"
                    "\"reason\":\"raw volume open failed\",\"observed\":[]}\n",
                    static_cast<char>(driveLetter), opened.openError);
        return kProbeBlocked;
    }
    auto geometry = QueryGeometry(opened.handle.get());
    USN_JOURNAL_DATA_V0 journalData{};
    DWORD journalBytes = 0;
    const bool journalQueried =
        geometry &&
        DeviceIoControl(opened.handle.get(), FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journalData,
                        sizeof(journalData), &journalBytes, nullptr) != FALSE;
    if (!journalQueried) {
        const DWORD error = GetLastError();
        std::printf("{\"tool\":\"fftest\",\"probe\":\"ntfs-observe\",\"mode\":\"usn\","
                    "\"status\":\"FAILED\",\"accessMode\":\"read-only\","
                    "\"requestedAccess\":[\"GENERIC_READ\"],\"mutationOperations\":0,"
                    "\"driveLetter\":\"%c\",\"journalQueryError\":%lu,"
                    "\"reason\":\"journal query failed\",\"observed\":[]}\n",
                    static_cast<char>(driveLetter), error);
        return kProbeFailed;
    }
    const uint64_t journalId = static_cast<uint64_t>(journalData.UsnJournalID);
    const uint64_t endUsn = static_cast<uint64_t>(journalData.NextUsn);
    // Matched segments -> journal name (production trusts the MFT re-read
    // for sizes; the journal name cross-checks the match).
    std::vector<std::pair<uint64_t, std::wstring>> hits;
    uint64_t cursor = startUsn;
    std::string readFailure;
    for (size_t buffers = 0; buffers < kMaxJournalBuffers; ++buffers) {
        READ_USN_JOURNAL_DATA_V0 readRequest{};
        readRequest.StartUsn = static_cast<USN>(cursor);
        readRequest.ReasonMask = 0xFFFFFFFF;
        readRequest.ReturnOnlyOnClose = FALSE;
        readRequest.Timeout = 0;
        readRequest.BytesToWaitFor = 0;
        readRequest.UsnJournalID = journalData.UsnJournalID;
        std::vector<uint8_t> readBuffer(kJournalBufferSize);
        DWORD readBytesReturned = 0;
        if (!DeviceIoControl(opened.handle.get(), FSCTL_READ_USN_JOURNAL, &readRequest,
                             sizeof(readRequest), readBuffer.data(),
                             static_cast<DWORD>(readBuffer.size()), &readBytesReturned,
                             nullptr)) {
            const DWORD error = GetLastError();
            if (error == ERROR_JOURNAL_ENTRY_DELETED || error == ERROR_INVALID_PARAMETER) {
                readFailure = "resume range aged out of the journal";
            } else {
                readFailure = "journal read failed";
            }
            break;
        }
        if (readBytesReturned <= sizeof(USN)) {
            break;
        }
        const USN nextUsn =
            *reinterpret_cast<const USN*>(readBuffer.data());
        size_t offset = sizeof(USN);
        while (offset + sizeof(USN_RECORD_V2) <= readBytesReturned) {
            const size_t remaining = readBytesReturned - offset;
            const DWORD recordLength =
                *reinterpret_cast<const DWORD*>(readBuffer.data() + offset);
            if (recordLength < sizeof(USN_RECORD_V2) || recordLength > remaining) {
                break;
            }
            auto decoded =
                ffindexsvc::DecodeUsnRecord(readBuffer.data() + offset, remaining);
            if (decoded) {
                for (const uint64_t want : wanted) {
                    if ((decoded->fileReferenceNumber & kMftSegmentNumberMask) ==
                        (want & kMftSegmentNumberMask)) {
                        hits.emplace_back(decoded->fileReferenceNumber,
                                          ExtractUsnName(readBuffer.data() + offset,
                                                         remaining));
                        break;
                    }
                }
            }
            offset += recordLength;
        }
        cursor = static_cast<uint64_t>(nextUsn);
        if (cursor >= endUsn) {
            break;
        }
    }
    std::printf("{\"tool\":\"fftest\",\"probe\":\"ntfs-observe\",\"mode\":\"usn\","
                "\"status\":\"%s\",\"accessMode\":\"read-only\","
                "\"requestedAccess\":[\"GENERIC_READ\"],\"mutationOperations\":0,"
                "\"driveLetter\":\"%c\",\"journalId\":\"%s\",\"startUsn\":\"%s\","
                "\"endUsn\":\"%s\",\"reason\":\"%s\",\"observed\":[",
                readFailure.empty() ? "OBSERVED" : "FAILED", static_cast<char>(driveLetter),
                FormatHexU64(journalId).c_str(), FormatHexU64(startUsn).c_str(),
                FormatHexU64(endUsn).c_str(), JsonEscape(readFailure).c_str());
    bool first = true;
    for (const uint64_t want : wanted) {
        ObservedRecord record;
        record.fileReferenceNumber = want;
        for (const auto& hit : hits) {
            if ((hit.first & kMftSegmentNumberMask) == (want & kMftSegmentNumberMask)) {
                auto mft = ObserveOneFrn(opened.handle.get(), *geometry, driveLetter, hit.first);
                if (mft.found) {
                    record = mft;
                } else {
                    // Production fallback shape: journal name without MFT
                    // size (deleted between journal and re-read). Emitted
                    // honestly; the field comparison will FAIL truthfully.
                    record.name = hit.second;
                    record.sizeBytes = 0;
                    record.found = !record.name.empty();
                }
                break;
            }
        }
        PrintObservedRecord(record, first);
        first = false;
    }
    std::printf("]}\n");
    return readFailure.empty() ? kProbeObserved : kProbeFailed;
}

} // namespace

int RunNtfsObserveProbe(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: fftest ntfs-observe <L> <semantics|find|usn> [...]\n");
        return kProbeFailed;
    }
    const std::wstring letterArg = argv[0];
    if (letterArg.size() != 1 ||
        !((letterArg[0] >= L'A' && letterArg[0] <= L'Z') ||
          (letterArg[0] >= L'a' && letterArg[0] <= L'z'))) {
        std::fprintf(stderr, "ntfs-observe: drive letter must be a single A-Z character\n");
        return kProbeFailed;
    }
    const wchar_t driveLetter =
        (letterArg[0] >= L'a') ? static_cast<wchar_t>(letterArg[0] - L'a' + L'A') : letterArg[0];
    const std::wstring mode = argv[1];
    if (_wcsicmp(mode.c_str(), L"semantics") == 0 && argc == 2) {
        return RunSemantics(driveLetter);
    }
    if (_wcsicmp(mode.c_str(), L"find") == 0 && argc >= 3) {
        return RunFind(driveLetter, argc - 2, argv + 2);
    }
    if (_wcsicmp(mode.c_str(), L"usn") == 0 && argc >= 4) {
        auto startUsn = ParseHexU64(argv[2]);
        if (!startUsn) {
            std::fprintf(stderr, "ntfs-observe: malformed start USN '%ls'\n", argv[2]);
            return kProbeFailed;
        }
        return RunUsn(driveLetter, *startUsn, argc - 3, argv + 3);
    }
    std::fprintf(stderr, "usage: fftest ntfs-observe <L> <semantics|find|usn> [...]\n");
    return kProbeFailed;
}

} // namespace ffntfsobserve
