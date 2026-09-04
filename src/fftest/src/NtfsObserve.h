#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// ntfs-observe (certification closure FC-003/FC-004): bounded, read-only
// NTFS/MFT/USN observer probe for the privileged-NTFS certification lane.
//
// It reuses the exact production mechanisms FastFiles scans with -- the
// VolumeScanner raw-volume open sequence (GENERIC_READ + backup semantics),
// FSCTL_GET_NTFS_VOLUME_DATA / FSCTL_ENUM_USN_DATA /
// FSCTL_GET_NTFS_FILE_RECORD with the same bounds/segment checks, the
// production ffmftparser record parser, and the production
// FSCTL_QUERY_USN_JOURNAL / FSCTL_READ_USN_JOURNAL journal sequence -- but
// bounded (one enum buffer for semantics, targeted record fetches for
// fixture confirmation, capped journal read) so certification never runs a
// full-volume scan. Query-only: it NEVER creates/resizes/deletes the
// journal and NEVER writes (no FSCTL_CREATE_USN_JOURNAL, no raw writes).
namespace ffntfsobserve {

// Terminal classification of one probe invocation. BLOCKED covers every
// environment shortfall (not elevated, non-NTFS, access denied, no fixed
// NTFS volume); only genuine semantic observation yields OBSERVED.
enum class ObserveStatus {
    Observed,
    Blocked,
    Failed,
};

const char* ObserveStatusName(ObserveStatus status);

// Pure: maps open/filesystem/observation signals to a terminal status.
// Unit-testable without a volume.
ObserveStatus ClassifyObserveStatus(bool volumeOpened, DWORD volumeOpenError, bool isNtfs,
                                    bool enumOk, size_t parsedRecords, bool journalQueried);

// Pure: parses "0x..." / decimal file-reference / USN text. Returns
// nullopt on malformed input (never throws, never trusts the length).
std::optional<uint64_t> ParseHexU64(const std::wstring& text);

// Pure: canonical "0x..." upper-hex rendering of a file reference / USN.
std::string FormatHexU64(uint64_t value);

// Pure: joins a drive letter and root-to-leaf names into "X:\a\b".
std::wstring JoinParentPath(wchar_t driveLetter, const std::vector<std::wstring>& names);

// Observed shape shared by the find/usn legs (matches the certification
// runner's Name/Size/ParentPath/FileId record contract).
struct ObservedRecord {
    std::wstring name;
    uint64_t sizeBytes = 0;
    std::wstring parentPath;
    uint64_t fileReferenceNumber = 0;
    bool found = false;
};

// Pure walker over one FSCTL_ENUM_USN_DATA output buffer: invokes the
// callback once per structurally valid USN_RECORD_V2 entry (production
// bounds rule -- RecordLength==0 or overrun stops the walk, mirroring
// VolumeScanner.cpp). Returns entries visited. Never reads past
// bufferSize; safe on adversarial bytes.
size_t WalkEnumBuffer(const uint8_t* buffer, size_t bufferSize,
                      const std::function<void(uint64_t fileReferenceNumber)>& callback);

// Runs `fftest ntfs-observe ...`. argv/argc are the arguments AFTER the
// "ntfs-observe" verb:
//
//   ntfs-observe <L> semantics
//   ntfs-observe <L> find <frnHex>...
//   ntfs-observe <L> usn <startUsnHex> <frnHex>...
//
// Always emits one JSON document on stdout. Exit 0 = OBSERVED, 2 =
// BLOCKED (fail-closed, reason in JSON), 1 = FAIL/usage.
int RunNtfsObserveProbe(int argc, wchar_t** argv);

} // namespace ffntfsobserve
