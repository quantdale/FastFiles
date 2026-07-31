#pragma once
#include <windows.h>

#include <atomic>
#include <mutex>

#include "ffprotocol/Commands.h"

namespace ffindexsvc {

// index-storage-and-scanning tasks.md 5.1/5.2/5.4: replaces the
// NotYetImplemented OpenUsnJournal stub with real
// FSCTL_QUERY_USN_JOURNAL/FSCTL_READ_USN_JOURNAL-based streaming.
//
// Immediately sends a MessageType::UsnJournalOpened frame carrying the
// volume's current JournalId (tasks.md 5.2/spec "USN Journal Identity
// Reported for Resume Validation"), then streams JournalRecordBatch
// frames for the lifetime of the call. Each change record is re-read via
// FSCTL_GET_NTFS_FILE_RECORD and parsed with MftParser.h (tasks.md 5.3:
// "apply the same MFT attribute allowlist ... to journal change
// records") rather than trusting USN_RECORD's own truncated fields
// (which have only one timestamp and no size); a record that can no
// longer be read this way (typically because it was deleted) falls back
// to the raw USN_RECORD fields alone, with size 0 -- still enough for the
// engine to recognize and apply a delete via the record's Reason flags.
//
// Returns only when `shouldStop` is observed, the pipe write fails, or
// FSCTL_QUERY_USN_JOURNAL/FSCTL_READ_USN_JOURNAL itself fails (e.g. the
// journal was deleted mid-stream) -- the caller (ServiceConnection) is
// responsible for noticing the thread has returned and tearing down
// registry state; this function does not retry a failed
// FSCTL_QUERY_USN_JOURNAL, since a stale JournalId reported here is
// exactly the discontinuity signal design.md D6 asks the *engine* to act
// on, not something this layer should paper over.
//
// Writes to `pipe` are serialized via `writeMutex`, shared with the rest
// of the connection (see VolumeScanner.h for why).
void RunUsnJournalStream(
    HANDLE pipe,
    std::mutex& writeMutex,
    ffprotocol::VolumeId volumeId,
    wchar_t driveLetter,
    uint64_t resumeUsn,
    const std::atomic<bool>& shouldStop);

} // namespace ffindexsvc
