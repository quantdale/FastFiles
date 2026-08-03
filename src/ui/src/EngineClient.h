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
#include <vector>
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
    using UnavailableVolumesCallback =
        std::function<void(std::vector<ffprotocol::UnavailableVolumeRecord>)>;
    using ForgetUnavailableVolumeCallback =
        std::function<void(ffprotocol::ForgetUnavailableVolumeResultPayload)>;
    using FolderAggregateCallback =
        std::function<void(uint64_t requestId, ffprotocol::FolderAggregateStatus status, uint64_t itemCount, uint64_t totalSizeBytes)>;
    using VolumeStatusCallback = std::function<void(std::vector<ffprotocol::VolumeStatusRecord>)>;

    void Start(GenerationCallback onNewGeneration, StatusCallback onStatus, DirectoryErrorCallback onDirectoryError);
    void Stop();

    // Asks the engine to enumerate and start watching `path`. The result
    // shows up either as a DirectoryErrorCallback invocation or as the
    // path appearing in the next snapshot generation (onNewGeneration).
    void RequestDirectory(const std::wstring& path);
    void ReloadIndexingConfig();
    void RequestUnavailableVolumes(UnavailableVolumesCallback callback);
    void ForgetUnavailableVolume(int64_t volumeRowId, ForgetUnavailableVolumeCallback callback);
    void RequestFolderAggregate(int64_t volumeRowId, uint64_t parentFrnLow, uint64_t parentFrnHigh, FolderAggregateCallback callback);
    // settings-and-appearance §7.3: requests the engine's current
    // per-volume index-health condition report for the settings UI's
    // Indexing page.
    void RequestVolumeStatus(VolumeStatusCallback callback);
    // settings-and-appearance §9.1: control-plane pause/resume, global
    // (scope == 0) or per-volume (uppercase drive letter). Fire-and-forget:
    // the resulting state is read back through RequestVolumeStatus.
    void SetIndexingPaused(uint8_t scope, bool paused);
    uint64_t LastRequestId() const { return lastRequestId_; }

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
    mutable std::mutex mappingMutex_;

    std::thread managementThread_;
    std::thread invalidationThread_;
    std::atomic<bool> running_{false};
    std::mutex writeMutex_;
    std::mutex invalidationMutex_;
    std::condition_variable invalidationCv_;
    std::deque<std::wstring> invalidationQueue_;
    uint64_t lastRequestId_ = 0;

    GenerationCallback onNewGeneration_;
    StatusCallback onStatus_;
    DirectoryErrorCallback onDirectoryError_;
    std::mutex volumeCallbackMutex_;
    UnavailableVolumesCallback onUnavailableVolumes_;
    ForgetUnavailableVolumeCallback onForgetUnavailableVolume_;
    std::mutex aggregateCallbackMutex_;
    FolderAggregateCallback onFolderAggregate_;
    std::mutex volumeStatusCallbackMutex_;
    VolumeStatusCallback onVolumeStatus_;
};

} // namespace ffui
