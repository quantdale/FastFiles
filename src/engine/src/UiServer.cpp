#include "UiServer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <thread>
#include <unordered_set>
#include <utility>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Diagnostics.h"
#include "ffprotocol/UiProtocol.h"
#include "ffsetup/Identifiers.h"

#include "DegradedModeEnumerator.h"

namespace ffengine {

namespace {

using ffprotocol::UiMessageType;

bool DefaultFrameWriter(HANDLE pipe, uint16_t messageType, const void* payload, uint32_t payloadSize) {
    return ffipc::WriteFrameOverlapped(
        pipe, messageType, payload, payloadSize,
        std::chrono::steady_clock::now() + std::chrono::seconds(5));
}

void DefaultPipeCanceller(HANDLE pipe) {
    if (pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe, nullptr);
    }
}

ffprotocol::SnapshotDirectory ToSnapshotDirectory(const EnumerationResult& result) {
    ffprotocol::SnapshotDirectory directory;
    directory.status = static_cast<ffprotocol::DirectoryEnumerationStatus>(result.status);
    directory.entries.reserve(result.entries.size());
    for (const auto& entry : result.entries) {
        ffprotocol::SnapshotDirectoryEntry snapshotEntry;
        snapshotEntry.name = entry.name;
        snapshotEntry.isDirectory = entry.isDirectory;
        snapshotEntry.sizeBytes = entry.sizeBytes;
        // allocated-size-storage-accuracy D8: the unprivileged
        // FindFirstFileEx path does not surface allocated size, so degraded
        // entries are unknown by construction -- never derived from logical
        // size, never rendered as 0 B.
        snapshotEntry.allocatedSizeBytes = 0;
        snapshotEntry.allocatedSizeKnown = false;
        snapshotEntry.creationTime = entry.creationTime;
        snapshotEntry.lastModifiedTime = entry.lastModifiedTime;
        snapshotEntry.attributes = entry.attributes;
        directory.entries.push_back(std::move(snapshotEntry));
    }
    return directory;
}

} // namespace

UiServer::ClientConnection::ClientConnection(HANDLE pipe, FrameWriter frameWriter, PipeCanceller pipeCanceller)
    : pipe_(std::make_shared<OwnedPipe>(pipe)), frameWriter_(std::move(frameWriter)), pipeCanceller_(std::move(pipeCanceller)) {
    indexedSearchThread_ = std::thread(&ClientConnection::IndexedSearchWorker, this);
}

UiServer::ClientConnection::OwnedPipe::~OwnedPipe() {
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
        CloseHandle(handle);
    }
}

UiServer::ClientConnection::~ClientConnection() {
    Close();
}

bool UiServer::ClientConnection::Write(uint16_t messageType, const void* payload, uint32_t payloadSize) {
    // Serialize complete frames for this connection, but do not hold the
    // connection state lock while the external pipe write can block.
    std::lock_guard<std::mutex> writeLock(writeMutex_);
    PipePtr pipe;
    {
        std::lock_guard<std::mutex> stateLock(stateMutex_);
        if (closing_) {
            return false;
        }
        pipe = pipe_;
    }
    return pipe != nullptr && frameWriter_(pipe->handle, messageType, payload, payloadSize);
}

void UiServer::ClientConnection::Cancel() {
    CancelIndexedSearches();
    PipePtr pipe;
    {
        std::lock_guard<std::mutex> stateLock(stateMutex_);
        if (!closing_) {
            pipe = pipe_;
        }
    }
    if (pipe != nullptr) {
        // CancelIoEx is safe to issue from the server's stop thread and
        // unwinds either an overlapped read owned by HandleConnection or an
        // overlapped write owned by a notification/callback thread.
        pipeCanceller_(pipe->handle);
    }
}

void UiServer::ClientConnection::StartIndexedSearch(
    const ffprotocol::IndexedSearchRequest& request, const OnRequestIndexedSearch& handler) {
    // audit-remediation FF-AUDIT-06: keep the pipe reader free to consume a
    // cancel/superseding request while the projection search runs.
    const auto cancellation = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(indexedSearchMutex_);
        if (indexedSearchStopping_) return;
        if (activeIndexedSearchCancellation_ != nullptr) {
            activeIndexedSearchCancellation_->store(true, std::memory_order_release);
        }
        if (pendingIndexedSearch_.has_value()) {
            pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
        }
        pendingIndexedSearch_ = IndexedSearchTask{request, handler, cancellation};
    }
    indexedSearchCondition_.notify_one();
}

bool UiServer::ClientConnection::CancelIndexedSearch(uint64_t requestId) {
    if (requestId == 0) return false;
    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lock(indexedSearchMutex_);
        if (pendingIndexedSearch_.has_value() && pendingIndexedSearch_->request.requestId == requestId) {
            pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
            pendingIndexedSearch_.reset();
            cancelled = true;
        }
        if (activeIndexedSearchRequestId_ == requestId && activeIndexedSearchCancellation_ != nullptr) {
            activeIndexedSearchCancellation_->store(true, std::memory_order_release);
            cancelled = true;
        }
    }
    indexedSearchCondition_.notify_one();
    return cancelled;
}

void UiServer::ClientConnection::CancelIndexedSearches() {
    {
        std::lock_guard<std::mutex> lock(indexedSearchMutex_);
        if (activeIndexedSearchCancellation_ != nullptr) {
            activeIndexedSearchCancellation_->store(true, std::memory_order_release);
        }
        if (pendingIndexedSearch_.has_value()) {
            pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
            pendingIndexedSearch_.reset();
        }
    }
    indexedSearchCondition_.notify_one();
}

void UiServer::ClientConnection::IndexedSearchWorker() {
    for (;;) {
        IndexedSearchTask task;
        {
            std::unique_lock<std::mutex> lock(indexedSearchMutex_);
            indexedSearchCondition_.wait(lock, [this] {
                return indexedSearchStopping_ || pendingIndexedSearch_.has_value();
            });
            if (indexedSearchStopping_) return;
            task = std::move(*pendingIndexedSearch_);
            pendingIndexedSearch_.reset();
            activeIndexedSearchCancellation_ = task.cancellation;
            activeIndexedSearchRequestId_ = task.request.requestId;
        }

        ffprotocol::IndexedSearchResponse response;
        response.requestId = task.request.requestId;
        if (task.handler) {
            const SearchCancellation cancellation = [token = task.cancellation] {
                return token->load(std::memory_order_acquire);
            };
            response = task.handler(task.request, cancellation);
            response.requestId = task.request.requestId;
        } else {
            response.status = ffprotocol::IndexedSearchStatus::Unavailable;
        }

        bool stopping = false;
        {
            std::lock_guard<std::mutex> lock(indexedSearchMutex_);
            if (activeIndexedSearchCancellation_ == task.cancellation) {
                activeIndexedSearchCancellation_.reset();
                activeIndexedSearchRequestId_ = 0;
            }
            stopping = indexedSearchStopping_;
        }
        if (!stopping && !task.cancellation->load(std::memory_order_acquire)
            && response.status != ffprotocol::IndexedSearchStatus::Cancelled) {
            // indexed-search-allocated-size D5: emit the additive V2 response so
            // the UI receives on-disk size + knownness; legacy message 18 is
            // preserved byte-for-byte and still decoded as allocated-unknown.
            const auto payload = ffprotocol::SerializeIndexedSearchResponseV2(response);
            if (!payload.empty()) {
                Write(static_cast<uint16_t>(UiMessageType::IndexedSearchResultV2),
                      payload.data(), static_cast<uint32_t>(payload.size()));
            }
        }
    }
}

void UiServer::ClientConnection::Close() {
    {
        std::lock_guard<std::mutex> lock(indexedSearchMutex_);
        indexedSearchStopping_ = true;
        if (activeIndexedSearchCancellation_ != nullptr) {
            activeIndexedSearchCancellation_->store(true, std::memory_order_release);
        }
        if (pendingIndexedSearch_.has_value()) {
            pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
            pendingIndexedSearch_.reset();
        }
    }
    indexedSearchCondition_.notify_one();

    PipePtr pipe;
    {
        std::lock_guard<std::mutex> stateLock(stateMutex_);
        if (closing_) {
            return;
        }
        closing_ = true;
        pipe = pipe_;
    }

    // Cancel before waiting for a concurrent writer. A writer that already
    // owns writeMutex_ is now guaranteed to unwind; a later writer observes
    // closing_ before it can issue I/O.
    if (pipe != nullptr) pipeCanceller_(pipe->handle);
    std::lock_guard<std::mutex> writeLock(writeMutex_);
    {
        std::lock_guard<std::mutex> stateLock(stateMutex_);
        pipe = pipe_;
        pipe_.reset();
    }
    if (indexedSearchThread_.joinable()) {
        indexedSearchThread_.join();
    }
}

UiServer::ClientConnection::PipePtr UiServer::ClientConnection::Pipe() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return pipe_;
}

UiServer::UiServer(FrameWriter frameWriter, PipeCanceller pipeCanceller)
    : frameWriter_(std::move(frameWriter)), pipeCanceller_(std::move(pipeCanceller)) {
    if (!frameWriter_) {
        frameWriter_ = DefaultFrameWriter;
    }
    if (!pipeCanceller_) {
        pipeCanceller_ = DefaultPipeCanceller;
    }
}

UiServer::~UiServer() {
    Stop();
}

bool UiServer::Start(DWORD sessionId) {
    connectionGate_ = std::make_shared<ConnectionGate>();
    if (!snapshot_.Start(sessionId)) {
        connectionGate_.reset();
        return false;
    }

    auto descriptor = ffsetup::BuildCurrentUserPipeSecurityDescriptor();
    if (!descriptor) {
        snapshot_.Stop();
        connectionGate_.reset();
        return false;
    }
    securityDescriptor_ = std::move(*descriptor);

    wchar_t pipeName[128];
    swprintf(pipeName, std::size(pipeName), ffsetup::kUiCtrlPipeNameFormat, sessionId);

    running_ = true;
    const auto connectionGate = connectionGate_;
    if (!listener_.Start(pipeName, &securityDescriptor_.attributes,
                         [this, connectionGate](HANDLE pipe) {
                             {
                                 std::lock_guard<std::mutex> lock(connectionGate->mutex);
                                 if (connectionGate->stopping) {
                                     CloseHandle(pipe);
                                     return;
                                 }
                                 ++connectionGate->active;
                             }
                             ConnectionRegistration registration{connectionGate};
                             HandleConnection(pipe);
                         })) {
        running_ = false;
        snapshot_.Stop();
        securityDescriptor_ = {};
        connectionGate_.reset();
        return false;
    }
    return true;
}

void UiServer::Stop() {
    running_ = false;
    const auto connectionGate = connectionGate_;
    if (connectionGate != nullptr) {
        std::lock_guard<std::mutex> lock(connectionGate->mutex);
        connectionGate->stopping = true;
    }

    // Cancel before joining watcher callbacks. A callback may already be in
    // ClientConnection::Write, and CancelIoEx must run before that callback's
    // owning watcher thread is joined. The client list is copied under its
    // short state lock; no client-list lock is held across pipe I/O.
    std::vector<ClientConnectionPtr> clients;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients = subscribedClients_;
    }
    for (const auto& client : clients) {
        client->Cancel();
    }

    listener_.Stop();

    // PipeListener invokes connections on retained joinable threads. Do not
    // tear down snapshot/client state until every callback that passed the
    // gate has returned; callbacks arriving after the stop mark close their
    // pipe without dereferencing this server.
    if (connectionGate != nullptr) {
        std::unique_lock<std::mutex> lock(connectionGate->mutex);
        connectionGate->condition.wait(lock, [&connectionGate] {
            return connectionGate->active == 0;
        });
    }

    watcher_.UnwatchAll();
    snapshot_.Stop();
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        subscribedClients_.clear();
    }
}

void UiServer::OnDirectoryChanged(const std::wstring& path) {
    auto result = EnumerateDirectoryDegraded(path);
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        directories_[path] = ToSnapshotDirectory(result);
        // deterministic-missing-path-state D2: record watcher observation
        // for merge precedence ordering (task 2.1).
        const auto snapStatus = static_cast<ffprotocol::DirectoryEnumerationStatus>(result.status);
        pathObservations_[path] = PathObservation{
            ObservationSource::WatchRefresh, snapStatus, nextObservationEpoch_++};
    }
    if (!RepublishAndBroadcastGeneration()) {
        // A watcher refresh has no single requesting client.  Surface the
        // same publish failure to every subscribed UI so a retained
        // same-context listing is visibly stale instead of silently current.
        BroadcastDirectoryError(path, ffprotocol::DirectoryErrorReason::PublishFailed);
    }
}

bool UiServer::RepublishAndBroadcastGeneration() {
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        generation = snapshot_.Publish(directories_);
    }
    if (generation == 0) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                      ffprotocol::DiagnosticCode::FFDIAG_SNAPSHOT_PUBLISH_FAILED,
                                      {}, {}, L"snapshot-publish-failed", ERROR_WRITE_FAULT, 0});
        return false; // publish failed; nothing to announce
    }

    ffprotocol::NewGenerationPayload payload{generation};
    std::vector<ClientConnectionPtr> clients;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients = subscribedClients_;
    }
    for (const auto& client : clients) {
        if (!client->Write(static_cast<uint16_t>(UiMessageType::NewGeneration), &payload, sizeof(payload))) {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            subscribedClients_.erase(
                std::remove(subscribedClients_.begin(), subscribedClients_.end(), client), subscribedClients_.end());
        }
    }
    return true;
}

void UiServer::BroadcastEngineStatus(const ClientConnectionPtr& toSingleClient) {
    ffprotocol::EngineStatusPayload payload{
        engineActive_ ? ffprotocol::PrivilegedPathStatus::Active : ffprotocol::PrivilegedPathStatus::Unavailable};

    if (toSingleClient != nullptr) {
        toSingleClient->Write(static_cast<uint16_t>(UiMessageType::EngineStatus), &payload, sizeof(payload));
        return;
    }

    std::vector<ClientConnectionPtr> clients;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients = subscribedClients_;
    }
    for (const auto& client : clients) {
        if (!client->Write(static_cast<uint16_t>(UiMessageType::EngineStatus), &payload, sizeof(payload))) {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            subscribedClients_.erase(
                std::remove(subscribedClients_.begin(), subscribedClients_.end(), client), subscribedClients_.end());
        }
    }
}

void UiServer::MergeIndexDirectories(std::map<std::wstring, ffprotocol::SnapshotDirectory> indexDirectories) {
    std::unordered_set<int64_t> incomingVolumes;
    for (const auto& [path, directory] : indexDirectories) {
        if (directory.volumeRowId != 0) {
            incomingVolumes.insert(directory.volumeRowId);
        }
    }
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        // ExportDirectorySnapshot is a complete export for each volume. Drop
        // the previous generation for those volume IDs before inserting the
        // new one; otherwise deleted/renamed directories remain searchable
        // forever as stale map keys. Degraded entries have volumeRowId == 0
        // and are intentionally retained until the indexed export covers
        // the same path.
        if (!incomingVolumes.empty()) {
            for (auto it = directories_.begin(); it != directories_.end();) {
                if (incomingVolumes.contains(it->second.volumeRowId)) {
                    it = directories_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& [path, directory] : indexDirectories) {
            // IndexPipeline historically accepts a caller prefix such as
            // "C:" while the UI requests the Win32 root as "C:\\". Keep
            // one canonical key in the shared snapshot without changing the
            // index pipeline's public export contract.
            std::wstring normalizedPath = path;
            if (normalizedPath.size() == 2 && normalizedPath[1] == L':') {
                normalizedPath.push_back(L'\\');
            }
            // deterministic-missing-path-state D2/D3: a stale indexed
            // Success must not overwrite a newer explicit failure
            // observation for the same canonical path. The observation
            // map records explicit filesystem results from requests and
            // watcher refreshes; indexed data is only inserted if no
            // newer explicit failure observation exists for the path
            // (task 2.1, 2.2).
            const auto obsIt = pathObservations_.find(normalizedPath);
            if (obsIt != pathObservations_.end()
                && obsIt->second.source != ObservationSource::IndexedMerge
                && obsIt->second.status != ffprotocol::DirectoryEnumerationStatus::Success) {
                // A newer explicit failure (NotFound/AccessDenied/IoError)
                // exists for this path. Do not overwrite with stale indexed
                // success (task 2.1). Record the suppression for diagnostics.
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                              ffprotocol::DiagnosticCode::FFDIAG_INDEXED_MERGE_SUPPRESSED,
                                              normalizedPath, {}, L"indexed-merge-suppressed",
                                              0, 0});
                continue;
            }
            directories_[normalizedPath] = std::move(directory);
            // Record the indexed merge as an observation so future
            // watcher/request observations can be compared (task 2.3).
            pathObservations_[normalizedPath] = PathObservation{
                ObservationSource::IndexedMerge,
                ffprotocol::DirectoryEnumerationStatus::Success,
                nextObservationEpoch_++};
        }
    }
    RepublishAndBroadcastGeneration();
}

void UiServer::RemoveVolumeDirectories(wchar_t driveLetter) {
    if (driveLetter == L'\0') {
        return;
    }
    std::wstring root;
    root.push_back(static_cast<wchar_t>(towupper(driveLetter)));
    root.push_back(L':');
    const std::wstring descendantPrefix = root + L"\\";
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        for (auto it = directories_.begin(); it != directories_.end();) {
            std::wstring normalized = it->first;
            if (!normalized.empty()) {
                normalized[0] = static_cast<wchar_t>(towupper(normalized[0]));
            }
            if (normalized == root || normalized.starts_with(descendantPrefix)) {
                it = directories_.erase(it);
            } else {
                ++it;
            }
        }
        // deterministic-missing-path-state: clean up observation state for
        // removed volume paths so tombstones don't accumulate (task 2.2).
        for (auto it = pathObservations_.begin(); it != pathObservations_.end();) {
            std::wstring normalized = it->first;
            if (!normalized.empty()) {
                normalized[0] = static_cast<wchar_t>(towupper(normalized[0]));
            }
            if (normalized == root || normalized.starts_with(descendantPrefix)) {
                it = pathObservations_.erase(it);
            } else {
                ++it;
            }
        }
    }
    RepublishAndBroadcastGeneration();
}

bool UiServer::SendUnavailableVolumes(const ClientConnectionPtr& client) {
    const auto records = onRequestUnavailableVolumes ? onRequestUnavailableVolumes()
                                                     : std::vector<ffprotocol::UnavailableVolumeRecord>{};
    if (records.size() > (ffprotocol::kMaxFrameSize - sizeof(ffprotocol::FrameHeader)
                          - sizeof(ffprotocol::UnavailableVolumesHeader))
            / sizeof(ffprotocol::UnavailableVolumeRecord)) {
        return false;
    }
    std::vector<uint8_t> payload(sizeof(ffprotocol::UnavailableVolumesHeader)
                                 + records.size() * sizeof(ffprotocol::UnavailableVolumeRecord));
    const ffprotocol::UnavailableVolumesHeader header{static_cast<uint32_t>(records.size())};
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!records.empty()) {
        std::memcpy(payload.data() + sizeof(header), records.data(),
                    records.size() * sizeof(ffprotocol::UnavailableVolumeRecord));
    }
    return client->Write(static_cast<uint16_t>(UiMessageType::UnavailableVolumes), payload.data(),
                         static_cast<uint32_t>(payload.size()));
}

bool UiServer::SendForgetUnavailableVolumeResult(const ClientConnectionPtr& client, int64_t volumeRowId) {
    const auto status = onForgetUnavailableVolume
        ? onForgetUnavailableVolume(volumeRowId)
        : ffprotocol::ForgetUnavailableVolumeStatus::Failed;
    const ffprotocol::ForgetUnavailableVolumeResultPayload payload{volumeRowId, status};
    if (status == ffprotocol::ForgetUnavailableVolumeStatus::Failed) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::DatabaseError,
                                      ffprotocol::DiagnosticCode::FFDIAG_FORGET_VOLUME_FAILED,
                                      {}, std::to_wstring(volumeRowId), L"forget-volume-failed", ERROR_WRITE_FAULT, 0});
    }
    return client->Write(static_cast<uint16_t>(ffprotocol::UiMessageType::ForgetUnavailableVolumeResult),
                         &payload, sizeof(payload));
}

bool UiServer::SendFolderAggregateResult(const ClientConnectionPtr& client, uint64_t requestId,
                                         ffprotocol::FolderAggregateStatus status, uint64_t itemCount,
                                         uint64_t totalSizeBytes, uint64_t totalAllocatedSizeBytes,
                                         uint64_t allocatedBytesKnownCount, uint64_t allocatedBytesTotalCount) {
    // allocated-size-storage-accuracy D6: the engine always emits the
    // additive FolderAggregateResultV2 (message 20); a legacy UI that only
    // knows message 13 simply sees no reply for this request type and shows
    // its pending state -- it never mis-parses the extended payload.
    const ffprotocol::FolderAggregateResultV2Payload payload{
        requestId, status, itemCount, totalSizeBytes, totalAllocatedSizeBytes,
        allocatedBytesKnownCount, allocatedBytesTotalCount};
    return client->Write(static_cast<uint16_t>(ffprotocol::UiMessageType::FolderAggregateResultV2),
                         &payload, sizeof(payload));
}

bool UiServer::SendIndexedSearchResult(const ClientConnectionPtr& client,
                                       const ffprotocol::IndexedSearchResponse& response) {
    auto payload = ffprotocol::SerializeIndexedSearchResponse(response);
    if (payload.empty()) {
        return false;
    }
    return client->Write(static_cast<uint16_t>(UiMessageType::IndexedSearchResult), payload.data(),
                         static_cast<uint32_t>(payload.size()));
}

bool UiServer::SendVolumeStatus(const ClientConnectionPtr& client) {
    const auto records = onRequestVolumeStatus
        ? onRequestVolumeStatus()
        : std::vector<ffprotocol::VolumeStatusRecord>{};
    if (records.size() > (ffprotocol::kMaxFrameSize - sizeof(ffprotocol::FrameHeader)
                          - sizeof(ffprotocol::VolumeStatusHeader))
            / sizeof(ffprotocol::VolumeStatusRecord)) {
        return false;
    }
    std::vector<uint8_t> payload(sizeof(ffprotocol::VolumeStatusHeader)
                                 + records.size() * sizeof(ffprotocol::VolumeStatusRecord));
    const ffprotocol::VolumeStatusHeader header{static_cast<uint32_t>(records.size())};
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!records.empty()) {
        std::memcpy(payload.data() + sizeof(header), records.data(),
                    records.size() * sizeof(ffprotocol::VolumeStatusRecord));
    }
    return client->Write(static_cast<uint16_t>(UiMessageType::VolumeStatus), payload.data(),
                         static_cast<uint32_t>(payload.size()));
}

void UiServer::SetEngineStatus(bool privilegedPathActive) {
    const bool changed = engineActive_.exchange(privilegedPathActive) != privilegedPathActive;
    if (changed) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_ENGINE_STATUS_CHANGED,
                                      {}, {}, privilegedPathActive ? L"active" : L"unavailable", 0, 0});
        BroadcastEngineStatus();
    }
}

bool UiServer::SendDirectoryError(const ClientConnectionPtr& client, const std::wstring& path,
                                   ffprotocol::DirectoryErrorReason reason) {
    std::vector<uint8_t> payload(sizeof(ffprotocol::DirectoryErrorPayload) + path.size() * sizeof(wchar_t));
    ffprotocol::DirectoryErrorPayload header{reason, static_cast<uint16_t>(path.size())};
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!path.empty()) {
        std::memcpy(payload.data() + sizeof(header), path.data(), path.size() * sizeof(wchar_t));
    }
    return client->Write(static_cast<uint16_t>(UiMessageType::DirectoryError), payload.data(),
                         static_cast<uint32_t>(payload.size()));
}

void UiServer::BroadcastDirectoryError(const std::wstring& path, ffprotocol::DirectoryErrorReason reason) {
    std::vector<ClientConnectionPtr> clients;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients = subscribedClients_;
    }
    for (const auto& client : clients) {
        if (!SendDirectoryError(client, path, reason)) {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            subscribedClients_.erase(
                std::remove(subscribedClients_.begin(), subscribedClients_.end(), client), subscribedClients_.end());
        }
    }
}

void UiServer::HandleRequestDirectory(const ClientConnectionPtr& client, const std::wstring& path) {
    if (onActivity) {
        onActivity();
    }

    watcher_.Watch(path, [this](const std::wstring& changedPath) { OnDirectoryChanged(changedPath); });

    auto result = EnumerateDirectoryDegraded(path);
    const auto snapshotDir = ToSnapshotDirectory(result);
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        directories_[path] = snapshotDir;
        // deterministic-missing-path-state D2: record the explicit
        // filesystem observation so a later stale indexed merge cannot
        // resurrect a deleted path (task 2.1).
        const auto snapStatus = static_cast<ffprotocol::DirectoryEnumerationStatus>(result.status);
        pathObservations_[path] = PathObservation{
            ObservationSource::Request, snapStatus, nextObservationEpoch_++};
    }

    if (result.status == EnumerationStatus::AccessDenied) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::InaccessibleDirectory,
                                      ffprotocol::DiagnosticCode::FFDIAG_DIRECTORY_ACCESS_DENIED,
                                      path, {}, L"access-denied", ERROR_ACCESS_DENIED, 0});
        SendDirectoryError(client, path, ffprotocol::DirectoryErrorReason::AccessDenied);
        // deterministic-missing-path-state D1: publish the failure status
        // into the snapshot so every subscribed client sees the terminal
        // state, not just the requesting one (task 4.1).
        RepublishAndBroadcastGeneration();
        return;
    }
    if (result.status == EnumerationStatus::NotFound) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::InaccessibleDirectory,
                                      ffprotocol::DiagnosticCode::FFDIAG_DIRECTORY_NOT_FOUND,
                                      path, {}, L"not-found", ERROR_PATH_NOT_FOUND, 0});
        SendDirectoryError(client, path, ffprotocol::DirectoryErrorReason::NoLongerAvailable);
        RepublishAndBroadcastGeneration();
        return;
    }
    if (result.status == EnumerationStatus::IoError) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::InaccessibleDirectory,
                                      ffprotocol::DiagnosticCode::FFDIAG_DIRECTORY_ENUMERATION_FAILED,
                                      path, {}, L"enumeration-failed", ERROR_READ_FAULT, 0});
        SendDirectoryError(client, path, ffprotocol::DirectoryErrorReason::EnumerationFailed);
        RepublishAndBroadcastGeneration();
        return;
    }

    // Success: publish + broadcast so this (and every other subscribed)
    // client can read the freshly enumerated directory straight out of
    // the shared-memory snapshot, zero further IPC round trip.
    if (!RepublishAndBroadcastGeneration()) {
        // recovery-degraded-browsing 4.2: enumeration succeeded but the
        // publish failed -- tell this client explicitly rather than leaving
        // it with stale data and no explanation.
        SendDirectoryError(client, path, ffprotocol::DirectoryErrorReason::PublishFailed);
    }
}

void UiServer::HandleRequestFolderAggregate(const ClientConnectionPtr& client,
                                            const std::vector<uint8_t>& framePayload) {
    if (framePayload.size() != sizeof(ffprotocol::RequestFolderAggregatePayload)) {
        return;
    }
    ffprotocol::RequestFolderAggregatePayload request{};
    std::memcpy(&request, framePayload.data(), sizeof(request));

    if (!onRequestFolderAggregate) {
        SendFolderAggregateResult(client, request.requestId, ffprotocol::FolderAggregateStatus::NotFound, 0, 0, 0, 0, 0);
        return;
    }

    const ffindexstore::FileId parentFrn{request.parentFrnLow, request.parentFrnHigh};
    const auto aggregateOpt = onRequestFolderAggregate(static_cast<ffindexstore::VolumeRowId>(request.volumeRowId), parentFrn);

    if (!aggregateOpt) {
        SendFolderAggregateResult(client, request.requestId, ffprotocol::FolderAggregateStatus::NotFound, 0, 0, 0, 0, 0);
    } else {
        const auto& aggregate = *aggregateOpt;
        SendFolderAggregateResult(client, request.requestId, ffprotocol::FolderAggregateStatus::Resolved,
                                  aggregate.itemCount, aggregate.totalSizeBytes,
                                  aggregate.totalAllocatedSizeBytes, aggregate.allocatedBytesKnownCount,
                                  aggregate.allocatedBytesTotalCount);
    }
}

bool UiServer::HandleRequestIndexedSearch(const ClientConnectionPtr& client,
                                          const std::vector<uint8_t>& framePayload) {
    const auto request = ffprotocol::ParseIndexedSearchRequest(framePayload.data(), framePayload.size());
    if (!request) {
        return false;
    }
    client->StartIndexedSearch(*request, onRequestIndexedSearch);
    return true;
}

void UiServer::HandleConnection(HANDLE pipe) {
    const ClientConnectionPtr client = std::make_shared<ClientConnection>(pipe, frameWriter_, pipeCanceller_);
    bool subscribed = false;

    for (;;) {
        const auto readPipe = client->Pipe();
        if (!readPipe) break;
        auto frame = ffipc::ReadFrameOverlapped(
            readPipe->handle, std::chrono::steady_clock::time_point::max());
        if (!frame) {
            break;
        }
        auto messageType = ffprotocol::ToUiMessageType(frame->header.messageType);
        if (!messageType) {
            break; // unrecognized message type: protocol violation
        }

        switch (*messageType) {
            case UiMessageType::Subscribe: {
                // fix-initial-snapshot-handshake: never acknowledge a
                // subscription with an unusable snapshot. UiServer::Start
                // commits a usable generation before exposing the listener,
                // so this branch is unreachable in normal operation; it is a
                // fail-closed guard so an internal regression surfaces as a
                // stage-specific diagnostic instead of an infinite client
                // reconnect loop over an advertised invalid generation.
                if (snapshot_.SectionName().empty() || snapshot_.CurrentGeneration() == 0) {
                    ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                                  ffprotocol::DiagnosticCode::FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID,
                                                  {}, {}, L"subscribe-without-valid-snapshot",
                                                  ERROR_INVALID_STATE, 0});
                    goto disconnected;
                }
                {
                    std::lock_guard<std::mutex> lock(clientsMutex_);
                    subscribedClients_.push_back(client);
                }
                subscribed = true;

                const std::wstring& sectionName = snapshot_.SectionName();
                std::vector<uint8_t> payload(sizeof(ffprotocol::SubscribeAckPayload) + sectionName.size() * sizeof(wchar_t));
                ffprotocol::SubscribeAckPayload header{snapshot_.CurrentGeneration(), static_cast<uint16_t>(sectionName.size())};
                std::memcpy(payload.data(), &header, sizeof(header));
                std::memcpy(payload.data() + sizeof(header), sectionName.data(), sectionName.size() * sizeof(wchar_t));

                if (!client->Write(static_cast<uint16_t>(UiMessageType::SubscribeAck), payload.data(),
                                   static_cast<uint32_t>(payload.size()))) {
                    goto disconnected;
                }
                BroadcastEngineStatus(client);
                break;
            }

            case UiMessageType::RequestDirectory: {
                if (frame->payload.size() < sizeof(ffprotocol::RequestDirectoryHeader)) {
                    goto disconnected;
                }
                ffprotocol::RequestDirectoryHeader header{};
                std::memcpy(&header, frame->payload.data(), sizeof(header));
                if (!ffprotocol::IsUiPathLengthValid(header.pathLengthChars)) {
                    goto disconnected;
                }
                const size_t expectedSize = sizeof(header) + static_cast<size_t>(header.pathLengthChars) * sizeof(wchar_t);
                if (frame->payload.size() != expectedSize) {
                    goto disconnected;
                }
                std::wstring path(
                    reinterpret_cast<const wchar_t*>(frame->payload.data() + sizeof(header)), header.pathLengthChars);
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                              ffprotocol::DiagnosticCode::FFDIAG_ENGINE_DIRECTORY_REQUEST_RECEIVED,
                                              path, {}, L"engine-directory-request-received", 0, 0});
                HandleRequestDirectory(client, path);
                break;
            }

            case UiMessageType::ReloadIndexingConfig:
                if (!frame->payload.empty()) goto disconnected;
                if (onReloadIndexingConfig) onReloadIndexingConfig();
                break;

            case UiMessageType::RequestUnavailableVolumes:
                if (!frame->payload.empty() || !SendUnavailableVolumes(client)) goto disconnected;
                break;

    case UiMessageType::ForgetUnavailableVolume: {
        if (frame->payload.size() != sizeof(ffprotocol::ForgetUnavailableVolumePayload)) goto disconnected;
        ffprotocol::ForgetUnavailableVolumePayload payload{};
        std::memcpy(&payload, frame->payload.data(), sizeof(payload));
        if (payload.volumeRowId <= 0 || !SendForgetUnavailableVolumeResult(client, payload.volumeRowId)) {
            goto disconnected;
        }
        break;
    }

    case UiMessageType::RequestFolderAggregate: {
        if (frame->payload.size() != sizeof(ffprotocol::RequestFolderAggregatePayload)) goto disconnected;
        HandleRequestFolderAggregate(client, frame->payload);
        break;
    }

    case UiMessageType::RequestIndexedSearch:
        if (!HandleRequestIndexedSearch(client, frame->payload)) goto disconnected;
        break;

    case UiMessageType::CancelIndexedSearch: {
        if (frame->payload.size() != sizeof(ffprotocol::CancelIndexedSearchPayload)) goto disconnected;
        ffprotocol::CancelIndexedSearchPayload payload{};
        std::memcpy(&payload, frame->payload.data(), sizeof(payload));
        client->CancelIndexedSearch(payload.requestId);
        break;
    }

    case UiMessageType::RequestVolumeStatus:
        if (!frame->payload.empty() || !SendVolumeStatus(client)) goto disconnected;
        break;

    case UiMessageType::SetIndexingPaused: {
        if (frame->payload.size() != sizeof(ffprotocol::SetIndexingPausedPayload)) goto disconnected;
        ffprotocol::SetIndexingPausedPayload payload{};
        std::memcpy(&payload, frame->payload.data(), sizeof(payload));
        const wchar_t letter = static_cast<wchar_t>(towupper(static_cast<wchar_t>(payload.scope)));
        if (payload.scope != 0 && (letter < L'A' || letter > L'Z')) goto disconnected;
        if (payload.paused > 1) goto disconnected;
        if (onSetIndexingPaused) onSetIndexingPaused(payload.scope, payload.paused != 0);
        break;
    }

    default:
        goto disconnected; // reply-only types are illegal from a client
        }
    }

disconnected:
    if (subscribed) {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        subscribedClients_.erase(
            std::remove(subscribedClients_.begin(), subscribedClients_.end(), client), subscribedClients_.end());
    }
    client->Close();
}

} // namespace ffengine
