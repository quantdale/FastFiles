#pragma once
#include <windows.h>
#include <cstdint>
#include <optional>

#include "MftRecordParser.h"

namespace ffindexsvc {

// RAII raw volume handle, opened with SeBackupPrivilege-granted access
// (design.md D4) -- shared by VolumeScanner and UsnJournalReader so both
// go through one, carefully-reviewed open path.
class RawVolumeHandle {
public:
    static std::optional<RawVolumeHandle> Open(wchar_t driveLetter);

    RawVolumeHandle(RawVolumeHandle&& other) noexcept;
    RawVolumeHandle& operator=(RawVolumeHandle&& other) noexcept;
    RawVolumeHandle(const RawVolumeHandle&) = delete;
    RawVolumeHandle& operator=(const RawVolumeHandle&) = delete;
    ~RawVolumeHandle();

    HANDLE Get() const noexcept { return handle_; }
    uint32_t BytesPerSector() const noexcept { return bytesPerSector_; }

private:
    RawVolumeHandle() = default;

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    uint32_t bytesPerSector_ = 512;
};

// Fetches the CURRENT raw MFT record for `fileReferenceNumber` via
// FSCTL_GET_NTFS_FILE_RECORD (never opens the target file itself, so this
// works even against a file exclusively locked by another process -- task
// 4.6) and runs it through MftRecordParser.
//
// FSCTL_GET_NTFS_FILE_RECORD is documented to potentially return the
// record for a different, nearby FRN if the exact one requested is no
// longer a valid record (e.g. deleted and the MFT slot not yet reused, or
// reused with a different sequence number) -- this function verifies the
// returned FileReferenceNumber matches what was requested and returns
// std::nullopt on any mismatch, exactly like any other malformed/
// inconsistent record (task 4.3's skip-and-continue applies at the
// caller, not here).
std::optional<ParsedMftRecord> FetchAndParseMftRecord(HANDLE volumeHandle, uint32_t bytesPerSector, uint64_t fileReferenceNumber);

} // namespace ffindexsvc
