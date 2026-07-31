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
// StartVolumeScan/OpenUsnJournal now drive real scanning/journal-reading
// worker threads (VolumeScanner.h/UsnJournalReader.h, index-storage-and-
// scanning tasks.md 4/5); this registry is the ownership bookkeeping a
// ServiceConnection uses to decide whether a Stop/Close request is
// authorized before it stops the corresponding worker.
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
