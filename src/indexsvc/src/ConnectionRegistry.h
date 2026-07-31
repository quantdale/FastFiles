#pragma once
#include <cstdint>
#include <map>
#include <mutex>

#include "ffprotocol/Commands.h"

namespace ffindexsvc {

// Tracks which connection owns which open volume scan / USN journal.
// Both StartVolumeScan/StopVolumeScan and OpenUsnJournal/CloseUsnJournal
// are keyed directly by the (already-opaque, service-assigned) VolumeId --
// the wire protocol has no separate scan/journal handle, so the VolumeId
// itself is the connection-scoped resource (spec "Opaque, Connection-
// Scoped Handles").
//
// This bookkeeping exists now even though StartVolumeScan/OpenUsnJournal
// currently just reply NotYetImplemented (task 3.8): it's the scoping
// infrastructure the follow-up change (real MFT/USN parsing) will build
// on, and it's exercised end-to-end by this change's Stop/Close rejection
// and disconnect-teardown behavior regardless.
class ConnectionRegistry {
public:
    using ConnectionId = uint64_t;

    void MarkVolumeScanStarted(ConnectionId owner, ffprotocol::VolumeId volumeId);
    // Returns true (and clears the entry) only if `requester` owns an
    // active scan on `volumeId`.
    bool TryStopVolumeScan(ConnectionId requester, ffprotocol::VolumeId volumeId);

    void MarkUsnJournalOpened(ConnectionId owner, ffprotocol::VolumeId volumeId);
    bool TryCloseUsnJournal(ConnectionId requester, ffprotocol::VolumeId volumeId);

    // Clears every scan/journal owned by `owner`, without requiring an
    // explicit Stop/Close message first (spec "Disconnect tears down owned
    // scans and journals").
    void TeardownConnection(ConnectionId owner);

private:
    std::mutex mutex_;
    std::map<uint32_t, ConnectionId> volumeScanOwners_;
    std::map<uint32_t, ConnectionId> usnJournalOwners_;
};

} // namespace ffindexsvc
