#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#include "ffindexstore/Projection.h"
#include "ffipc/PipeListener.h"
#include "ffsetup/SecurityDescriptors.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"
#include "ffprotocol/WindowsPath.h"

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
    using FrameWriter = std::function<bool(HANDLE, uint16_t, const void*, uint32_t)>;
    using PipeCanceller = std::function<void(HANDLE)>;

    explicit UiServer(FrameWriter frameWriter = {}, PipeCanceller pipeCanceller = {});
    ~UiServer();

    UiServer(const UiServer&) = delete;
    UiServer& operator=(const UiServer&) = delete;

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
    void MergeIndexDirectories(ffprotocol::PooledSnapshotMap indexDirectories);
    // Compatibility overload for owning maps (tests, small manual paths).
    // Production passes the pooled export so the eliminated allocations do
    // not reappear at this merge boundary.
    void MergeIndexDirectories(std::map<std::wstring, ffprotocol::SnapshotDirectory> indexDirectories);
    void RemoveVolumeDirectories(wchar_t driveLetter);

    // memory-bounded-indexed-scale 5.2: observable bounded working-set state.
    struct SnapshotWorkingSetStats {
        size_t totalDirectories = 0;
        size_t indexedDirectories = 0;
        size_t retainedCap = 0;
        uint64_t workingSetEvictions = 0;
    };
    SnapshotWorkingSetStats GetSnapshotWorkingSetStats() const;
    // Test/config seam: overrides the retained indexed-directory cap (0
    // restores the production default). Must be called before merges.
    void SetIndexedWorkingSetCap(size_t maxIndexedDirectories);

    // nonblocking-indexed-read-plane 5.1: global admission diagnostics for
    // the bounded indexed-search coordinator. The limits are deliberately
    // engine-global even though each connection retains its own supersession
    // worker.
    struct IndexedSearchStats {
        size_t active = 0;
        size_t queued = 0;
        size_t peakActive = 0;
        uint64_t completed = 0;
        uint64_t queuedCancellations = 0;
        uint64_t rejected = 0;
        double totalQueueWaitMs = 0.0;
        double maxQueueWaitMs = 0.0;
    };
    IndexedSearchStats GetIndexedSearchStats() const;
    // Test seam: changes the bounded coordinator limits before search work is
    // admitted; zero restores the production default for that limit.
    void SetIndexedSearchLimitsForTesting(size_t maxActive, size_t maxQueued);

    // Invoked (from an arbitrary internal thread) on every UI request --
    // used by IdleManager (task 4.10) as the "recent activity" signal.
    std::function<void()> onActivity;
    std::function<void()> onReloadIndexingConfig;
    std::function<std::vector<ffprotocol::UnavailableVolumeRecord>()> onRequestUnavailableVolumes;
    std::function<ffprotocol::ForgetUnavailableVolumeStatus(int64_t)> onForgetUnavailableVolume;
    using OnRequestFolderAggregate = std::function<std::optional<ffindexstore::Projection::FolderAggregate>(ffindexstore::VolumeRowId, ffindexstore::FileId)>;
    OnRequestFolderAggregate onRequestFolderAggregate;
    using SearchCancellation = std::function<bool()>;
    using OnRequestIndexedSearch = std::function<ffprotocol::IndexedSearchResponse(
        const ffprotocol::IndexedSearchRequest&, const SearchCancellation&)>;
    OnRequestIndexedSearch onRequestIndexedSearch;
    // settings-and-appearance §7.3: supplies the per-volume condition
    // report (VolumeStatusRecords) on demand; the engine derives it from
    // its own connection/scan/reconciliation state. Empty vector == no
    // volumes configured.
    using OnRequestVolumeStatus = std::function<std::vector<ffprotocol::VolumeStatusRecord>()>;
    OnRequestVolumeStatus onRequestVolumeStatus;
    // settings-and-appearance §9.1 (design.md D9): control-plane
    // pause/resume, global (scope == 0) or per-volume (uppercase drive
    // letter). The engine acknowledges by updating the state the status
    // report reads; there is no separate ack/outcome message.
    std::function<void(uint8_t scope, bool paused)> onSetIndexingPaused;

private:
    class SearchCoordinator : public std::enable_shared_from_this<SearchCoordinator> {
    public:
        static constexpr size_t kMaxActive = 2;
        static constexpr size_t kMaxQueued = 8;

        struct Stats {
            size_t active = 0;
            size_t queued = 0;
            size_t peakActive = 0;
            uint64_t completed = 0;
            uint64_t queuedCancellations = 0;
            uint64_t rejected = 0;
            double totalQueueWaitMs = 0.0;
            double maxQueueWaitMs = 0.0;
        };

        class Permit {
        public:
            Permit() = delete;
            ~Permit();
            Permit(const Permit&) = delete;
            Permit& operator=(const Permit&) = delete;
            Permit(Permit&& other) noexcept;
            Permit& operator=(Permit&& other) noexcept;

        private:
            friend class SearchCoordinator;
            explicit Permit(std::shared_ptr<SearchCoordinator> owner);
            std::shared_ptr<SearchCoordinator> owner_;
        };

        std::optional<Permit> Acquire(const std::function<bool()>& cancelled);
        void Notify();
        void Stop();
        void SetLimitsForTesting(size_t maxActive, size_t maxQueued);
        Stats GetStats() const;

    private:
        struct Waiter {
            std::chrono::steady_clock::time_point enqueuedAt = std::chrono::steady_clock::now();
        };
        void Release();

        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<std::shared_ptr<Waiter>> waiters_;
        size_t active_ = 0;
        size_t peakActive_ = 0;
        uint64_t completed_ = 0;
        uint64_t queuedCancellations_ = 0;
        uint64_t rejected_ = 0;
        double totalQueueWaitMs_ = 0.0;
        double maxQueueWaitMs_ = 0.0;
        size_t maxActive_ = kMaxActive;
        size_t maxQueued_ = kMaxQueued;
        bool stopping_ = false;
    };

    struct ConnectionGate {
        void Release() noexcept {
            std::lock_guard<std::mutex> lock(mutex);
            if (active != 0) {
                --active;
            }
            if (active == 0) {
                condition.notify_all();
            }
        }
        ~ConnectionGate() {
            if (shutdownEvent != nullptr && shutdownEvent != INVALID_HANDLE_VALUE) {
                CloseHandle(shutdownEvent);
            }
        }

        std::mutex mutex;
        std::condition_variable condition;
        // Atomic so HandleConnection can observe the stop mark from its own
        // loop without taking the gate mutex on every re-armed read.
        std::atomic<bool> stopping{false};
        // Manual-reset event signaled in Stop() to wake HandleConnection's
        // shutdown-aware read without polling.
        HANDLE shutdownEvent{nullptr};
        size_t active = 0;
    };

    struct ConnectionRegistration {
        std::shared_ptr<ConnectionGate> gate;
        ~ConnectionRegistration() {
            if (gate != nullptr) {
                gate->Release();
            }
        }
    };

    struct ClientConnection {
        struct OwnedPipe {
            explicit OwnedPipe(HANDLE value) noexcept : handle(value) {}
            ~OwnedPipe();

            OwnedPipe(const OwnedPipe&) = delete;
            OwnedPipe& operator=(const OwnedPipe&) = delete;

            HANDLE handle = INVALID_HANDLE_VALUE;
        };
        using PipePtr = std::shared_ptr<OwnedPipe>;

        ClientConnection(HANDLE pipe, FrameWriter frameWriter, PipeCanceller pipeCanceller,
                         std::shared_ptr<SearchCoordinator> searchCoordinator);
        ~ClientConnection();

        bool Write(uint16_t messageType, const void* payload = nullptr, uint32_t payloadSize = 0);
        void Cancel();
        void StartIndexedSearch(const ffprotocol::IndexedSearchRequest& request,
                                const OnRequestIndexedSearch& handler);
        bool CancelIndexedSearch(uint64_t requestId);
        void Close();
        PipePtr Pipe() const;

        void CancelIndexedSearches();
        void IndexedSearchWorker();

        struct IndexedSearchTask {
            ffprotocol::IndexedSearchRequest request;
            OnRequestIndexedSearch handler;
            std::shared_ptr<std::atomic<bool>> cancellation;
        };

        PipePtr pipe_;
        FrameWriter frameWriter_;
        PipeCanceller pipeCanceller_;
        std::shared_ptr<SearchCoordinator> searchCoordinator_;
        std::mutex writeMutex_;
        mutable std::mutex stateMutex_;
        bool closing_ = false;
        std::mutex indexedSearchMutex_;
        std::condition_variable indexedSearchCondition_;
        std::optional<IndexedSearchTask> pendingIndexedSearch_;
        std::shared_ptr<std::atomic<bool>> activeIndexedSearchCancellation_;
        uint64_t activeIndexedSearchRequestId_ = 0;
        bool indexedSearchStopping_ = false;
        std::thread indexedSearchThread_;
    };

    using ClientConnectionPtr = std::shared_ptr<ClientConnection>;

    void HandleConnection(HANDLE pipe, const std::shared_ptr<ConnectionGate>& connectionGate);
    void HandleRequestDirectory(const ClientConnectionPtr& client, const std::wstring& path);
    void HandleRequestFolderAggregate(const ClientConnectionPtr& client, const std::vector<uint8_t>& framePayload);
    bool HandleRequestIndexedSearch(const ClientConnectionPtr& client, const std::vector<uint8_t>& framePayload);
    void OnDirectoryChanged(const std::wstring& path);
    // recovery-degraded-browsing 4.2: returns false when the publish failed
    // (e.g. maximum snapshot payload exceeded) so the requesting client can be told
    // instead of silently keeping stale data.
    bool RepublishAndBroadcastGeneration();
    void BroadcastEngineStatus(const ClientConnectionPtr& toSingleClient = nullptr);
    bool SendDirectoryError(const ClientConnectionPtr& client, const std::wstring& path,
                            ffprotocol::DirectoryErrorReason reason);
    void BroadcastDirectoryError(const std::wstring& path, ffprotocol::DirectoryErrorReason reason);
    bool SendUnavailableVolumes(const ClientConnectionPtr& client);
    bool SendForgetUnavailableVolumeResult(const ClientConnectionPtr& client, int64_t volumeRowId);
    bool SendFolderAggregateResult(const ClientConnectionPtr& client, uint64_t requestId,
                                   ffprotocol::FolderAggregateStatus status, uint64_t itemCount,
                                   uint64_t totalSizeBytes, uint64_t totalAllocatedSizeBytes,
                                   uint64_t allocatedBytesKnownCount, uint64_t allocatedBytesTotalCount);
    bool SendIndexedSearchResult(const ClientConnectionPtr& client, const ffprotocol::IndexedSearchResponse& response);
    bool SendVolumeStatus(const ClientConnectionPtr& client);

    ffipc::PipeListener listener_;
    ffsetup::OwnedSecurityDescriptor securityDescriptor_;
    DirectoryWatcher watcher_;
    SnapshotPublisher snapshot_;

    mutable std::mutex directoriesMutex_;
    // performance-closure-and-multisurface-acceleration 6.1: the snapshot
    // model stores pooled entry names (no per-entry owning strings).
    ffprotocol::PooledCaseInsensitiveSnapshotMap directories_;

    // deterministic-missing-path-state D2/D3: per-path observation ordering.
    // An explicit degraded filesystem observation (from request or watcher
    // refresh) outranks a later stale indexed merge for the same path. Each
    // entry records the source, status, and a monotonically increasing epoch.
    enum class ObservationSource : uint8_t {
        Request,      // explicit HandleRequestDirectory enumeration
        WatchRefresh, // watcher change callback re-enumeration
        IndexedMerge, // indexed export merge
    };
    struct PathObservation {
        ObservationSource source = ObservationSource::IndexedMerge;
        ffprotocol::DirectoryEnumerationStatus status = ffprotocol::DirectoryEnumerationStatus::Success;
        uint64_t epoch = 0;
    };
    std::map<std::wstring, PathObservation, ffprotocol::CaseInsensitivePathLess> pathObservations_;
    uint64_t nextObservationEpoch_ = 1;

    // memory-bounded-indexed-scale 5.2: retained indexed directories are a
    // bounded working set. When a merge pushes the count past the cap, the
    // lowest-observation-epoch indexed entries are dropped first; degraded
    // (actively browsed/watched) entries -- volumeRowId == 0 -- are always
    // retained and never counted against the cap.
    static constexpr size_t kDefaultMaxRetainedIndexedDirectories = 16384;
    size_t indexedWorkingSetCap_ = kDefaultMaxRetainedIndexedDirectories;
    uint64_t workingSetEvictions_ = 0;
    void EnforceIndexedWorkingSetCapLocked();

    std::mutex clientsMutex_;
    std::vector<ClientConnectionPtr> subscribedClients_;
    std::shared_ptr<ConnectionGate> connectionGate_;
    mutable std::mutex searchCoordinatorMutex_;
    std::shared_ptr<SearchCoordinator> searchCoordinator_;

    FrameWriter frameWriter_;
    PipeCanceller pipeCanceller_;

    std::atomic<bool> running_{false};
    std::atomic<bool> engineActive_{false};
};

} // namespace ffengine
