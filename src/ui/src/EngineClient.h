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

struct SnapshotReadResult {
    uint64_t generation = 0;
    std::map<std::wstring, ffprotocol::SnapshotDirectory> directories;
};

// recovery-degraded-browsing D2: the UI-side connection lifecycle states.
// Starting covers both the initial launch and reconnects after a drop;
// after a bounded number of attempts the state becomes Unavailable and the
// management thread parks until RetryConnect() (badge click) or Stop().
enum class EngineConnectionState : uint8_t {
    Starting,   // attempting to launch/connect (initial + bounded retries)
    Connected,  // control pipe connected and snapshot mapped
    Unavailable // gave up after the bounded attempt cycle
};

// Task 5.2: the UI's client connection to FastFilesEngine's same-privilege
// control pipe. Subscribes to snapshot-generation notifications, requests
// directory enumeration/watching, and reads the published snapshot
// directly out of shared memory (zero IPC round trip per read).
//
// Also implements the "lazy start" half of task 4.1: if the engine isn't
// reachable yet (the per-user Scheduled Task may not have fired this
// session, or this is the very first run before the next logon), the UI
// launches FastFilesEngine.exe directly rather than waiting. The engine is
// located deterministically (see EngineDiscovery.h) and connection attempts
// are bounded (design D2) -- the UI never silently retries forever.
class EngineClient {
public:
    ~EngineClient();

    using GenerationCallback = std::function<void(uint64_t generation)>;
    // `state` is the local connection lifecycle; `privilegedPathActive`
    // mirrors the engine's EngineStatus report (index service reachable).
    using StatusCallback = std::function<void(EngineConnectionState state, bool privilegedPathActive)>;
    using DirectoryErrorCallback = std::function<void(const std::wstring& path, ffprotocol::DirectoryErrorReason reason)>;
    using UnavailableVolumesCallback =
        std::function<void(std::vector<ffprotocol::UnavailableVolumeRecord>)>;
    using ForgetUnavailableVolumeCallback =
        std::function<void(ffprotocol::ForgetUnavailableVolumeResultPayload)>;
    using FolderAggregateCallback =
        std::function<void(uint64_t requestId, ffprotocol::FolderAggregateStatus status, uint64_t itemCount, uint64_t totalSizeBytes)>;
    using VolumeStatusCallback = std::function<void(std::vector<ffprotocol::VolumeStatusRecord>)>;
    using IndexedSearchCallback = std::function<void(ffprotocol::IndexedSearchResponse)>;

    void Start(GenerationCallback onNewGeneration, StatusCallback onStatus, DirectoryErrorCallback onDirectoryError);
    void Stop();

    // Asks the engine to enumerate and start watching `path`. The result
    // shows up either as a DirectoryErrorCallback invocation or as the
    // path appearing in the next snapshot generation (onNewGeneration).
    void RequestDirectory(const std::wstring& path);
    void ReloadIndexingConfig();
    void RequestUnavailableVolumes(UnavailableVolumesCallback callback);
    void ForgetUnavailableVolume(int64_t volumeRowId, ForgetUnavailableVolumeCallback callback);
    uint64_t RequestFolderAggregate(int64_t volumeRowId, uint64_t parentFrnLow, uint64_t parentFrnHigh, FolderAggregateCallback callback);
    // settings-and-appearance §7.3: requests the engine's current
    // per-volume index-health condition report for the settings UI's
    // Indexing page.
    void RequestVolumeStatus(VolumeStatusCallback callback);
    // settings-and-appearance §9.1: control-plane pause/resume, global
    // (scope == 0) or per-volume (uppercase drive letter). Fire-and-forget:
    // the resulting state is read back through RequestVolumeStatus.
    void SetIndexingPaused(uint8_t scope, bool paused);
    // Runs a bounded query against the durable engine projection. The
    // callback is invoked from the reader/connection thread; UI callers must
    // marshal back to their owner window.
    uint64_t RequestIndexedSearch(const std::wstring& query, uint8_t scope,
                                  const std::wstring& scopePath, uint8_t sortField,
                                  bool descending, uint32_t maxResults,
                                  IndexedSearchCallback callback);
    uint64_t LastRequestId() const { return lastRequestId_.load(); }

    // Reads the most recently published snapshot directly out of the
    // mapped shared-memory section.
    std::optional<std::map<std::wstring, ffprotocol::SnapshotDirectory>> ReadSnapshot() const;
    // Same read with the committed generation, used by the browser freshness
    // model to reject an older/out-of-order mapping read.
    std::optional<SnapshotReadResult> ReadSnapshotWithGeneration() const;

    // recovery-degraded-browsing 2.3: restarts the bounded connect cycle
    // from the Unavailable state (invoked by the status-chip retry
    // affordance). Safe to call from any thread at any time.
    void RetryConnect();

private:
    void ManagementLoop();
    bool ConnectAndSubscribe();
    void ReaderLoop();
    void InvalidationLoop();
    bool SendDirectoryRequest(const std::wstring& path);
    bool WriteCurrentFrame(uint16_t messageType, const void* payload = nullptr, uint32_t payloadSize = 0);
    // Closes the current control pipe while serializing against every writer.
    // The management thread owns connection replacement; keeping close and
    // WriteFrame under the same mutex prevents a disconnect from closing a
    // HANDLE that the invalidation worker is using.
    void ClosePipe();
    void FailIndexedSearchCallbacks();
    bool MapSnapshotSection(const std::wstring& sectionName);
    void UnmapSnapshotSection();
    void LaunchEngineIfNotRunning(const std::wstring& pipeName);
    void SetConnectionState(EngineConnectionState state, bool privilegedPathActive);
    // Waits the reconnect cadence, interruptible by RetryConnect()/Stop().
    // Returns false when the client is stopping (caller should exit).
    bool WaitForRetryDelay();

    std::wstring enginePipeName_;
    std::atomic<HANDLE> pipe_{INVALID_HANDLE_VALUE};
    // A failed writer cancels the reader before the management thread can
    // leave ReaderLoop. Retain that exact handle until ClosePipe() runs so a
    // failed write cannot leak the old connection.
    std::vector<HANDLE> retiredPipes_;

    HANDLE mappingHandle_ = nullptr;
    const uint8_t* mappedView_ = nullptr;
    mutable std::mutex mappingMutex_;

    std::thread managementThread_;
    std::thread invalidationThread_;
    std::atomic<bool> running_{false};
    // recovery-degraded-browsing D2: parked-wait state for the bounded
    // connect cycle (RetryConnect wakes it).
    std::atomic<bool> retryRequested_{false};
    std::mutex retryMutex_;
    std::condition_variable retryCv_;
    std::mutex writeMutex_;
    std::mutex invalidationMutex_;
    std::condition_variable invalidationCv_;
    std::deque<std::wstring> invalidationQueue_;
    std::atomic<uint64_t> lastRequestId_{0};
    std::atomic<uint64_t> announcedGeneration_{0};

    GenerationCallback onNewGeneration_;
    StatusCallback onStatus_;
    DirectoryErrorCallback onDirectoryError_;
    std::mutex volumeCallbackMutex_;
    UnavailableVolumesCallback onUnavailableVolumes_;
    ForgetUnavailableVolumeCallback onForgetUnavailableVolume_;
    std::mutex aggregateCallbackMutex_;
    // Storage-analysis issues overlapping subtree requests concurrently; the
    // callback for each in-flight request is keyed by its request id so a
    // later request no longer overwrites an earlier one's callback.
    std::map<uint64_t, FolderAggregateCallback> aggregateCallbacks_;
    std::mutex volumeStatusCallbackMutex_;
    VolumeStatusCallback onVolumeStatus_;
    std::mutex indexedSearchCallbackMutex_;
    std::map<uint64_t, IndexedSearchCallback> indexedSearchCallbacks_;
};

} // namespace ffui
