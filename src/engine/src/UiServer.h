#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

#include "ffipc/PipeListener.h"
#include "ffsetup/SecurityDescriptors.h"
#include "ffprotocol/SnapshotFormat.h"

#include "DirectoryWatcher.h"
#include "SnapshotPublisher.h"

namespace ffengine {

// The engine's same-privilege, control-plane-only server for FastFiles
// (UI) clients (design.md D3; tasks 4.7, 4.8; UI-side counterpart is task
// 5.2). Handles Subscribe/RequestDirectory, republishes the shared-memory
// snapshot on every requested or watched-directory change, and pushes
// NewGeneration/EngineStatus notifications to every subscribed client.
class UiServer {
public:
    bool Start(DWORD sessionId);
    void Stop();

    // Broadcasts the privileged-connection status to every subscribed UI
    // client (task 4.9; spec "Version-Aware Reconnection" / status badge).
    void SetEngineStatus(bool privilegedPathActive);

    // index-storage-and-scanning tasks.md 3.5: merges directory data
    // sourced from the privileged, whole-volume index (VolumeSessionManager/
    // IndexPipeline) into the published snapshot, republishing and
    // broadcasting a new generation exactly as a degraded-mode directory
    // request does. Keyed by full path, same as directories_ -- entries
    // from the index simply take their place alongside (and eventually
    // superseding, as the index grows to cover them) any degraded-mode-
    // populated entries for the same path.
    void MergeIndexDirectories(std::map<std::wstring, ffprotocol::SnapshotDirectory> indexDirectories);

    // Invoked (from an arbitrary internal thread) on every UI request --
    // used by IdleManager (task 4.10) as the "recent activity" signal.
    std::function<void()> onActivity;

private:
    void HandleConnection(HANDLE pipe);
    void HandleRequestDirectory(HANDLE pipe, const std::wstring& path);
    void OnDirectoryChanged(const std::wstring& path);
    void RepublishAndBroadcastGeneration();
    void BroadcastEngineStatus(HANDLE toSinglePipe = nullptr);

    ffipc::PipeListener listener_;
    ffsetup::OwnedSecurityDescriptor securityDescriptor_;
    DirectoryWatcher watcher_;
    SnapshotPublisher snapshot_;

    std::mutex directoriesMutex_;
    std::map<std::wstring, ffprotocol::SnapshotDirectory> directories_;

    std::mutex clientsMutex_;
    std::vector<HANDLE> subscribedPipes_;

    std::atomic<bool> engineActive_{false};
};

} // namespace ffengine
