#include "UiServer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <limits>
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


ffprotocol::PooledSnapshotDirectory ToPooledSnapshotDirectory(const EnumerationResult& result) {
    // performance-closure-and-multisurface-acceleration 6.1: degraded and
    // watcher listings build the pooled model directly (names land in the
    // directory's own pool; no per-entry owning strings).
    ffprotocol::PooledSnapshotDirectoryBuilder builder;
    builder.SetStatus(static_cast<ffprotocol::DirectoryEnumerationStatus>(result.status));
    for (const auto& entry : result.entries) {
        // allocated-size-storage-accuracy D8: the unprivileged
        // FindFirstFileEx path does not surface allocated size, so degraded
        // entries are unknown by construction -- never derived from logical
        // size, never rendered as 0 B.
        builder.AddEntry(entry.name, entry.isDirectory, entry.sizeBytes,
                         0, false, entry.attributes, entry.creationTime,
                         entry.lastModifiedTime, 0, 0, 0, 0, 0);
    }
    return std::move(builder).Build();
}

} // namespace

UiServer::SearchCoordinator::Permit::Permit(std::shared_ptr<SearchCoordinator> owner)
    : owner_(std::move(owner)) {}

UiServer::SearchCoordinator::Permit::~Permit() {
    if (owner_ != nullptr) owner_->Release();
}

UiServer::SearchCoordinator::Permit::Permit(Permit&& other) noexcept
    : owner_(std::move(other.owner_)) {}

UiServer::SearchCoordinator::Permit& UiServer::SearchCoordinator::Permit::operator=(Permit&& other) noexcept {
    if (this == &other) return *this;
    if (owner_ != nullptr) owner_->Release();
    owner_ = std::move(other.owner_);
    return *this;
}

std::optional<UiServer::SearchCoordinator::Permit> UiServer::SearchCoordinator::Acquire(
    const std::function<bool()>& cancelled) {
    const auto isCancelled = [&cancelled] { return cancelled && cancelled(); };
    std::unique_lock<std::mutex> lock(mutex_);
    if (stopping_ || isCancelled()) return std::nullopt;
    if (active_ < maxActive_ && waiters_.empty()) {
        ++active_;
        peakActive_ = (std::max)(peakActive_, active_);
        return std::optional<Permit>(Permit(shared_from_this()));
    }
    if (waiters_.size() >= maxQueued_) {
        ++rejected_;
        return std::nullopt;
    }

    const auto waiter = std::make_shared<Waiter>();
    waiters_.push_back(waiter);
    for (;;) {
        condition_.wait(lock, [&] {
            return stopping_ || isCancelled() ||
                   (!waiters_.empty() && waiters_.front() == waiter && active_ < maxActive_);
        });
        if (stopping_ || isCancelled()) {
            const auto it = std::find(waiters_.begin(), waiters_.end(), waiter);
            if (it != waiters_.end()) waiters_.erase(it);
            const double waitMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - waiter->enqueuedAt).count();
            totalQueueWaitMs_ += waitMs;
            maxQueueWaitMs_ = (std::max)(maxQueueWaitMs_, waitMs);
            if (!stopping_) ++queuedCancellations_;
            condition_.notify_all();
            return std::nullopt;
        }
        if (!waiters_.empty() && waiters_.front() == waiter && active_ < maxActive_) {
            waiters_.pop_front();
            const double waitMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - waiter->enqueuedAt).count();
            totalQueueWaitMs_ += waitMs;
            maxQueueWaitMs_ = (std::max)(maxQueueWaitMs_, waitMs);
            ++active_;
            peakActive_ = (std::max)(peakActive_, active_);
            return std::optional<Permit>(Permit(shared_from_this()));
        }
    }
}

void UiServer::SearchCoordinator::Notify() {
    condition_.notify_all();
}

void UiServer::SearchCoordinator::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
}

void UiServer::SearchCoordinator::SetLimitsForTesting(const size_t maxActive,
                                                       const size_t maxQueued) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        maxActive_ = maxActive == 0 ? kMaxActive : maxActive;
        maxQueued_ = maxQueued == 0 ? kMaxQueued : maxQueued;
    }
    condition_.notify_all();
}

void UiServer::SearchCoordinator::Release() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ != 0) --active_;
        ++completed_;
    }
    condition_.notify_all();
}

UiServer::SearchCoordinator::Stats UiServer::SearchCoordinator::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats stats;
    stats.active = active_;
    stats.queued = waiters_.size();
    stats.peakActive = peakActive_;
    stats.completed = completed_;
    stats.queuedCancellations = queuedCancellations_;
    stats.rejected = rejected_;
    stats.totalQueueWaitMs = totalQueueWaitMs_;
    stats.maxQueueWaitMs = maxQueueWaitMs_;
    return stats;
}

UiServer::ClientConnection::ClientConnection(HANDLE pipe, FrameWriter frameWriter,
                                             PipeCanceller pipeCanceller,
                                             std::shared_ptr<SearchCoordinator> searchCoordinator)
    : pipe_(std::make_shared<OwnedPipe>(pipe)), frameWriter_(std::move(frameWriter)),
      pipeCanceller_(std::move(pipeCanceller)), searchCoordinator_(std::move(searchCoordinator)) {
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
    if (searchCoordinator_ != nullptr) searchCoordinator_->Notify();
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
    if (searchCoordinator_ != nullptr) searchCoordinator_->Notify();
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
    if (searchCoordinator_ != nullptr) searchCoordinator_->Notify();
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
        const SearchCancellation cancellation = [token = task.cancellation] {
            return token->load(std::memory_order_acquire);
        };
        std::optional<SearchCoordinator::Permit> permit;
        if (searchCoordinator_ != nullptr) {
            permit = searchCoordinator_->Acquire(cancellation);
        }
        if (task.cancellation->load(std::memory_order_acquire)) {
            response.status = ffprotocol::IndexedSearchStatus::Cancelled;
        } else if (!permit.has_value()) {
            response.status = ffprotocol::IndexedSearchStatus::Unavailable;
        } else if (task.handler) {
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
            bool wireTruncated = false;
            auto payload = ffprotocol::SerializeIndexedSearchResponseV2(response, &wireTruncated);
            bool wireDroppedCandidates = false;
            if (!payload.empty() && wireTruncated && response.candidates.size() > 0) {
                ffprotocol::IndexedSearchResultHeader wireHeader{};
                if (payload.size() >= sizeof(wireHeader)) {
                    std::memcpy(&wireHeader, payload.data(), sizeof(wireHeader));
                    wireDroppedCandidates = wireHeader.count < response.candidates.size();
                }
            }
            if ((payload.empty() || wireDroppedCandidates) &&
                response.status != ffprotocol::IndexedSearchStatus::Cancelled) {
                // nonblocking-indexed-read-plane 6.4: a response that cannot
                // fit the normal bounded frame is never silently dropped or
                // reported as a complete result. Invalid candidate data also
                // takes this fail-closed path instead of disappearing.
                response.status = ffprotocol::IndexedSearchStatus::Unavailable;
                response.truncated = false;
                response.candidates.clear();
                payload = ffprotocol::SerializeIndexedSearchResponseV2(response);
            }
            if (!task.cancellation->load(std::memory_order_acquire) && !payload.empty()) {
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
    if (searchCoordinator_ != nullptr) searchCoordinator_->Notify();

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
    { // Drain, then release writeMutex_: wait for any writer already past the
        // closing_ check to finish its frame before the handle below can be
        // closed by the last PipePtr release.
        std::lock_guard<std::mutex> writeLock(writeMutex_);
    }
    {
        std::lock_guard<std::mutex> stateLock(stateMutex_);
        pipe = pipe_;
        pipe_.reset();
    }
    // Join WITHOUT holding writeMutex_. Ordering proof: closing_ was set
    // above, so every Write() that acquires writeMutex_ from now on observes
    // closing_ and returns false without I/O; the drain above guarantees no
    // writer that passed the check still holds the mutex or the handle. The
    // search worker itself calls Write(), so joining under writeMutex_ would
    // deadlock against the worker's own acquisition.
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
    connectionGate_->shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (connectionGate_->shutdownEvent == nullptr) {
        connectionGate_.reset();
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(searchCoordinatorMutex_);
        searchCoordinator_ = std::make_shared<SearchCoordinator>();
    }
    if (!snapshot_.Start(sessionId)) {
        connectionGate_.reset();
        std::lock_guard<std::mutex> lock(searchCoordinatorMutex_);
        searchCoordinator_.reset();
        return false;
    }

    auto descriptor = ffsetup::BuildCurrentUserPipeSecurityDescriptor();
    if (!descriptor) {
        snapshot_.Stop();
        connectionGate_.reset();
        std::lock_guard<std::mutex> lock(searchCoordinatorMutex_);
        searchCoordinator_.reset();
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
                                 if (connectionGate->stopping.load(std::memory_order_acquire)) {
                                     CloseHandle(pipe);
                                     return;
                                 }
                                 ++connectionGate->active;
                             }
                             ConnectionRegistration registration{connectionGate};
                             HandleConnection(pipe, connectionGate);
                         })) {
        running_ = false;
        snapshot_.Stop();
        securityDescriptor_ = {};
        connectionGate_.reset();
        std::lock_guard<std::mutex> lock(searchCoordinatorMutex_);
        searchCoordinator_.reset();
        return false;
    }
    return true;
}

void UiServer::Stop() {
    running_ = false;
    std::shared_ptr<SearchCoordinator> searchCoordinator;
    {
        std::lock_guard<std::mutex> lock(searchCoordinatorMutex_);
        searchCoordinator = searchCoordinator_;
    }
    if (searchCoordinator != nullptr) searchCoordinator->Stop();
    const auto connectionGate = connectionGate_;
    if (connectionGate != nullptr) {
        {
            std::lock_guard<std::mutex> lock(connectionGate->mutex);
            connectionGate->stopping.store(true, std::memory_order_release);
        }
        if (connectionGate->shutdownEvent != nullptr) {
            SetEvent(connectionGate->shutdownEvent);
        }
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
    {
        std::lock_guard<std::mutex> lock(searchCoordinatorMutex_);
        searchCoordinator_.reset();
    }
}

void UiServer::OnDirectoryChanged(const std::wstring& path) {
    auto result = EnumerateDirectoryDegraded(path);
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        directories_[path] = ToPooledSnapshotDirectory(result);
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
        ffprotocol::AppendDiagnostic(ffprotocol::MakeDiagnosticEvent(ffprotocol::DiagnosticCategory::IndexingError,
                                      ffprotocol::DiagnosticCode::FFDIAG_SNAPSHOT_PUBLISH_FAILED,
                                      {}, {}, L"snapshot-publish-failed", ERROR_WRITE_FAULT, 0));
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

void UiServer::MergeIndexDirectories(ffprotocol::PooledSnapshotMap indexDirectories) {
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
                ffprotocol::AppendDiagnostic(ffprotocol::MakeDiagnosticEvent(ffprotocol::DiagnosticCategory::IndexingError,
                                              ffprotocol::DiagnosticCode::FFDIAG_INDEXED_MERGE_SUPPRESSED,
                                              normalizedPath, {}, L"indexed-merge-suppressed",
                                              0, 0));
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
        // memory-bounded-indexed-scale 5.2: retained indexed directories are
        // capped by the recency-ordered working set. Stale-key cleanup above
        // already dropped the previous generation for the incoming volumes;
        // this bound additionally keeps total retention proportional to the
        // active browse working set instead of the whole indexed tree.
        EnforceIndexedWorkingSetCapLocked();
    }
    RepublishAndBroadcastGeneration();
}

void UiServer::MergeIndexDirectories(std::map<std::wstring, ffprotocol::SnapshotDirectory> indexDirectories) {
    // performance-closure-and-multisurface-acceleration 6.1 compatibility
    // overload: convert the owning map into the pooled model and delegate.
    // Production call sites pass the pooled export directly so the
    // eliminated per-entry allocations never reappear at this boundary.
    ffprotocol::PooledSnapshotMap pooled;
    for (auto& [path, directory] : indexDirectories) {
        ffprotocol::PooledSnapshotDirectoryBuilder builder;
        builder.SetStatus(directory.status);
        builder.SetIdentity(directory.volumeRowId, directory.directoryIdLow, directory.directoryIdHigh,
                            directory.parentIdLow, directory.parentIdHigh);
        for (auto& entry : directory.entries) {
            builder.AddEntry(entry.name, entry.isDirectory, entry.sizeBytes,
                             entry.allocatedSizeBytes, entry.allocatedSizeKnown, entry.attributes,
                             entry.creationTime, entry.lastModifiedTime, entry.volumeRowId,
                             entry.fileIdLow, entry.fileIdHigh, entry.parentIdLow, entry.parentIdHigh);
        }
        pooled.emplace(path, std::move(builder).Build());
    }
    MergeIndexDirectories(std::move(pooled));
}

void UiServer::EnforceIndexedWorkingSetCapLocked() {
    if (indexedWorkingSetCap_ == 0) {
        return;
    }
    // memory-bounded-indexed-scale 5.2: recency-ordered eviction over indexed
    // entries only. Observation epochs are monotonic across requests, watcher
    // refreshes, and indexed merges, so this retains the most recently
    // observed working set deterministically.
    std::vector<std::pair<uint64_t, const std::wstring*>> indexedEntries;
    indexedEntries.reserve(directories_.size());
    for (const auto& [path, directory] : directories_) {
        if (directory.volumeRowId == 0) continue;
        const auto obsIt = pathObservations_.find(path);
        const uint64_t epoch = obsIt != pathObservations_.end() ? obsIt->second.epoch : 0;
        indexedEntries.emplace_back(epoch, &path);
    }
    if (indexedEntries.size() <= indexedWorkingSetCap_) {
        return;
    }
    std::sort(indexedEntries.begin(), indexedEntries.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
    const size_t toEvict = indexedEntries.size() - indexedWorkingSetCap_;
    for (size_t index = 0; index < toEvict; ++index) {
        // review-B finding F1: `indexedEntries` stores pointers to directory
        // map keys; erasing the directory frees that node, so the key must be
        // copied before the first erase or the pathObservations lookup below
        // dereferences freed memory (ASan: heap-use-after-free, READ).
        const std::wstring victimPath = *indexedEntries[index].second;
        directories_.erase(victimPath);
        pathObservations_.erase(victimPath);
        ++workingSetEvictions_;
    }
}

UiServer::SnapshotWorkingSetStats UiServer::GetSnapshotWorkingSetStats() const {
    std::lock_guard<std::mutex> lock(directoriesMutex_);
    SnapshotWorkingSetStats stats;
    stats.totalDirectories = directories_.size();
    stats.retainedCap = indexedWorkingSetCap_;
    stats.workingSetEvictions = workingSetEvictions_;
    for (const auto& [path, directory] : directories_) {
        (void)path;
        if (directory.volumeRowId != 0) ++stats.indexedDirectories;
    }
    return stats;
}

void UiServer::SetIndexedWorkingSetCap(const size_t maxIndexedDirectories) {
    std::lock_guard<std::mutex> lock(directoriesMutex_);
    indexedWorkingSetCap_ = maxIndexedDirectories == 0 ? kDefaultMaxRetainedIndexedDirectories
                                                       : maxIndexedDirectories;
}

UiServer::IndexedSearchStats UiServer::GetIndexedSearchStats() const {
    std::shared_ptr<SearchCoordinator> coordinator;
    {
        std::lock_guard<std::mutex> lock(searchCoordinatorMutex_);
        coordinator = searchCoordinator_;
    }
    if (coordinator == nullptr) return {};
    const auto stats = coordinator->GetStats();
    return {stats.active, stats.queued, stats.peakActive, stats.completed,
            stats.queuedCancellations, stats.rejected, stats.totalQueueWaitMs,
            stats.maxQueueWaitMs};
}

void UiServer::SetIndexedSearchLimitsForTesting(const size_t maxActive, const size_t maxQueued) {
    std::shared_ptr<SearchCoordinator> coordinator;
    {
        std::lock_guard<std::mutex> lock(searchCoordinatorMutex_);
        coordinator = searchCoordinator_;
    }
    if (coordinator != nullptr) coordinator->SetLimitsForTesting(maxActive, maxQueued);
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
        ffprotocol::AppendDiagnostic(ffprotocol::MakeDiagnosticEvent(ffprotocol::DiagnosticCategory::DatabaseError,
                                      ffprotocol::DiagnosticCode::FFDIAG_FORGET_VOLUME_FAILED,
                                      {}, std::to_wstring(volumeRowId), L"forget-volume-failed", ERROR_WRITE_FAULT, 0));
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
        ffprotocol::AppendDiagnostic(ffprotocol::MakeDiagnosticEvent(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_ENGINE_STATUS_CHANGED,
                                      {}, {}, privilegedPathActive ? L"active" : L"unavailable", 0, 0));
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
    const auto snapshotDir = ToPooledSnapshotDirectory(result);
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
        ffprotocol::AppendDiagnostic(ffprotocol::MakeDiagnosticEvent(ffprotocol::DiagnosticCategory::InaccessibleDirectory,
                                      ffprotocol::DiagnosticCode::FFDIAG_DIRECTORY_ACCESS_DENIED,
                                      path, {}, L"access-denied", ERROR_ACCESS_DENIED, 0));
        SendDirectoryError(client, path, ffprotocol::DirectoryErrorReason::AccessDenied);
        // deterministic-missing-path-state D1: publish the failure status
        // into the snapshot so every subscribed client sees the terminal
        // state, not just the requesting one (task 4.1).
        RepublishAndBroadcastGeneration();
        return;
    }
    if (result.status == EnumerationStatus::NotFound) {
        ffprotocol::AppendDiagnostic(ffprotocol::MakeDiagnosticEvent(ffprotocol::DiagnosticCategory::InaccessibleDirectory,
                                      ffprotocol::DiagnosticCode::FFDIAG_DIRECTORY_NOT_FOUND,
                                      path, {}, L"not-found", ERROR_PATH_NOT_FOUND, 0));
        SendDirectoryError(client, path, ffprotocol::DirectoryErrorReason::NoLongerAvailable);
        RepublishAndBroadcastGeneration();
        return;
    }
    if (result.status == EnumerationStatus::IoError) {
        ffprotocol::AppendDiagnostic(ffprotocol::MakeDiagnosticEvent(ffprotocol::DiagnosticCategory::InaccessibleDirectory,
                                      ffprotocol::DiagnosticCode::FFDIAG_DIRECTORY_ENUMERATION_FAILED,
                                      path, {}, L"enumeration-failed", ERROR_READ_FAULT, 0));
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

void UiServer::HandleConnection(HANDLE pipe, const std::shared_ptr<ConnectionGate>& connectionGate) {
    std::shared_ptr<SearchCoordinator> searchCoordinator;
    {
        std::lock_guard<std::mutex> lock(searchCoordinatorMutex_);
        searchCoordinator = searchCoordinator_;
    }
    const ClientConnectionPtr client = std::make_shared<ClientConnection>(
        pipe, frameWriter_, pipeCanceller_, std::move(searchCoordinator));
    bool subscribed = false;

    for (;;) {
        if (connectionGate->stopping.load(std::memory_order_acquire)) {
            break;
        }
        const auto readPipe = client->Pipe();
        if (!readPipe) break;
        // Shutdown-aware blocking: no polling slice. The reader blocks on
        // INFINITE and is woken only by real I/O or the gate's shutdownEvent.
        // This preserves framing across any deadline expiry because no
        // partial-frame re-arm occurs.
        ffipc::ReceivedFrame frameStorage;
        const ffipc::FrameReadStatus readStatus = ffipc::ReadFrameOverlappedDetailed(
            readPipe->handle, connectionGate->shutdownEvent,
            std::chrono::steady_clock::time_point::max(), &frameStorage);
        if (readStatus != ffipc::FrameReadStatus::Success) {
            if (readStatus == ffipc::FrameReadStatus::Cancelled
                || connectionGate->stopping.load(std::memory_order_acquire)) {
                break;
            }
            break;
        }
        auto frame = std::optional<ffipc::ReceivedFrame>(std::move(frameStorage));
        if (!frame) break;
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
                    ffprotocol::AppendDiagnostic(ffprotocol::MakeDiagnosticEvent(ffprotocol::DiagnosticCategory::IndexingError,
                                                  ffprotocol::DiagnosticCode::FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID,
                                                  {}, {}, L"subscribe-without-valid-snapshot",
                                                  ERROR_INVALID_STATE, 0));
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
                ffprotocol::AppendDiagnostic(ffprotocol::MakeDiagnosticEvent(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                              ffprotocol::DiagnosticCode::FFDIAG_ENGINE_DIRECTORY_REQUEST_RECEIVED,
                                              path, {}, L"engine-directory-request-received", 0, 0));
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
