#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "ffprotocol/Records.h"

namespace ffindexsvc {

enum class JournalOpenResult {
    Opened,
    NotSupported, // volume open failed, or the journal could not be created/queried at all
};

// Task 5.1/5.2: queries (creating if necessary) the volume's USN Change
// Journal identity, without starting to read it yet -- lets the caller
// (ServiceConnection) reply JournalOpened with the JournalId before
// streaming begins.
struct JournalIdentity {
    uint64_t journalId = 0;
    uint64_t firstUsn = 0;
    uint64_t nextUsn = 0;
};

std::optional<JournalIdentity> QueryOrCreateUsnJournal(wchar_t driveLetter);

enum class JournalRunOutcome {
    StoppedOrDisconnected, // RequestStop() observed, or onBatch returned false
    ResumePositionInvalid, // FSCTL_READ_USN_JOURNAL rejected startUsn (deleted/out-of-range) -- caller should reconcile (task 7.6)
    VolumeOpenFailed,
};

// Real USN Change Journal streaming (tasks.md section 5): reads change
// records from `startUsn` via FSCTL_READ_USN_JOURNAL for as long as the
// journal stays open, fetching each changed file's current raw MFT record
// (FetchAndParseMftRecord) for authoritative allowlisted fields -- except
// for a delete record, where the file is already gone and the USN
// journal's own record fields are used directly (there's nothing left to
// fetch).
class UsnJournalReader {
public:
    using BatchCallback = std::function<bool(const std::vector<ffprotocol::UsnDeltaV1>& batch, uint64_t resumeUsnAfterBatch)>;

    JournalRunOutcome Run(wchar_t driveLetter, uint64_t journalId, uint64_t startUsn, const BatchCallback& onBatch);

    void RequestStop() { stopRequested_.store(true, std::memory_order_relaxed); }

private:
    std::atomic<bool> stopRequested_{false};
};

} // namespace ffindexsvc
