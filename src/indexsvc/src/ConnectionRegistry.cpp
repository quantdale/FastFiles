#include "ConnectionRegistry.h"

namespace ffindexsvc {

bool ConnectionRegistry::TryMarkVolumeScanStarted(ConnectionId owner, ffprotocol::VolumeId volumeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = volumeScanOwners_.find(volumeId.value);
    if (it != volumeScanOwners_.end() && it->second != owner) {
        return false;  // another live connection owns the scan on this volume
    }
    volumeScanOwners_[volumeId.value] = owner;
    return true;
}

bool ConnectionRegistry::TryStopVolumeScan(ConnectionId requester, ffprotocol::VolumeId volumeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = volumeScanOwners_.find(volumeId.value);
    if (it == volumeScanOwners_.end() || it->second != requester) {
        return false;
    }
    volumeScanOwners_.erase(it);
    return true;
}

bool ConnectionRegistry::TryMarkUsnJournalOpened(ConnectionId owner, ffprotocol::VolumeId volumeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = usnJournalOwners_.find(volumeId.value);
    if (it != usnJournalOwners_.end() && it->second != owner) {
        return false;  // another live connection owns the journal on this volume
    }
    usnJournalOwners_[volumeId.value] = owner;
    return true;
}

bool ConnectionRegistry::TryCloseUsnJournal(ConnectionId requester, ffprotocol::VolumeId volumeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = usnJournalOwners_.find(volumeId.value);
    if (it == usnJournalOwners_.end() || it->second != requester) {
        return false;
    }
    usnJournalOwners_.erase(it);
    return true;
}

void ConnectionRegistry::TeardownConnection(ConnectionId owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = volumeScanOwners_.begin(); it != volumeScanOwners_.end();) {
        it = (it->second == owner) ? volumeScanOwners_.erase(it) : std::next(it);
    }
    for (auto it = usnJournalOwners_.begin(); it != usnJournalOwners_.end();) {
        it = (it->second == owner) ? usnJournalOwners_.erase(it) : std::next(it);
    }
}

} // namespace ffindexsvc
