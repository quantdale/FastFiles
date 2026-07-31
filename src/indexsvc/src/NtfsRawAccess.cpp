#include "NtfsRawAccess.h"

#include <winioctl.h>

#include <utility>
#include <vector>

#include "PrivilegeVerification.h"

namespace ffindexsvc {

namespace {

uint32_t QueryBytesPerSector(wchar_t driveLetter) {
    wchar_t rootPath[] = {driveLetter, L':', L'\\', L'\0'};
    DWORD sectorsPerCluster = 0, bytesPerSector = 512, freeClusters = 0, totalClusters = 0;
    if (!GetDiskFreeSpaceW(rootPath, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters)) {
        return 512; // conservative, universally-safe default
    }
    return bytesPerSector;
}

} // namespace

std::optional<RawVolumeHandle> RawVolumeHandle::Open(wchar_t driveLetter) {
    // Task 7.1's finding, applied for real here: SeBackupPrivilege must be
    // explicitly enabled (not just held) for FILE_FLAG_BACKUP_SEMANTICS to
    // bypass the normal ACL check on a raw volume open.
    if (!EnsureBackupPrivilegeEnabled()) {
        return std::nullopt;
    }

    wchar_t volumePath[] = {L'\\', L'\\', L'.', L'\\', driveLetter, L':', L'\0'};
    HANDLE handle = CreateFileW(
        volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    RawVolumeHandle result;
    result.handle_ = handle;
    result.bytesPerSector_ = QueryBytesPerSector(driveLetter);
    return result;
}

RawVolumeHandle::RawVolumeHandle(RawVolumeHandle&& other) noexcept
    : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)), bytesPerSector_(other.bytesPerSector_) {}

RawVolumeHandle& RawVolumeHandle::operator=(RawVolumeHandle&& other) noexcept {
    if (this != &other) {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        bytesPerSector_ = other.bytesPerSector_;
    }
    return *this;
}

RawVolumeHandle::~RawVolumeHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
    }
}

std::optional<ParsedMftRecord> FetchAndParseMftRecord(HANDLE volumeHandle, uint32_t bytesPerSector, uint64_t fileReferenceNumber) {
    NTFS_FILE_RECORD_INPUT_BUFFER input{};
    input.FileReferenceNumber.QuadPart = static_cast<LONGLONG>(fileReferenceNumber);

    // Real NTFS MFT record sizes are 1024 bytes in the overwhelming
    // majority of deployments (occasionally up to 4096); this covers both
    // with headroom rather than querying the boot sector for the exact
    // value.
    constexpr DWORD kMaxRecordBytes = 4096;
    std::vector<uint8_t> outputBuffer(sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) - 1 + kMaxRecordBytes);
    auto* output = reinterpret_cast<NTFS_FILE_RECORD_OUTPUT_BUFFER*>(outputBuffer.data());

    DWORD bytesReturned = 0;
    const BOOL ok = DeviceIoControl(
        volumeHandle, FSCTL_GET_NTFS_FILE_RECORD, &input, sizeof(input), output,
        static_cast<DWORD>(outputBuffer.size()), &bytesReturned, nullptr);
    if (!ok) {
        return std::nullopt; // file no longer exists, or the FRN is stale
    }

    // The FSCTL can return a different, nearby record if the exact FRN
    // (specifically its sequence number) is no longer valid -- verify
    // before trusting anything about the returned bytes.
    if (static_cast<uint64_t>(output->FileReferenceNumber.QuadPart) != fileReferenceNumber) {
        return std::nullopt;
    }
    if (output->FileRecordLength == 0 || output->FileRecordLength > kMaxRecordBytes) {
        return std::nullopt;
    }

    const uint8_t* recordStart = reinterpret_cast<const uint8_t*>(&output->FileRecordBuffer[0]);
    std::vector<uint8_t> recordBuffer(recordStart, recordStart + output->FileRecordLength);
    return ParseMftRecord(recordBuffer, bytesPerSector);
}

} // namespace ffindexsvc
