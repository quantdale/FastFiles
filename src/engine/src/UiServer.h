#pragma once
#include <atomic>
#include <condition_variable>
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
    void MergeIndexDirectories(std::map<std::wstring, ffprotocol::SnapshotDirectory> indexDirectories);
    void RemoveVolumeDirectories(wchar_t driveLetter);

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

        std::mutex mutex;
        std::condition_variable condition;
        bool stopping = false;
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
        ClientConnection(HANDLE pipe, FrameWriter frameWriter, PipeCanceller pipeCanceller);
        ~ClientConnection();

        bool Write(uint16_t messageType, const void* payload = nullptr, uint32_t payloadSize = 0);
        void Cancel();
        void StartIndexedSearch(const ffprotocol::IndexedSearchRequest& request,
                                const OnRequestIndexedSearch& handler);
        bool CancelIndexedSearch(uint64_t requestId);
        void Close();
        HANDLE Pipe() const noexcept { return pipe_; }

        void CancelPendingWriteLocked();
        void CancelIndexedSearches();
        void IndexedSearchWorker();

        struct IndexedSearchTask {
            ffprotocol::IndexedSearchRequest request;
            OnRequestIndexedSearch handler;
            std::shared_ptr<std::atomic<bool>> cancellation;
        };

        HANDLE pipe_ = INVALID_HANDLE_VALUE;
        HANDLE writerThread_ = nullptr;
        FrameWriter frameWriter_;
        PipeCanceller pipeCanceller_;
        std::mutex writeMutex_;
        std::mutex stateMutex_;
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

    void HandleConnection(HANDLE pipe);
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
                                   ffprotocol::FolderAggregateStatus status,
                                   uint64_t itemCount, uint64_t totalSizeBytes);
    bool SendIndexedSearchResult(const ClientConnectionPtr& client, const ffprotocol::IndexedSearchResponse& response);
    bool SendVolumeStatus(const ClientConnectionPtr& client);

    ffipc::PipeListener listener_;
    ffsetup::OwnedSecurityDescriptor securityDescriptor_;
    DirectoryWatcher watcher_;
    SnapshotPublisher snapshot_;

    std::mutex directoriesMutex_;
    ffprotocol::CaseInsensitiveSnapshotMap directories_;

    std::mutex clientsMutex_;
    std::vector<ClientConnectionPtr> subscribedClients_;
    std::shared_ptr<ConnectionGate> connectionGate_;

    FrameWriter frameWriter_;
    PipeCanceller pipeCanceller_;

    std::atomic<bool> running_{false};
    std::atomic<bool> engineActive_{false};
};

} // namespace ffengine
