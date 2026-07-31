#include "EngineClient.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "ffipc/PipeFraming.h"
#include "ffsetup/Identifiers.h"

namespace ffui {

namespace {
using ffprotocol::UiMessageType;
constexpr std::chrono::milliseconds kReconnectDelay{2000};
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
    std::wstring directory(ownPath, length);
    const size_t lastSlash = directory.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return;
    }
    directory = directory.substr(0, lastSlash);

    std::wstring commandLine = L"\"" + directory + L"\\" + ffsetup::kEngineExeName + L"\"";

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startupInfo, &processInfo)) {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }
}

bool EngineClient::MapSnapshotSection(const std::wstring& sectionName) {
    UnmapSnapshotSection();

    mappingHandle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, sectionName.c_str());
    if (mappingHandle_ == nullptr) {
        return false;
    }
    mappedView_ = static_cast<const uint8_t*>(MapViewOfFile(mappingHandle_, FILE_MAP_READ, 0, 0, 0));
    if (mappedView_ == nullptr) {
        CloseHandle(mappingHandle_);
        mappingHandle_ = nullptr;
        return false;
    }
    return true;
}

void EngineClient::UnmapSnapshotSection() {
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
    if (mappedView_ == nullptr) {
        return std::nullopt;
    }
    const auto* header = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(mappedView_);
    const uint32_t activeSlot = header->activeSlot;
    const uint32_t dataSize = header->activeSlotDataSize;

    const uint8_t* slotBase = mappedView_ + sizeof(ffprotocol::SnapshotSharedHeader) +
        static_cast<size_t>(activeSlot) * ffprotocol::kSnapshotSlotCapacityBytes;
    return ffprotocol::ParseSnapshot(slotBase, dataSize);
}

bool EngineClient::ConnectAndSubscribe() {
    HANDLE pipe = CreateFileW(enginePipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    if (!ffipc::WriteFrame(pipe, static_cast<uint16_t>(UiMessageType::Subscribe))) {
        CloseHandle(pipe);
        return false;
    }
    auto reply = ffipc::ReadFrame(pipe);
    if (!reply || ffprotocol::ToUiMessageType(reply->header.messageType) != UiMessageType::SubscribeAck) {
        CloseHandle(pipe);
        return false;
    }
    if (reply->payload.size() < sizeof(ffprotocol::SubscribeAckPayload)) {
        CloseHandle(pipe);
        return false;
    }

    ffprotocol::SubscribeAckPayload header{};
    std::memcpy(&header, reply->payload.data(), sizeof(header));
    const size_t expectedSize = sizeof(header) + static_cast<size_t>(header.sectionNameLengthChars) * sizeof(wchar_t);
    if (reply->payload.size() != expectedSize) {
        CloseHandle(pipe);
        return false;
    }
    std::wstring sectionName(
        reinterpret_cast<const wchar_t*>(reply->payload.data() + sizeof(header)), header.sectionNameLengthChars);

    if (!MapSnapshotSection(sectionName)) {
        CloseHandle(pipe);
        return false;
    }

    pipe_ = pipe;
    return true;
}

void EngineClient::ReaderLoop() {
    for (;;) {
        HANDLE pipe = pipe_.load();
        auto frame = ffipc::ReadFrame(pipe);
        if (!frame) {
            break;
        }
        auto messageType = ffprotocol::ToUiMessageType(frame->header.messageType);
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
                if (frame->payload.size() != sizeof(ffprotocol::EngineStatusPayload)) {
                    break;
                }
                ffprotocol::EngineStatusPayload status{};
                std::memcpy(&status, frame->payload.data(), sizeof(status));
                if (onStatus_) {
                    onStatus_(status.status == ffprotocol::PrivilegedPathStatus::Active);
                }
                break;
            }

            case UiMessageType::DirectoryError: {
                if (frame->payload.size() < sizeof(ffprotocol::DirectoryErrorPayload)) {
                    break;
                }
                ffprotocol::DirectoryErrorPayload header{};
                std::memcpy(&header, frame->payload.data(), sizeof(header));
                const size_t expected = sizeof(header) + static_cast<size_t>(header.pathLengthChars) * sizeof(wchar_t);
                if (frame->payload.size() != expected) {
                    break;
                }
                std::wstring path(
                    reinterpret_cast<const wchar_t*>(frame->payload.data() + sizeof(header)), header.pathLengthChars);
                if (onDirectoryError_) {
                    onDirectoryError_(path, header.reason);
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
        LaunchEngineIfNotRunning(enginePipeName_);

        if (!ConnectAndSubscribe()) {
            if (onStatus_) {
                onStatus_(false);
            }
            std::this_thread::sleep_for(kReconnectDelay);
            continue;
        }

        ReaderLoop(); // blocks until disconnect

        HANDLE pipe = pipe_.exchange(INVALID_HANDLE_VALUE);
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
        }
        UnmapSnapshotSection();
        if (onStatus_) {
            onStatus_(false);
        }

        if (!running_) {
            break;
        }
        std::this_thread::sleep_for(kReconnectDelay);
    }
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
}

void EngineClient::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;

    // CancelSynchronousIo targets whatever blocking call this specific
    // thread currently has pending (the reader loop's ReadFile), which is
    // more robust than CancelIoEx(handle) racing a concurrent CloseHandle.
    if (managementThread_.joinable()) {
        CancelSynchronousIo(managementThread_.native_handle());
    }

    HANDLE pipe = pipe_.exchange(INVALID_HANDLE_VALUE);
    if (pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe);
    }
    if (managementThread_.joinable()) {
        managementThread_.join();
    }
    UnmapSnapshotSection();
}

void EngineClient::RequestDirectory(const std::wstring& path) {
    HANDLE pipe = pipe_.load();
    if (pipe == INVALID_HANDLE_VALUE) {
        return;
    }

    std::vector<uint8_t> payload(sizeof(ffprotocol::RequestDirectoryHeader) + path.size() * sizeof(wchar_t));
    ffprotocol::RequestDirectoryHeader header{static_cast<uint16_t>(path.size())};
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), path.data(), path.size() * sizeof(wchar_t));

    std::lock_guard<std::mutex> lock(writeMutex_);
    ffipc::WriteFrame(pipe, static_cast<uint16_t>(UiMessageType::RequestDirectory), payload.data(),
                       static_cast<uint32_t>(payload.size()));
}

} // namespace ffui
