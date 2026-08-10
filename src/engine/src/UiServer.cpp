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
    return ffipc::WriteFrame(pipe, messageType, payload, payloadSize);
}

void DefaultPipeCanceller(HANDLE pipe) {
    if (pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe, nullptr);
    }
}

// Synchronous named-pipe handles cannot keep a blocking ReadFile pending
// while another thread writes on the same handle. Poll for a complete frame
// header so outbound UI notifications and inbound control requests can make
// progress concurrently (the service control loop uses the same pattern).
bool ReadUiFrame(HANDLE pipe, std::atomic<bool>& running, ffipc::ReceivedFrame& frame) {
    for (;;) {
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
            return false;
        }
        if (bytesAvailable >= sizeof(ffprotocol::FrameHeader)) {
            auto parsed = ffipc::ReadFrame(pipe);
            if (!parsed) return false;
            frame = std::move(*parsed);
            return true;
        }
        if (!running.load()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
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
        snapshotEntry.creationTime = entry.creationTime;
        snapshotEntry.lastModifiedTime = entry.lastModifiedTime;
        snapshotEntry.attributes = entry.attributes;
        directory.entries.push_back(std::move(snapshotEntry));
    }
    return directory;
}

} // namespace

UiServer::ClientConnection::ClientConnection(HANDLE pipe, FrameWriter frameWriter, PipeCanceller pipeCanceller)
    : pipe_(pipe), frameWriter_(std::move(frameWriter)), pipeCanceller_(std::move(pipeCanceller)) {}

bool UiServer::ClientConnection::Write(uint16_t messageType, const void* payload, uint32_t payloadSize) {
    // Serialize complete frames for this connection, but do not hold the
    // connection state lock while the external pipe write can block.
    std::lock_guard<std::mutex> writeLock(writeMutex_);
    HANDLE writerThread = OpenThread(THREAD_TERMINATE, FALSE, GetCurrentThreadId());
    HANDLE pipe = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> stateLock(stateMutex_);
        if (closing_) {
            if (writerThread != nullptr) {
                CloseHandle(writerThread);
            }
            return false;
        }
        pipe = pipe_;
        writerThread_ = writerThread;
    }
    const bool written = frameWriter_(pipe, messageType, payload, payloadSize);
    {
        std::lock_guard<std::mutex> stateLock(stateMutex_);
        writerThread_ = nullptr;
    }
    if (writerThread != nullptr) {
        CloseHandle(writerThread);
    }
    return written;
}

void UiServer::ClientConnection::CancelPendingWriteLocked() {
    if (writerThread_ != nullptr) {
        CancelSynchronousIo(writerThread_);
    }
}

void UiServer::ClientConnection::Cancel() {
    std::lock_guard<std::mutex> stateLock(stateMutex_);
    if (!closing_) {
        CancelPendingWriteLocked();
        pipeCanceller_(pipe_);
    }
}

void UiServer::ClientConnection::Close() {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> stateLock(stateMutex_);
        if (closing_) {
            return;
        }
        closing_ = true;
        pipe = pipe_;
        CancelPendingWriteLocked();
    }

    // Cancel before waiting for a concurrent writer. A writer that already
    // owns writeMutex_ is now guaranteed to unwind; a later writer observes
    // closing_ before it can issue I/O.
    pipeCanceller_(pipe);
    std::lock_guard<std::mutex> writeLock(writeMutex_);
    {
        std::lock_guard<std::mutex> stateLock(stateMutex_);
        pipe = pipe_;
        pipe_ = INVALID_HANDLE_VALUE;
    }
    if (pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe);
    }
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
    if (!snapshot_.Start(sessionId)) {
        return false;
    }

    auto descriptor = ffsetup::BuildCurrentUserPipeSecurityDescriptor();
    if (!descriptor) {
        return false;
    }
    securityDescriptor_ = std::move(*descriptor);

    wchar_t pipeName[128];
    swprintf(pipeName, std::size(pipeName), ffsetup::kUiCtrlPipeNameFormat, sessionId);

    running_ = true;
    if (!listener_.Start(pipeName, &securityDescriptor_.attributes,
                         [this](HANDLE pipe) { HandleConnection(pipe); })) {
        running_ = false;
        return false;
    }
    return true;
}

void UiServer::Stop() {
    running_ = false;
    listener_.Stop();

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
    }
    RepublishAndBroadcastGeneration();
}

bool UiServer::RepublishAndBroadcastGeneration() {
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        generation = snapshot_.Publish(directories_);
    }
    if (generation == 0) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                      {}, {}, L"snapshot-publish-failed", ERROR_WRITE_FAULT, 0});
        return false; // publish failed (e.g. exceeded slot capacity); nothing to announce
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
            directories_[normalizedPath] = std::move(directory);
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
                                      {}, std::to_wstring(volumeRowId), L"forget-volume-failed", ERROR_WRITE_FAULT, 0});
    }
    return client->Write(static_cast<uint16_t>(ffprotocol::UiMessageType::ForgetUnavailableVolumeResult),
                         &payload, sizeof(payload));
}

bool UiServer::SendFolderAggregateResult(const ClientConnectionPtr& client, uint64_t requestId,
                                         ffprotocol::FolderAggregateStatus status, uint64_t itemCount,
                                         uint64_t totalSizeBytes) {
    const ffprotocol::FolderAggregateResultPayload payload{requestId, status, itemCount, totalSizeBytes};
    return client->Write(static_cast<uint16_t>(ffprotocol::UiMessageType::FolderAggregateResult),
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

void UiServer::HandleRequestDirectory(const ClientConnectionPtr& client, const std::wstring& path) {
    if (onActivity) {
        onActivity();
    }

    watcher_.Watch(path, [this](const std::wstring& changedPath) { OnDirectoryChanged(changedPath); });

    auto result = EnumerateDirectoryDegraded(path);
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        directories_[path] = ToSnapshotDirectory(result);
    }

    if (result.status == EnumerationStatus::AccessDenied) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::InaccessibleDirectory,
                                      path, {}, L"access-denied", ERROR_ACCESS_DENIED, 0});
        SendDirectoryError(client, path, ffprotocol::DirectoryErrorReason::AccessDenied);
        return;
    }
    if (result.status == EnumerationStatus::NotFound) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::InaccessibleDirectory,
                                      path, {}, L"not-found", ERROR_PATH_NOT_FOUND, 0});
        SendDirectoryError(client, path, ffprotocol::DirectoryErrorReason::NoLongerAvailable);
        return;
    }
    if (result.status == EnumerationStatus::IoError) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::InaccessibleDirectory,
                                      path, {}, L"enumeration-failed", ERROR_READ_FAULT, 0});
        SendDirectoryError(client, path, ffprotocol::DirectoryErrorReason::EnumerationFailed);
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
        SendFolderAggregateResult(client, request.requestId, ffprotocol::FolderAggregateStatus::NotFound, 0, 0);
        return;
    }

    const ffindexstore::FileId parentFrn{request.parentFrnLow, request.parentFrnHigh};
    const auto aggregateOpt = onRequestFolderAggregate(static_cast<ffindexstore::VolumeRowId>(request.volumeRowId), parentFrn);

    if (!aggregateOpt) {
        SendFolderAggregateResult(client, request.requestId, ffprotocol::FolderAggregateStatus::NotFound, 0, 0);
    } else {
        const auto& aggregate = *aggregateOpt;
        SendFolderAggregateResult(client, request.requestId, ffprotocol::FolderAggregateStatus::Resolved,
                                  aggregate.itemCount, aggregate.totalSizeBytes);
    }
}

bool UiServer::HandleRequestIndexedSearch(const ClientConnectionPtr& client,
                                          const std::vector<uint8_t>& framePayload) {
    const auto request = ffprotocol::ParseIndexedSearchRequest(framePayload.data(), framePayload.size());
    if (!request) {
        return false;
    }
    ffprotocol::IndexedSearchResponse response;
    response.requestId = request->requestId;
    if (onRequestIndexedSearch) {
        response = onRequestIndexedSearch(*request);
        // The request id is owned by the UI protocol even if an internal
        // callback accidentally returned a default/foreign id.
        response.requestId = request->requestId;
    } else {
        response.status = ffprotocol::IndexedSearchStatus::Unavailable;
    }
    return SendIndexedSearchResult(client, response);
}

void UiServer::HandleConnection(HANDLE pipe) {
    const ClientConnectionPtr client = std::make_shared<ClientConnection>(pipe, frameWriter_, pipeCanceller_);
    bool subscribed = false;

    for (;;) {
        ffipc::ReceivedFrame frame;
        if (!ReadUiFrame(client->Pipe(), running_, frame)) {
            break;
        }
        auto messageType = ffprotocol::ToUiMessageType(frame.header.messageType);
        if (!messageType) {
            break; // unrecognized message type: protocol violation
        }

        switch (*messageType) {
            case UiMessageType::Subscribe: {
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
                if (frame.payload.size() < sizeof(ffprotocol::RequestDirectoryHeader)) {
                    goto disconnected;
                }
                ffprotocol::RequestDirectoryHeader header{};
                std::memcpy(&header, frame.payload.data(), sizeof(header));
                if (!ffprotocol::IsUiPathLengthValid(header.pathLengthChars)) {
                    goto disconnected;
                }
                const size_t expectedSize = sizeof(header) + static_cast<size_t>(header.pathLengthChars) * sizeof(wchar_t);
                if (frame.payload.size() != expectedSize) {
                    goto disconnected;
                }
                std::wstring path(
                    reinterpret_cast<const wchar_t*>(frame.payload.data() + sizeof(header)), header.pathLengthChars);
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                              path, {}, L"engine-directory-request-received", 0, 0});
                HandleRequestDirectory(client, path);
                break;
            }

            case UiMessageType::ReloadIndexingConfig:
                if (!frame.payload.empty()) goto disconnected;
                if (onReloadIndexingConfig) onReloadIndexingConfig();
                break;

            case UiMessageType::RequestUnavailableVolumes:
                if (!frame.payload.empty() || !SendUnavailableVolumes(client)) goto disconnected;
                break;

    case UiMessageType::ForgetUnavailableVolume: {
        if (frame.payload.size() != sizeof(ffprotocol::ForgetUnavailableVolumePayload)) goto disconnected;
        ffprotocol::ForgetUnavailableVolumePayload payload{};
        std::memcpy(&payload, frame.payload.data(), sizeof(payload));
        if (payload.volumeRowId <= 0 || !SendForgetUnavailableVolumeResult(client, payload.volumeRowId)) {
            goto disconnected;
        }
        break;
    }

    case UiMessageType::RequestFolderAggregate: {
        if (frame.payload.size() != sizeof(ffprotocol::RequestFolderAggregatePayload)) goto disconnected;
        HandleRequestFolderAggregate(client, frame.payload);
        break;
    }

    case UiMessageType::RequestIndexedSearch:
        if (!HandleRequestIndexedSearch(client, frame.payload)) goto disconnected;
        break;

    case UiMessageType::RequestVolumeStatus:
        if (!frame.payload.empty() || !SendVolumeStatus(client)) goto disconnected;
        break;

    case UiMessageType::SetIndexingPaused: {
        if (frame.payload.size() != sizeof(ffprotocol::SetIndexingPausedPayload)) goto disconnected;
        ffprotocol::SetIndexingPausedPayload payload{};
        std::memcpy(&payload, frame.payload.data(), sizeof(payload));
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
