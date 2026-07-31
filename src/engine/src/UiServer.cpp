#include "UiServer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/UiProtocol.h"
#include "ffsetup/Identifiers.h"

#include "DegradedModeEnumerator.h"

namespace ffengine {

namespace {

using ffprotocol::UiMessageType;

ffprotocol::SnapshotDirectory ToSnapshotDirectory(const EnumerationResult& result) {
    ffprotocol::SnapshotDirectory directory;
    directory.status = static_cast<ffprotocol::DirectoryEnumerationStatus>(result.status);
    directory.entries.reserve(result.entries.size());
    for (const auto& entry : result.entries) {
        ffprotocol::SnapshotDirectoryEntry snapshotEntry;
        snapshotEntry.name = entry.name;
        snapshotEntry.isDirectory = entry.isDirectory;
        directory.entries.push_back(std::move(snapshotEntry));
    }
    return directory;
}

bool SendDirectoryError(HANDLE pipe, const std::wstring& path, ffprotocol::DirectoryErrorReason reason) {
    std::vector<uint8_t> payload(sizeof(ffprotocol::DirectoryErrorPayload) + path.size() * sizeof(wchar_t));
    ffprotocol::DirectoryErrorPayload header{reason, static_cast<uint16_t>(path.size())};
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!path.empty()) {
        std::memcpy(payload.data() + sizeof(header), path.data(), path.size() * sizeof(wchar_t));
    }
    return ffipc::WriteFrame(pipe, static_cast<uint16_t>(UiMessageType::DirectoryError), payload.data(),
                              static_cast<uint32_t>(payload.size()));
}

} // namespace

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

    return listener_.Start(pipeName, &securityDescriptor_.attributes,
                            [this](HANDLE pipe) { HandleConnection(pipe); });
}

void UiServer::Stop() {
    listener_.Stop();
    watcher_.UnwatchAll();
    snapshot_.Stop();

    // Cancel each connection's pending ReadFrame rather than closing the
    // handle here directly: each connection's own (detached) thread in
    // HandleConnection owns the single CloseHandle for its pipe, in its
    // `disconnected:` path. Closing it from both places would race a
    // double-close / a handle-value reused by an unrelated CreateFile.
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (HANDLE pipe : subscribedPipes_) {
        CancelIoEx(pipe, nullptr);
    }
    subscribedPipes_.clear();
}

void UiServer::OnDirectoryChanged(const std::wstring& path) {
    auto result = EnumerateDirectoryDegraded(path);
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        directories_[path] = ToSnapshotDirectory(result);
    }
    RepublishAndBroadcastGeneration();
}

void UiServer::RepublishAndBroadcastGeneration() {
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        generation = snapshot_.Publish(directories_);
    }
    if (generation == 0) {
        return; // publish failed (e.g. exceeded slot capacity); nothing to announce
    }

    ffprotocol::NewGenerationPayload payload{generation};
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto it = subscribedPipes_.begin(); it != subscribedPipes_.end();) {
        if (!ffipc::WriteFrame(*it, static_cast<uint16_t>(UiMessageType::NewGeneration), &payload, sizeof(payload))) {
            it = subscribedPipes_.erase(it); // that connection's own reader thread will observe the disconnect and clean up
        } else {
            ++it;
        }
    }
}

void UiServer::BroadcastEngineStatus(HANDLE toSinglePipe) {
    ffprotocol::EngineStatusPayload payload{
        engineActive_ ? ffprotocol::PrivilegedPathStatus::Active : ffprotocol::PrivilegedPathStatus::Unavailable};

    if (toSinglePipe != nullptr) {
        ffipc::WriteFrame(toSinglePipe, static_cast<uint16_t>(UiMessageType::EngineStatus), &payload, sizeof(payload));
        return;
    }

    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (HANDLE pipe : subscribedPipes_) {
        ffipc::WriteFrame(pipe, static_cast<uint16_t>(UiMessageType::EngineStatus), &payload, sizeof(payload));
    }
}

void UiServer::MergeIndexDirectories(std::map<std::wstring, ffprotocol::SnapshotDirectory> indexDirectories) {
    {
        std::lock_guard<std::mutex> lock(directoriesMutex_);
        for (auto& [path, directory] : indexDirectories) {
            directories_[path] = std::move(directory);
        }
    }
    RepublishAndBroadcastGeneration();
}

void UiServer::SetEngineStatus(bool privilegedPathActive) {
    engineActive_ = privilegedPathActive;
    BroadcastEngineStatus();
}

void UiServer::HandleRequestDirectory(HANDLE pipe, const std::wstring& path) {
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
        SendDirectoryError(pipe, path, ffprotocol::DirectoryErrorReason::AccessDenied);
        return;
    }
    if (result.status == EnumerationStatus::NotFound) {
        SendDirectoryError(pipe, path, ffprotocol::DirectoryErrorReason::NoLongerAvailable);
        return;
    }

    // Success: publish + broadcast so this (and every other subscribed)
    // client can read the freshly enumerated directory straight out of
    // the shared-memory snapshot, zero further IPC round trip.
    RepublishAndBroadcastGeneration();
}

void UiServer::HandleConnection(HANDLE pipe) {
    bool subscribed = false;

    for (;;) {
        auto frame = ffipc::ReadFrame(pipe);
        if (!frame) {
            break;
        }
        auto messageType = ffprotocol::ToUiMessageType(frame->header.messageType);
        if (!messageType) {
            break; // unrecognized message type: protocol violation
        }

        switch (*messageType) {
            case UiMessageType::Subscribe: {
                {
                    std::lock_guard<std::mutex> lock(clientsMutex_);
                    subscribedPipes_.push_back(pipe);
                }
                subscribed = true;

                const std::wstring& sectionName = snapshot_.SectionName();
                std::vector<uint8_t> payload(sizeof(ffprotocol::SubscribeAckPayload) + sectionName.size() * sizeof(wchar_t));
                ffprotocol::SubscribeAckPayload header{snapshot_.CurrentGeneration(), static_cast<uint16_t>(sectionName.size())};
                std::memcpy(payload.data(), &header, sizeof(header));
                std::memcpy(payload.data() + sizeof(header), sectionName.data(), sectionName.size() * sizeof(wchar_t));

                if (!ffipc::WriteFrame(pipe, static_cast<uint16_t>(UiMessageType::SubscribeAck), payload.data(),
                                        static_cast<uint32_t>(payload.size()))) {
                    goto disconnected;
                }
                BroadcastEngineStatus(pipe);
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
                HandleRequestDirectory(pipe, path);
                break;
            }

            default:
                goto disconnected; // reply-only types are illegal from a client
        }
    }

disconnected:
    if (subscribed) {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        subscribedPipes_.erase(std::remove(subscribedPipes_.begin(), subscribedPipes_.end(), pipe), subscribedPipes_.end());
    }
    CloseHandle(pipe);
}

} // namespace ffengine
