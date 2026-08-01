#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "ffprotocol/Commands.h"

namespace ffindexsvc {

// index-storage-and-scanning tasks.md 4.1/4.4/4.5: replaces the
// NotYetImplemented StartVolumeScan stub with a real raw-volume MFT
// enumeration, streaming ScanRecordBatch frames (and a final
// ScanComplete) over `pipe`.
//
// Enumeration is driven by FSCTL_ENUM_USN_DATA (64 KB buffers), which
// returns only in-use files, in MFT order -- one DeviceIoControl per
// ~hundreds of records instead of one FSCTL_GET_NTFS_FILE_RECORD per MFT
// slot, most of which would fail on unused slots. The full MFT record for
// each enumerated FileReferenceNumber is still fetched individually via
// FSCTL_GET_NTFS_FILE_RECORD (which the filesystem driver services by
// locating and reading the correct on-disk MFT record -- this process
// never hand-walks the $MFT's own non-resident data runs), then parsed
// with MftParser.h's allowlisted, skip-and-continue-on-malformed-record
// logic (tasks.md 4.2/4.3). The record index still doubles as the resume
// cursor (tasks.md 4.5/D8): resumeCursor is the 8-byte little-endian
// index of the next record to read, empty meaning "start from record 0".
// Note the units split: the cursor is a record INDEX, while
// FSCTL_ENUM_USN_DATA's start position and returned FileReferenceNumbers
// are NTFS FRNs, i.e. BYTE OFFSETS into the $MFT (FRN == index *
// recordSize) -- the two are converted at the enumeration boundary.
//
// Writes to `pipe` are serialized via `writeMutex`, which the caller
// shares with whatever else writes to the same connection (Heartbeat
// acks, etc.) -- named-pipe writes from multiple threads on one handle
// are not implicitly serialized by the OS, so frames could otherwise
// interleave. Polls `shouldStop` between buffers/records/batches for
// prompt cancellation (StopVolumeScan or connection teardown) rather
// than only checking once per batch.
void RunVolumeScan(
    HANDLE pipe,
    std::mutex& writeMutex,
    ffprotocol::VolumeId volumeId,
    wchar_t driveLetter,
    const std::vector<uint8_t>& resumeCursor,
    bool lowPriority,
    const std::atomic<bool>& shouldStop);

} // namespace ffindexsvc
