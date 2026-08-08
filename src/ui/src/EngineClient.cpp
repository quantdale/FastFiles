#include "EngineClient.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <thread>

#include "EngineDiscovery.h"
#include "ffipc/PipeFraming.h"
#include "ffprotocol/Diagnostics.h"
#include "ffsetup/Identifiers.h"

namespace ffui {

namespace {
using ffprotocol::UiMessageType;
constexpr std::chrono::milliseconds kReconnectDelay{2000};
// recovery-degraded-browsing D2: at most this many connect attempts per
// cycle (~10 s at the 2 s cadence) before the explicit Unavailable state.
constexpr int kMaxConnectAttempts = 5;

bool ReadUiFrame(HANDLE pipe, std::atomic<bool>& running, ffipc::ReceivedFrame& frame) {
    for (;;) {
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytesAvailable, nullptr)) return false;
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

uint32_t LoadSharedU32(const uint32_t* value) {
    // The UI deliberately maps the section with FILE_MAP_READ.  An
    // InterlockedCompareExchange-based load is not read-only: when the
    // comparand matches it performs a store and faults on this mapping.
    // Snapshot fields are naturally aligned and the process is 64-bit, so an
    // acquire barrier followed by a volatile load gives the required
    // cross-process publication ordering without mutating shared memory.
    MemoryBarrier();
    return *reinterpret_cast<const volatile uint32_t*>(value);
}

uint64_t LoadSharedU64(const uint64_t* value) {
    MemoryBarrier();
    return *reinterpret_cast<const volatile uint64_t*>(value);
}

bool WaitForNamedPipe(const std::wstring& pipeName, const std::atomic<bool>& running,
                      DWORD timeoutMilliseconds) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
    while (running.load()) {
        if (WaitNamedPipeW(pipeName.c_str(), 200)) {
            return true;
        }
        if (GetTickCount64() >= deadline) {
            return false;
        }
        Sleep(50);
    }
    return false;
}
}

EngineClient::~EngineClient() {
    Stop();
}

void EngineClient::LaunchEngineIfNotRunning(const std::wstring& pipeName) {
    if (WaitNamedPipeW(pipeName.c_str(), 0)) {
        return; // already reachable
    }

    wchar_t ownPath[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, ownPath, static_cast<DWORD>(std::size(ownPath)));
    if (length == 0) {
        return;
    }

    // recovery-degraded-browsing D1: deterministic candidate order (same
    // dir, dev sibling, install dir); only validated regular files are
    // launched, and every failure is logged to the diagnostics log.
    bool launched = false;
    DWORD lastError = ERROR_FILE_NOT_FOUND;
    for (const std::wstring& candidate : EngineDiscoveryCandidates(std::wstring(ownPath, length))) {
        if (!IsValidEngineExecutable(candidate)) {
            continue;
        }
        std::wstring commandLine = L"\"" + candidate + L"\"";
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                           &startupInfo, &processInfo)) {
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                          candidate, {}, L"engine-launched", 0, 0});
            // Give the just-launched helper a short, bounded readiness
            // window before ConnectAndSubscribe performs its first open.
            // Without this, the normal interval between CreateProcess and
            // PipeListener::Start is reported as a failed connection and a
            // later retry can launch a duplicate helper if startup is slow.
            if (!WaitForNamedPipe(pipeName, running_, 10000)) {
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                              candidate, {}, L"engine-pipe-not-ready-after-launch", GetLastError(), 0});
            }
            launched = true;
            break;
        }
        lastError = GetLastError();
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      candidate, {}, L"engine-launch-failed", lastError, 0});
    }
    if (!launched) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      {}, {}, L"engine-not-found", lastError, 0});
    }
}

bool EngineClient::MapSnapshotSection(const std::wstring& sectionName) {
    UnmapSnapshotSection();
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sectionName.c_str());
    if (mapping == nullptr) {
        return false;
    }
    const auto* view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (view == nullptr) {
        CloseHandle(mapping);
        return false;
    }
    std::lock_guard<std::mutex> lock(mappingMutex_);
    mappingHandle_ = mapping;
    mappedView_ = view;
    return true;
}

void EngineClient::UnmapSnapshotSection() {
    std::lock_guard<std::mutex> lock(mappingMutex_);
    if (mappedView_ != nullptr) {
        UnmapViewOfFile(mappedView_);
        mappedView_ = nullptr;
    }
    if (mappingHandle_ != nullptr) {
        CloseHandle(mappingHandle_);
        mappingHandle_ = nullptr;
    }
}

std::optional<std::map<std::wstring, ffprotocol::SnapshotDirectory>> EngineClient::ReadSnapshot() const {
    std::lock_guard<std::mutex> lock(mappingMutex_);
    if (mappedView_ == nullptr) {
        return std::nullopt;
    }
    const auto* header = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(mappedView_);
    for (int attempt = 0; attempt != 3; ++attempt) {
        const uint64_t generationBefore = LoadSharedU64(&header->generation);
        const uint32_t activeSlot = LoadSharedU32(&header->activeSlot);
        if (activeSlot > 1) return std::nullopt;
        const uint32_t dataSize = LoadSharedU32(&header->slotDataSize[activeSlot]);
        if (dataSize > ffprotocol::kSnapshotSlotCapacityBytes) return std::nullopt;

        const uint8_t* slotBase = mappedView_ + sizeof(ffprotocol::SnapshotSharedHeader) +
            static_cast<size_t>(activeSlot) * ffprotocol::kSnapshotSlotCapacityBytes;
        auto parsed = ffprotocol::ParseSnapshot(slotBase, dataSize);
        if (!parsed) return std::nullopt;

        // A concurrent publish may have flipped slots while parsing. Retry a
        // bounded number of times rather than exposing a torn generation.
        if (LoadSharedU64(&header->generation) == generationBefore &&
            LoadSharedU32(&header->activeSlot) == activeSlot &&
            LoadSharedU32(&header->slotDataSize[activeSlot]) == dataSize) {
            return parsed;
        }
    }
    return std::nullopt;
}

bool EngineClient::ConnectAndSubscribe() {
    const auto fail = [](const wchar_t* state, DWORD error) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      {}, {}, state, error, 0});
        // Preserve the stage-specific status across diagnostic-file I/O so
        // ManagementLoop's aggregate failure record remains meaningful.
        SetLastError(error);
        return false;
    };

    HANDLE pipe = CreateFileW(enginePipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return fail(L"engine-connect-open-failed", GetLastError());
    }
    DWORD mode = PIPE_READMODE_BYTE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        const DWORD error = GetLastError();
        CloseHandle(pipe);
        return fail(L"engine-connect-mode-failed", error);
    }

    if (!ffipc::WriteFrame(pipe, static_cast<uint16_t>(UiMessageType::Subscribe))) {
        const DWORD error = GetLastError();
        CloseHandle(pipe);
        return fail(L"engine-connect-subscribe-write-failed", error);
    }
    auto reply = ffipc::ReadFrame(pipe);
    if (!reply) {
        const DWORD error = GetLastError();
        CloseHandle(pipe);
        return fail(L"engine-connect-subscribe-read-failed", error);
    }
    if (ffprotocol::ToUiMessageType(reply->header.messageType) != UiMessageType::SubscribeAck) {
        CloseHandle(pipe);
        return fail(L"engine-connect-unexpected-reply", ERROR_INVALID_DATA);
    }
    if (reply->payload.size() < sizeof(ffprotocol::SubscribeAckPayload)) {
        CloseHandle(pipe);
        return fail(L"engine-connect-short-ack", ERROR_INVALID_DATA);
    }

    ffprotocol::SubscribeAckPayload header{};
    std::memcpy(&header, reply->payload.data(), sizeof(header));
    const size_t expectedSize = sizeof(header) + static_cast<size_t>(header.sectionNameLengthChars) * sizeof(wchar_t);
    if (reply->payload.size() != expectedSize) {
        CloseHandle(pipe);
        return fail(L"engine-connect-malformed-ack", ERROR_INVALID_DATA);
    }
    std::wstring sectionName(
        reinterpret_cast<const wchar_t*>(reply->payload.data() + sizeof(header)), header.sectionNameLengthChars);

    if (!MapSnapshotSection(sectionName)) {
        const DWORD error = GetLastError();
        CloseHandle(pipe);
        return fail(L"engine-connect-snapshot-map-failed", error);
    }

    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        // Stop() may race a slow handshake. Do not publish a newly-connected
        // handle after shutdown has begun, and never replace a live handle
        // behind the writer's back.
        if (!running_.load() || pipe_.load() != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
            return false;
        }
        pipe_ = pipe;
    }
    // Wake the invalidation worker after the control pipe becomes usable. Any
    // directory requests queued while the engine was starting must remain
    // pending until this point; dropping them leaves the corresponding column
    // permanently empty.
    invalidationCv_.notify_all();
    ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                  {}, {}, L"engine-connect-succeeded", 0, 0});
    return true;
}

void EngineClient::ReaderLoop() {
    for (;;) {
        HANDLE pipe = pipe_.load();
        ffipc::ReceivedFrame frame;
        if (!ReadUiFrame(pipe, running_, frame)) {
            break;
        }
        auto messageType = ffprotocol::ToUiMessageType(frame.header.messageType);
        if (!messageType) {
            break;
        }

        switch (*messageType) {
            case UiMessageType::NewGeneration:
                if (onNewGeneration_) {
                    onNewGeneration_();
                }
                break;

            case UiMessageType::EngineStatus: {
                if (frame.payload.size() != sizeof(ffprotocol::EngineStatusPayload)) {
                    break;
                }
                ffprotocol::EngineStatusPayload status{};
                std::memcpy(&status, frame.payload.data(), sizeof(status));
                SetConnectionState(EngineConnectionState::Connected,
                                   status.status == ffprotocol::PrivilegedPathStatus::Active);
                break;
            }

            case UiMessageType::DirectoryError: {
                if (frame.payload.size() < sizeof(ffprotocol::DirectoryErrorPayload)) {
                    break;
                }
                ffprotocol::DirectoryErrorPayload header{};
                std::memcpy(&header, frame.payload.data(), sizeof(header));
                const size_t expected = sizeof(header) + static_cast<size_t>(header.pathLengthChars) * sizeof(wchar_t);
                if (frame.payload.size() != expected) {
                    break;
                }
                std::wstring path(
                    reinterpret_cast<const wchar_t*>(frame.payload.data() + sizeof(header)), header.pathLengthChars);
                if (onDirectoryError_) {
                    onDirectoryError_(path, header.reason);
                }
                break;
            }

            case UiMessageType::UnavailableVolumes: {
                if (frame.payload.size() < sizeof(ffprotocol::UnavailableVolumesHeader)) {
                    break;
                }
                ffprotocol::UnavailableVolumesHeader header{};
                std::memcpy(&header, frame.payload.data(), sizeof(header));
                const size_t expected = sizeof(header)
                    + static_cast<size_t>(header.count) * sizeof(ffprotocol::UnavailableVolumeRecord);
                if (expected != frame.payload.size()) {
                    break;
                }
                std::vector<ffprotocol::UnavailableVolumeRecord> records(header.count);
                if (!records.empty()) {
                    std::memcpy(records.data(), frame.payload.data() + sizeof(header),
                                records.size() * sizeof(ffprotocol::UnavailableVolumeRecord));
                }
                UnavailableVolumesCallback callback;
                {
                    std::lock_guard<std::mutex> lock(volumeCallbackMutex_);
                    callback = std::move(onUnavailableVolumes_);
                }
                if (callback) {
                    callback(std::move(records));
                }
                break;
            }

            case UiMessageType::ForgetUnavailableVolumeResult: {
                if (frame.payload.size() != sizeof(ffprotocol::ForgetUnavailableVolumeResultPayload)) {
                    break;
                }
                ffprotocol::ForgetUnavailableVolumeResultPayload payload{};
                std::memcpy(&payload, frame.payload.data(), sizeof(payload));
                if (!ffprotocol::IsForgetUnavailableVolumeStatusValid(payload.status)) {
                    break;
                }
                ForgetUnavailableVolumeCallback callback;
                {
                    std::lock_guard<std::mutex> lock(volumeCallbackMutex_);
                    callback = std::move(onForgetUnavailableVolume_);
                }
                if (callback) {
                    callback(payload);
                }
                break;
            }

            case UiMessageType::FolderAggregateResult: {
                if (frame.payload.size() != sizeof(ffprotocol::FolderAggregateResultPayload)) {
                    break;
                }
                ffprotocol::FolderAggregateResultPayload payload{};
                std::memcpy(&payload, frame.payload.data(), sizeof(payload));
                if (!ffprotocol::IsFolderAggregateStatusValid(payload.status)) {
                    break;
                }
                FolderAggregateCallback callback;
                {
                    std::lock_guard<std::mutex> lock(aggregateCallbackMutex_);
                    auto it = aggregateCallbacks_.find(payload.requestId);
                    if (it != aggregateCallbacks_.end()) {
                        callback = std::move(it->second);
                        aggregateCallbacks_.erase(it);
                    }
                }
                if (callback) {
                    callback(payload.requestId, payload.status, payload.itemCount, payload.totalSizeBytes);
                }
                break;
            }

            case UiMessageType::IndexedSearchResult: {
                const auto response = ffprotocol::ParseIndexedSearchResponse(
                    frame.payload.data(), frame.payload.size());
                if (!response) {
                    break;
                }
                IndexedSearchCallback callback;
                {
                    std::lock_guard<std::mutex> lock(indexedSearchCallbackMutex_);
                    auto it = indexedSearchCallbacks_.find(response->requestId);
                    if (it != indexedSearchCallbacks_.end()) {
                        callback = std::move(it->second);
                        indexedSearchCallbacks_.erase(it);
                    }
                }
                if (callback) {
                    callback(std::move(*response));
                }
                break;
            }

            case UiMessageType::VolumeStatus: {
                if (frame.payload.size() < sizeof(ffprotocol::VolumeStatusHeader)) {
                    break;
                }
                ffprotocol::VolumeStatusHeader header{};
                std::memcpy(&header, frame.payload.data(), sizeof(header));
                const size_t expected = sizeof(header)
                    + static_cast<size_t>(header.count) * sizeof(ffprotocol::VolumeStatusRecord);
                if (expected != frame.payload.size()) {
                    break;
                }
                std::vector<ffprotocol::VolumeStatusRecord> records(header.count);
                if (!records.empty()) {
                    std::memcpy(records.data(), frame.payload.data() + sizeof(header),
                                records.size() * sizeof(ffprotocol::VolumeStatusRecord));
                }
                VolumeStatusCallback callback;
                {
                    std::lock_guard<std::mutex> lock(volumeStatusCallbackMutex_);
                    callback = std::move(onVolumeStatus_);
                }
                if (callback) {
                    callback(std::move(records));
                }
                break;
            }

            default:
                break; // ignore anything unexpected rather than tearing down the UI
        }
    }
}

void EngineClient::ManagementLoop() {
    while (running_) {
        SetConnectionState(EngineConnectionState::Starting, false);
        LaunchEngineIfNotRunning(enginePipeName_);

        // recovery-degraded-browsing D2: bounded attempt cycle. After
        // kMaxConnectAttempts the loop parks in the Unavailable state until
        // RetryConnect() or Stop() -- never an infinite silent retry loop.
        bool connected = false;
        int attempts = 0;
        while (running_ && !connected && attempts < kMaxConnectAttempts) {
            ++attempts;
            if (ConnectAndSubscribe()) {
                connected = true;
                break;
            }
            ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                          {}, {}, L"engine-connect-failed", GetLastError(), 0});
            if (!WaitForRetryDelay()) {
                return;
            }
        }
        if (!running_) {
            break;
        }
        if (!connected) {
            SetConnectionState(EngineConnectionState::Unavailable, false);
            ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                          {}, {}, L"engine-unavailable", 0, 0});
            std::unique_lock<std::mutex> lock(retryMutex_);
            retryCv_.wait(lock, [this] { return !running_ || retryRequested_.exchange(false); });
            continue;
        }

        SetConnectionState(EngineConnectionState::Connected, false);
        ReaderLoop(); // blocks until disconnect

        ClosePipe();
        FailIndexedSearchCallbacks();
        invalidationCv_.notify_all();
        UnmapSnapshotSection();
        if (!running_) {
            break;
        }
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      {}, {}, L"engine-disconnected", 0, 0});
        // Loop: re-enter Starting and run the same bounded reconnect cycle.
    }
}

void EngineClient::SetConnectionState(EngineConnectionState state, bool privilegedPathActive) {
    if (onStatus_) {
        onStatus_(state, privilegedPathActive);
    }
}

void EngineClient::FailIndexedSearchCallbacks() {
    std::vector<std::pair<uint64_t, IndexedSearchCallback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(indexedSearchCallbackMutex_);
        callbacks.reserve(indexedSearchCallbacks_.size());
        for (auto& [requestId, callback] : indexedSearchCallbacks_) {
            callbacks.emplace_back(requestId, std::move(callback));
        }
        indexedSearchCallbacks_.clear();
    }
    for (auto& [requestId, callback] : callbacks) {
        if (!callback) continue;
        ffprotocol::IndexedSearchResponse response;
        response.requestId = requestId;
        response.status = ffprotocol::IndexedSearchStatus::Unavailable;
        callback(std::move(response));
    }
}

bool EngineClient::WaitForRetryDelay() {
    std::unique_lock<std::mutex> lock(retryMutex_);
    retryCv_.wait_for(lock, kReconnectDelay, [this] { return !running_ || retryRequested_.exchange(false); });
    return running_;
}

void EngineClient::RetryConnect() {
    retryRequested_ = true;
    retryCv_.notify_all();
}

void EngineClient::Start(GenerationCallback onNewGeneration, StatusCallback onStatus, DirectoryErrorCallback onDirectoryError) {
    onNewGeneration_ = std::move(onNewGeneration);
    onStatus_ = std::move(onStatus);
    onDirectoryError_ = std::move(onDirectoryError);

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    wchar_t pipeName[128];
    swprintf(pipeName, std::size(pipeName), ffsetup::kUiCtrlPipeNameFormat, sessionId);
    enginePipeName_ = pipeName;

    running_ = true;
    managementThread_ = std::thread(&EngineClient::ManagementLoop, this);
    invalidationThread_ = std::thread(&EngineClient::InvalidationLoop, this);
}

void EngineClient::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    invalidationCv_.notify_all();
    retryCv_.notify_all();

    // CancelSynchronousIo targets whatever blocking call this specific
    // thread currently has pending (the reader loop's ReadFile), which is
    // more robust than CancelIoEx(handle) racing a concurrent CloseHandle.
    if (managementThread_.joinable()) {
        CancelSynchronousIo(managementThread_.native_handle());
    }
    if (invalidationThread_.joinable()) {
        CancelSynchronousIo(invalidationThread_.native_handle());
    }

    ClosePipe();
    if (managementThread_.joinable()) {
        managementThread_.join();
    }
    if (invalidationThread_.joinable()) {
        invalidationThread_.join();
    }
    UnmapSnapshotSection();
}

void EngineClient::RequestDirectory(const std::wstring& path) {
    if (path.empty() || path.size() > ffprotocol::kMaxPathLengthChars || !running_) return;
    {
        std::lock_guard<std::mutex> lock(invalidationMutex_);
        if (std::find(invalidationQueue_.begin(), invalidationQueue_.end(), path) == invalidationQueue_.end()) {
            invalidationQueue_.push_back(path);
        }
    }
    // Production state-transition evidence for separating input/hit-test
    // failures from pipe or engine failures during degraded browsing.
    ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                  path, {}, L"ui-directory-request-queued", 0, 0});
    invalidationCv_.notify_one();
}

void EngineClient::InvalidationLoop() {
    while (running_) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lock(invalidationMutex_);
            invalidationCv_.wait(lock, [this] {
                return !running_ || (!invalidationQueue_.empty() && pipe_.load() != INVALID_HANDLE_VALUE);
            });
            if (!running_) break;
            // Do not remove the request until the frame has been accepted by
            // the pipe.  The previous implementation popped first and lost
            // every request issued during startup/reconnect.
            path = invalidationQueue_.front();
        }
        if (SendDirectoryRequest(path)) {
            std::lock_guard<std::mutex> lock(invalidationMutex_);
            if (!invalidationQueue_.empty() && invalidationQueue_.front() == path) {
                invalidationQueue_.pop_front();
            }
        }
    }
}

bool EngineClient::SendDirectoryRequest(const std::wstring& path) {
    if (path.empty() || path.size() > ffprotocol::kMaxPathLengthChars) return false;

    std::vector<uint8_t> payload(sizeof(ffprotocol::RequestDirectoryHeader) + path.size() * sizeof(wchar_t));
    ffprotocol::RequestDirectoryHeader header{static_cast<uint16_t>(path.size())};
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), path.data(), path.size() * sizeof(wchar_t));

    if (WriteCurrentFrame(static_cast<uint16_t>(UiMessageType::RequestDirectory), payload.data(),
                          static_cast<uint32_t>(payload.size()))) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      path, {}, L"ui-directory-request-sent", 0, 0});
        return true;
    }
    invalidationCv_.notify_all();
    return false;
}

void EngineClient::ClosePipe() {
    std::lock_guard<std::mutex> lock(writeMutex_);
    HANDLE pipe = pipe_.exchange(INVALID_HANDLE_VALUE);
    if (pipe != INVALID_HANDLE_VALUE) {
        // Cancel any synchronous read/write still associated with this
        // connection before closing the handle. The owning worker is also
        // cancelled by Stop(); this call covers the reconnect path.
        CancelIoEx(pipe, nullptr);
        CloseHandle(pipe);
    }
    for (HANDLE retired : retiredPipes_) {
        if (retired != INVALID_HANDLE_VALUE) {
            CloseHandle(retired);
        }
    }
    retiredPipes_.clear();
}

bool EngineClient::WriteCurrentFrame(uint16_t messageType, const void* payload, uint32_t payloadSize) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    const HANDLE pipe = pipe_.load();
    if (pipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (ffipc::WriteFrame(pipe, messageType, payload, payloadSize)) {
        return true;
    }

    // A failed write means the reader loop may still be blocked on this
    // connection. Mark the exact handle stale while holding the writer lock;
    // ClosePipe() will then serialize the eventual close/reconnect and close
    // the retired handle, avoiding both a use-after-close and a handle leak.
    HANDLE expected = pipe;
    if (pipe_.compare_exchange_strong(expected, INVALID_HANDLE_VALUE)) {
        retiredPipes_.push_back(pipe);
        CancelIoEx(pipe, nullptr);
    }
    return false;
}

void EngineClient::ReloadIndexingConfig() {
    WriteCurrentFrame(static_cast<uint16_t>(ffprotocol::UiMessageType::ReloadIndexingConfig));
}

void EngineClient::RequestUnavailableVolumes(UnavailableVolumesCallback callback) {
    {
        std::lock_guard<std::mutex> lock(volumeCallbackMutex_);
        onUnavailableVolumes_ = std::move(callback);
    }
    const bool sent = WriteCurrentFrame(static_cast<uint16_t>(ffprotocol::UiMessageType::RequestUnavailableVolumes));
    if (!sent) {
        UnavailableVolumesCallback unavailable;
        {
            std::lock_guard<std::mutex> callbackLock(volumeCallbackMutex_);
            unavailable = std::move(onUnavailableVolumes_);
        }
        if (unavailable) {
            unavailable({});
        }
    }
}

void EngineClient::ForgetUnavailableVolume(int64_t volumeRowId, ForgetUnavailableVolumeCallback callback) {
    {
        std::lock_guard<std::mutex> lock(volumeCallbackMutex_);
        onForgetUnavailableVolume_ = std::move(callback);
    }
    const ffprotocol::ForgetUnavailableVolumePayload payload{volumeRowId};
    const bool sent = WriteCurrentFrame(static_cast<uint16_t>(ffprotocol::UiMessageType::ForgetUnavailableVolume),
                                        &payload, sizeof(payload));
    if (!sent) {
        ForgetUnavailableVolumeCallback failed;
        {
            std::lock_guard<std::mutex> callbackLock(volumeCallbackMutex_);
            failed = std::move(onForgetUnavailableVolume_);
        }
        if (failed) {
            failed({volumeRowId, ffprotocol::ForgetUnavailableVolumeStatus::Failed});
        }
    }
}

uint64_t EngineClient::RequestFolderAggregate(int64_t volumeRowId, uint64_t parentFrnLow, uint64_t parentFrnHigh, FolderAggregateCallback callback) {
    const uint64_t requestId = lastRequestId_.fetch_add(1) + 1;
    {
        std::lock_guard<std::mutex> lock(aggregateCallbackMutex_);
        aggregateCallbacks_[requestId] = std::move(callback);
    }
    ffprotocol::RequestFolderAggregatePayload payload{};
    payload.requestId = requestId;
    payload.volumeRowId = volumeRowId;
    payload.parentFrnLow = parentFrnLow;
    payload.parentFrnHigh = parentFrnHigh;

    const bool sent = WriteCurrentFrame(static_cast<uint16_t>(ffprotocol::UiMessageType::RequestFolderAggregate),
                                        &payload, sizeof(payload));
    if (!sent) {
        FolderAggregateCallback failed;
        {
            std::lock_guard<std::mutex> lock(aggregateCallbackMutex_);
            auto it = aggregateCallbacks_.find(requestId);
            if (it != aggregateCallbacks_.end()) {
                failed = std::move(it->second);
                aggregateCallbacks_.erase(it);
            }
        }
        if (failed) {
            failed(requestId, ffprotocol::FolderAggregateStatus::NotFound, 0, 0);
        }
    }
    return requestId;
}

void EngineClient::SetIndexingPaused(uint8_t scope, bool paused) {
    const ffprotocol::SetIndexingPausedPayload payload{scope, paused ? 1u : 0u};
    WriteCurrentFrame(static_cast<uint16_t>(ffprotocol::UiMessageType::SetIndexingPaused), &payload, sizeof(payload));
}

void EngineClient::RequestVolumeStatus(VolumeStatusCallback callback) {
    {
        std::lock_guard<std::mutex> lock(volumeStatusCallbackMutex_);
        onVolumeStatus_ = std::move(callback);
    }
    const bool sent = WriteCurrentFrame(static_cast<uint16_t>(ffprotocol::UiMessageType::RequestVolumeStatus));
    if (!sent) {
        VolumeStatusCallback failed;
        {
            std::lock_guard<std::mutex> callbackLock(volumeStatusCallbackMutex_);
            failed = std::move(onVolumeStatus_);
        }
        if (failed) {
            failed({});
        }
    }
}

uint64_t EngineClient::RequestIndexedSearch(const std::wstring& query, uint8_t scope,
                                            const std::wstring& scopePath, uint8_t sortField,
                                            bool descending, uint32_t maxResults,
                                            IndexedSearchCallback callback) {
    const uint64_t requestId = lastRequestId_.fetch_add(1) + 1;
    {
        std::lock_guard<std::mutex> lock(indexedSearchCallbackMutex_);
        indexedSearchCallbacks_[requestId] = std::move(callback);
    }

    ffprotocol::IndexedSearchRequest request;
    request.requestId = requestId;
    request.scope = scope;
    request.sortField = sortField;
    request.descending = descending;
    request.maxResults = maxResults;
    request.query = query;
    request.scopePath = scopePath;
    const auto payload = ffprotocol::SerializeIndexedSearchRequest(request);
    if (payload.empty()) {
        IndexedSearchCallback failed;
        {
            std::lock_guard<std::mutex> lock(indexedSearchCallbackMutex_);
            auto it = indexedSearchCallbacks_.find(requestId);
            if (it != indexedSearchCallbacks_.end()) {
                failed = std::move(it->second);
                indexedSearchCallbacks_.erase(it);
            }
        }
        if (failed) {
            ffprotocol::IndexedSearchResponse response;
            response.requestId = requestId;
            response.status = payload.empty() ? ffprotocol::IndexedSearchStatus::InvalidRequest
                                               : ffprotocol::IndexedSearchStatus::Unavailable;
            failed(std::move(response));
        }
        return requestId;
    }

    const bool sent = WriteCurrentFrame(static_cast<uint16_t>(UiMessageType::RequestIndexedSearch),
                                        payload.data(), static_cast<uint32_t>(payload.size()));
    if (!sent) {
        IndexedSearchCallback failed;
        {
            std::lock_guard<std::mutex> lock(indexedSearchCallbackMutex_);
            auto it = indexedSearchCallbacks_.find(requestId);
            if (it != indexedSearchCallbacks_.end()) {
                failed = std::move(it->second);
                indexedSearchCallbacks_.erase(it);
            }
        }
        if (failed) {
            ffprotocol::IndexedSearchResponse response;
            response.requestId = requestId;
            response.status = ffprotocol::IndexedSearchStatus::Unavailable;
            failed(std::move(response));
        }
    }
    return requestId;
}

} // namespace ffui
