#include "ConnectionRegistry.h"

namespace ffindexsvc {

void ConnectionRegistry::MarkVolumeScanStarted(ConnectionId owner, ffprotocol::VolumeId volumeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    volumeScanOwners_[volumeId.value] = owner;
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

void ConnectionRegistry::MarkUsnJournalOpened(ConnectionId owner, ffprotocol::VolumeId volumeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    usnJournalOwners_[volumeId.value] = owner;
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
