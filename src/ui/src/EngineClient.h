#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <windows.h>

#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"

namespace ffui {

// Task 5.2: the UI's client connection to FastFilesEngine's same-privilege
// control pipe. Subscribes to snapshot-generation notifications, requests
// directory enumeration/watching, and reads the published snapshot
// directly out of shared memory (zero IPC round trip per read).
//
// Also implements the "lazy start" half of task 4.1: if the engine isn't
// reachable yet (the per-user Scheduled Task may not have fired this
// session, or this is the very first run before the next logon), the UI
// launches FastFilesEngine.exe directly rather than waiting.
class EngineClient {
public:
    ~EngineClient();

    using GenerationCallback = std::function<void()>;
    using StatusCallback = std::function<void(bool privilegedPathActive)>;
    using DirectoryErrorCallback = std::function<void(const std::wstring& path, ffprotocol::DirectoryErrorReason reason)>;

    void Start(GenerationCallback onNewGeneration, StatusCallback onStatus, DirectoryErrorCallback onDirectoryError);
    void Stop();

    // Asks the engine to enumerate and start watching `path`. The result
    // shows up either as a DirectoryErrorCallback invocation or as the
    // path appearing in the next snapshot generation (onNewGeneration).
    void RequestDirectory(const std::wstring& path);
    void ReloadIndexingConfig();

    // Reads the most recently published snapshot directly out of the
    // mapped shared-memory section.
    std::optional<std::map<std::wstring, ffprotocol::SnapshotDirectory>> ReadSnapshot() const;

private:
    void ManagementLoop();
    bool ConnectAndSubscribe();
    void ReaderLoop();
    void InvalidationLoop();
    void SendDirectoryRequest(const std::wstring& path);
    bool MapSnapshotSection(const std::wstring& sectionName);
    void UnmapSnapshotSection();
    void LaunchEngineIfNotRunning(const std::wstring& pipeName);

    std::wstring enginePipeName_;
    std::atomic<HANDLE> pipe_{INVALID_HANDLE_VALUE};

    HANDLE mappingHandle_ = nullptr;
    const uint8_t* mappedView_ = nullptr;

    std::thread managementThread_;
    std::thread invalidationThread_;
    std::atomic<bool> running_{false};
    std::mutex writeMutex_;
    std::mutex invalidationMutex_;
    std::condition_variable invalidationCv_;
    std::deque<std::wstring> invalidationQueue_;

    GenerationCallback onNewGeneration_;
    StatusCallback onStatus_;
    DirectoryErrorCallback onDirectoryError_;
};

} // namespace ffui
