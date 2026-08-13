#include "EngineClient.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <thread>

#include "EngineDiscovery.h"
#include "ffipc/PipeFraming.h"
#include "ffprotocol/Diagnostics.h"
#include "ffsetup/Identifiers.h"

namespace ffui {

namespace {
using ffprotocol::UiMessageType;
constexpr std::chrono::milliseconds kReconnectDelay{2000};
constexpr std::chrono::milliseconds kHandshakeTimeout{5000};
constexpr std::chrono::milliseconds kPipeWriteTimeout{5000};
// recovery-degraded-browsing D2: at most this many connect attempts per
// cycle (~10 s at the 2 s cadence) before the explicit Unavailable state.
constexpr int kMaxConnectAttempts = 5;

std::wstring GenerationSectionName(const std::wstring& prefix, uint64_t generation) {
    return prefix + L"." + std::to_wstring(generation);
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

bool IsSnapshotGenerationUsable(const uint8_t* view, uint64_t expectedGeneration) {
    if (view == nullptr) return false;
    const auto* header = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(view);
    if (!ffprotocol::IsSnapshotFormatCompatible(LoadSharedU32(&header->formatVersion))) {
        return false;
    }
    if (LoadSharedU64(&header->generation) != expectedGeneration || expectedGeneration == 0) {
        return false;
    }
    const uint64_t payloadSize = LoadSharedU64(&header->payloadSizeBytes);
    if (payloadSize > ffprotocol::kSnapshotMaximumPayloadBytes
        || payloadSize > static_cast<uint64_t>(std::numeric_limits<size_t>::max() - sizeof(*header))) {
        return false;
    }

    MEMORY_BASIC_INFORMATION memoryInfo{};
    if (VirtualQuery(view, &memoryInfo, sizeof(memoryInfo)) != sizeof(memoryInfo)) {
        return false;
    }
    const uint64_t requiredBytes = sizeof(*header) + payloadSize;
    return requiredBytes <= static_cast<uint64_t>(memoryInfo.RegionSize);
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

EngineClient::OwnedPipe::~OwnedPipe() {
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
        CloseHandle(handle);
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
        const EngineExecutableValidation validation = ValidateEngineExecutable(candidate);
        if (!validation.Accepted()) {
            if (validation.status != EngineExecutableValidationStatus::NotRegularFile) {
                ffprotocol::DiagnosticEvent rejection{};
                rejection.category = ffprotocol::DiagnosticCategory::VolumeStateTransition;
                rejection.code = ffprotocol::DiagnosticCode::FFDIAG_ENGINE_SIGNATURE_REJECTED;
                rejection.path = candidate;
                rejection.state = EngineExecutableValidationStatusName(validation.status);
                rejection.errorCode = ERROR_INVALID_DATA;
                if (validation.signerThumbprint.has_value()) {
                    rejection.signatureThumbprint = ffsetup::ThumbprintToHex(*validation.signerThumbprint);
                } else {
                    rejection.signatureThumbprint = L"<none>";
                }
                ffprotocol::AppendDiagnostic(rejection);
            }
            continue;
        }
        const std::wstring launchPath = validation.canonicalPath.empty() ? candidate : validation.canonicalPath;
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        if (CreateProcessW(launchPath.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                           &startupInfo, &processInfo)) {
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            ffprotocol::DiagnosticEvent launchedEvent{};
            launchedEvent.category = ffprotocol::DiagnosticCategory::VolumeStateTransition;
            launchedEvent.code = ffprotocol::DiagnosticCode::FFDIAG_ENGINE_LAUNCHED;
            launchedEvent.path = launchPath;
            launchedEvent.state = L"engine-launched";
            launchedEvent.signatureThumbprint = validation.signerThumbprint.has_value()
                ? ffsetup::ThumbprintToHex(*validation.signerThumbprint)
                : L"<development-override>";
            ffprotocol::AppendDiagnostic(launchedEvent);
            // Give the just-launched helper a short, bounded readiness
            // window before ConnectAndSubscribe performs its first open.
            // Without this, the normal interval between CreateProcess and
            // PipeListener::Start is reported as a failed connection and a
            // later retry can launch a duplicate helper if startup is slow.
            if (!WaitForNamedPipe(pipeName, running_, 10000)) {
                ffprotocol::DiagnosticEvent pipeEvent{};
                pipeEvent.category = ffprotocol::DiagnosticCategory::VolumeStateTransition;
                pipeEvent.code = ffprotocol::DiagnosticCode::FFDIAG_ENGINE_PIPE_NOT_READY;
                pipeEvent.path = launchPath;
                pipeEvent.state = L"engine-pipe-not-ready-after-launch";
                pipeEvent.errorCode = GetLastError();
                pipeEvent.signatureThumbprint = launchedEvent.signatureThumbprint;
                ffprotocol::AppendDiagnostic(pipeEvent);
            }
            launched = true;
            break;
        }
        lastError = GetLastError();
        ffprotocol::DiagnosticEvent launchFailure{};
        launchFailure.category = ffprotocol::DiagnosticCategory::VolumeStateTransition;
        launchFailure.code = ffprotocol::DiagnosticCode::FFDIAG_ENGINE_LAUNCH_FAILED;
        launchFailure.path = launchPath;
        launchFailure.state = L"engine-launch-failed";
        launchFailure.errorCode = lastError;
        launchFailure.signatureThumbprint = validation.signerThumbprint.has_value()
            ? ffsetup::ThumbprintToHex(*validation.signerThumbprint)
            : L"<development-override>";
        ffprotocol::AppendDiagnostic(launchFailure);
    }
    if (!launched) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_ENGINE_NOT_FOUND,
                                      {}, {}, L"engine-not-found", lastError, 0});
    }
}

bool EngineClient::MapSnapshotGeneration(const std::wstring& sectionPrefix, uint64_t generation) {
    if (sectionPrefix.empty()) return false;
    const std::wstring sectionName = GenerationSectionName(sectionPrefix, generation);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sectionName.c_str());
    if (mapping == nullptr) {
        return false;
    }
    const auto* view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (view == nullptr) {
        CloseHandle(mapping);
        return false;
    }
    if (!IsSnapshotGenerationUsable(view, generation)) {
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    HANDLE previousMapping = nullptr;
    const uint8_t* previousView = nullptr;
    {
        std::lock_guard<std::mutex> lock(mappingMutex_);
        previousMapping = mappingHandle_;
        previousView = mappedView_;
        mappingHandle_ = mapping;
        mappedView_ = view;
        snapshotSectionPrefix_ = sectionPrefix;
        mappedGeneration_ = generation;
    }
    if (previousView != nullptr) UnmapViewOfFile(previousView);
    if (previousMapping != nullptr) CloseHandle(previousMapping);
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
    snapshotSectionPrefix_.clear();
    mappedGeneration_ = 0;
}

std::optional<std::map<std::wstring, ffprotocol::SnapshotDirectory>> EngineClient::ReadSnapshot() const {
    auto result = ReadSnapshotWithGeneration();
    if (!result) return std::nullopt;
    return std::move(result->directories);
}

std::optional<SnapshotReadResult> EngineClient::ReadSnapshotWithGeneration() const {
    std::lock_guard<std::mutex> lock(mappingMutex_);
    if (mappedView_ == nullptr) {
        return std::nullopt;
    }
    const auto* header = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(mappedView_);
    if (!ffprotocol::IsSnapshotFormatCompatible(LoadSharedU32(&header->formatVersion))) {
        return std::nullopt;
    }
    const uint64_t generation = LoadSharedU64(&header->generation);
    if (generation == 0 || generation != mappedGeneration_) {
        return std::nullopt;
    }
    const uint64_t payloadSize = LoadSharedU64(&header->payloadSizeBytes);
    if (payloadSize > ffprotocol::kSnapshotMaximumPayloadBytes ||
        payloadSize > static_cast<uint64_t>(std::numeric_limits<size_t>::max() - sizeof(*header))) {
        return std::nullopt;
    }

    // The generation mapping is immutable after the final commit marker is
    // published. Parse the actual payload in place instead of copying it into
    // a second UI-owned buffer; the mapping remains held by mappingMutex_
    // until parsing has completed.
    const size_t dataSize = static_cast<size_t>(payloadSize);
    auto parsed = ffprotocol::ParseSnapshot(mappedView_ + sizeof(*header), dataSize);
    if (!parsed) return std::nullopt;
    return SnapshotReadResult{generation, std::move(*parsed)};
}

bool EngineClient::ConnectAndSubscribe() {
    const auto fail = [](const wchar_t* state, DWORD error) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_ENGINE_CONNECT_FAILED,
                                      {}, {}, state, error, 0});
        // Preserve the stage-specific status across diagnostic-file I/O so
        // ManagementLoop's aggregate failure record remains meaningful.
        SetLastError(error);
        return false;
    };

    const HANDLE rawPipe = CreateFileW(enginePipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_OVERLAPPED, nullptr);
    if (rawPipe == INVALID_HANDLE_VALUE) {
        return fail(L"engine-connect-open-failed", GetLastError());
    }
    const PipePtr pipe = std::make_shared<OwnedPipe>(rawPipe);
    {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        connectingPipe_ = pipe;
    }
    DWORD mode = PIPE_READMODE_BYTE;
    if (!SetNamedPipeHandleState(pipe->handle, &mode, nullptr, nullptr)) {
        const DWORD error = GetLastError();
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        return fail(L"engine-connect-mode-failed", error);
    }

    if (!ffipc::WriteFrameOverlapped(pipe->handle, static_cast<uint16_t>(UiMessageType::Subscribe), nullptr, 0,
                                     std::chrono::steady_clock::now() + kHandshakeTimeout)) {
        const DWORD error = GetLastError();
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        return fail(L"engine-connect-subscribe-write-failed", error);
    }
    auto reply = ffipc::ReadFrameOverlapped(pipe->handle, std::chrono::steady_clock::now() + kHandshakeTimeout);
    if (!reply) {
        const DWORD error = GetLastError();
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        return fail(L"engine-connect-subscribe-read-failed", error);
    }
    if (ffprotocol::ToUiMessageType(reply->header.messageType) != UiMessageType::SubscribeAck) {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        return fail(L"engine-connect-unexpected-reply", ERROR_INVALID_DATA);
    }
    if (reply->payload.size() < sizeof(ffprotocol::SubscribeAckPayload)) {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        return fail(L"engine-connect-short-ack", ERROR_INVALID_DATA);
    }

    ffprotocol::SubscribeAckPayload header{};
    std::memcpy(&header, reply->payload.data(), sizeof(header));
    const size_t expectedSize = sizeof(header) + static_cast<size_t>(header.sectionNameLengthChars) * sizeof(wchar_t);
    if (reply->payload.size() != expectedSize) {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        return fail(L"engine-connect-malformed-ack", ERROR_INVALID_DATA);
    }
    std::wstring sectionName(
        reinterpret_cast<const wchar_t*>(reply->payload.data() + sizeof(header)), header.sectionNameLengthChars);

    if (!MapSnapshotGeneration(sectionName, header.generationId)) {
        const DWORD error = GetLastError();
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        return fail(L"engine-connect-snapshot-map-failed", error);
    }

    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        // Stop() may race a slow handshake. Do not publish a newly-connected
        // handle after shutdown has begun, and never replace a live handle
        // behind the writer's back.
        std::lock_guard<std::mutex> stateLock(pipeStateMutex_);
        if (!running_.load() || pipe_ != nullptr) {
            if (connectingPipe_ == pipe) connectingPipe_.reset();
            return false;
        }
        pipe_ = pipe;
        if (connectingPipe_ == pipe) connectingPipe_.reset();
    }
    announcedGeneration_ = header.generationId;
    // Wake the invalidation worker after the control pipe becomes usable. Any
    // directory requests queued while the engine was starting must remain
    // pending until this point; dropping them leaves the corresponding column
    // permanently empty.
    invalidationCv_.notify_all();
    ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                  ffprotocol::DiagnosticCode::FFDIAG_ENGINE_CONNECT_SUCCEEDED,
                                  {}, {}, L"engine-connect-succeeded", 0, 0});
    return true;
}

void EngineClient::ReaderLoop() {
    PipePtr pipe;
    {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        pipe = pipe_;
    }
    if (!pipe) return;
    for (;;) {
        auto parsed = ffipc::ReadFrameOverlapped(pipe->handle, std::chrono::steady_clock::time_point::max());
        if (!parsed) break;
        ffipc::ReceivedFrame frame = std::move(*parsed);
        auto messageType = ffprotocol::ToUiMessageType(frame.header.messageType);
        if (!messageType) {
            break;
        }

        switch (*messageType) {
            case UiMessageType::NewGeneration:
                if (frame.payload.size() != sizeof(ffprotocol::NewGenerationPayload)) break;
                {
                    ffprotocol::NewGenerationPayload payload{};
                    std::memcpy(&payload, frame.payload.data(), sizeof(payload));
                    if (payload.generationId == 0) break;
                    const uint64_t announced = announcedGeneration_.load();
                    if (payload.generationId <= announced) break;
                    std::wstring sectionPrefix;
                    {
                        std::lock_guard<std::mutex> mappingLock(mappingMutex_);
                        sectionPrefix = snapshotSectionPrefix_;
                    }
                    // Remap before announcing the generation to the UI. If a
                    // new mapping is unavailable, the last good mapping stays
                    // usable and the UI does not discard it as stale.
                    if (!MapSnapshotGeneration(sectionPrefix, payload.generationId)) break;
                    announcedGeneration_.store(payload.generationId);
                    if (onNewGeneration_) onNewGeneration_(payload.generationId);
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
                                          ffprotocol::DiagnosticCode::FFDIAG_ENGINE_CONNECT_FAILED,
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
                                          ffprotocol::DiagnosticCode::FFDIAG_ENGINE_UNAVAILABLE,
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
                                      ffprotocol::DiagnosticCode::FFDIAG_ENGINE_DISCONNECTED,
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

    // Both the reader and writer paths use overlapped operations. Cancel the
    // exact handles before joining their owners; no helper-thread join is
    // allowed to define the shutdown deadline.
    PipePtr connecting;
    PipePtr current;
    {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        connecting = connectingPipe_;
        current = pipe_;
    }
    if (connecting) {
        CancelIoEx(connecting->handle, nullptr);
    }
    if (current) {
        CancelIoEx(current->handle, nullptr);
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
                                  ffprotocol::DiagnosticCode::FFDIAG_UI_DIRECTORY_REQUEST_QUEUED,
                                  path, {}, L"ui-directory-request-queued", 0, 0});
    invalidationCv_.notify_one();
}

void EngineClient::InvalidationLoop() {
    while (running_) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lock(invalidationMutex_);
            invalidationCv_.wait(lock, [this] {
                if (!running_ || invalidationQueue_.empty()) return !running_;
                std::lock_guard<std::mutex> pipeLock(pipeStateMutex_);
                return pipe_ != nullptr;
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
                                      ffprotocol::DiagnosticCode::FFDIAG_UI_DIRECTORY_REQUEST_SENT,
                                      path, {}, L"ui-directory-request-sent", 0, 0});
        return true;
    }
    invalidationCv_.notify_all();
    return false;
}

void EngineClient::ClosePipe() {
    PipePtr current;
    {
        std::lock_guard<std::mutex> stateLock(pipeStateMutex_);
        current = pipe_;
    }
    if (current) {
        CancelIoEx(current->handle, nullptr);
    }
    std::lock_guard<std::mutex> lock(writeMutex_);
    PipePtr pipe;
    std::vector<PipePtr> retired;
    {
        std::lock_guard<std::mutex> stateLock(pipeStateMutex_);
        pipe = std::move(pipe_);
        retired = std::move(retiredPipes_);
        retiredPipes_.clear();
    }
    if (pipe) {
        // Cancel any overlapped read/write still associated with this
        // connection before closing the handle. The pre-lock cancellation
        // above is what prevents a blocked writer from holding writeMutex_
        // forever while shutdown waits to acquire it.
        CancelIoEx(pipe->handle, nullptr);
    }
    for (const auto& retiredPipe : retired) {
        if (retiredPipe) {
            CancelIoEx(retiredPipe->handle, nullptr);
        }
    }
}

bool EngineClient::WriteCurrentFrame(uint16_t messageType, const void* payload, uint32_t payloadSize) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    PipePtr pipe;
    {
        std::lock_guard<std::mutex> stateLock(pipeStateMutex_);
        pipe = pipe_;
    }
    if (!pipe) {
        return false;
    }
    if (ffipc::WriteFrameOverlapped(
            pipe->handle, messageType, payload, payloadSize,
            std::chrono::steady_clock::now() + kPipeWriteTimeout)) {
        return true;
    }

    // A failed write means the reader loop may still be blocked on this
    // connection. Mark the exact handle stale while holding the writer lock;
    // ClosePipe() will then serialize the eventual close/reconnect and close
    // the retired handle, avoiding both a use-after-close and a handle leak.
    {
        std::lock_guard<std::mutex> stateLock(pipeStateMutex_);
        if (pipe_ == pipe) {
            retiredPipes_.push_back(std::move(pipe_));
        }
    }
    CancelIoEx(pipe->handle, nullptr);
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

void EngineClient::CancelIndexedSearch(uint64_t requestId) {
    if (requestId == 0) return;
    {
        std::lock_guard<std::mutex> lock(indexedSearchCallbackMutex_);
        indexedSearchCallbacks_.erase(requestId);
    }
    ffprotocol::CancelIndexedSearchPayload payload{requestId};
    WriteCurrentFrame(static_cast<uint16_t>(UiMessageType::CancelIndexedSearch),
                      &payload, sizeof(payload));
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
