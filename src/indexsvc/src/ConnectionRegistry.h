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
// ServiceConnection uses to enforce connection-scoped handles on BOTH the
// Start/Open request (a Start/Open that would supersede another live
// connection's handle is rejected) and the Stop/Close request (only the
// owning connection may stop/close before it stops the worker).
class ConnectionRegistry {
public:
    using ConnectionId = uint64_t;

    // Returns true (and records `owner` as the owner) only if `owner` may
    // start a scan on `volumeId`: either no other live connection owns it,
    // or `owner` already owns it (re-starting/superseding its own scan).
    // Returns false if another live connection owns it -- the connection-
    // scoped-handles design forbids a Start from superseding another
    // connection's open handle.
    bool TryMarkVolumeScanStarted(ConnectionId owner, ffprotocol::VolumeId volumeId);
    // Returns true (and clears the entry) only if `requester` owns an
    // active scan on `volumeId`.
    bool TryStopVolumeScan(ConnectionId requester, ffprotocol::VolumeId volumeId);

    // Returns true (and records `owner` as the owner) only if `owner` may
    // open a journal on `volumeId`: either no other live connection owns
    // it, or `owner` already owns it (re-opening its own journal). Returns
    // false if another live connection owns it -- consistent with the
    // connection-scoped-handles design.
    bool TryMarkUsnJournalOpened(ConnectionId owner, ffprotocol::VolumeId volumeId);
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
