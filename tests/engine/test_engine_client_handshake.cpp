#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "EngineClient.h"
#include "UiServer.h"
#include "ffipc/PipeFraming.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffsetup/Identifiers.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

constexpr std::chrono::milliseconds kWaitDeadline{10000};

// fix-initial-snapshot-handshake (tasks.md 3.x): true EngineClient <-> UiServer
// handshake over the real control pipe and real shared-memory snapshots.
// Fresh server + fresh client, no directory requested or published first:
//
//     server.Start(session)
//         -> EngineClient::Start (spawns its management loop)
//         -> ConnectAndSubscribe (real Subscribe/SubscribeAck exchange)
//         -> Connected
//         -> ReadSnapshotWithGeneration() == empty map
//     RequestDirectory(fixture)
//         -> NewGeneration
//         -> ReadSnapshotWithGeneration() contains the fixture directory
//
// Requires no Administrator, no FastFilesIndexSvc, no signing, no persistent
// user index, and no interactive UI. The test server binds the real per-session
// control pipe name; this process must not be concurrently sharing that name
// with a running FastFilesEngine (the CTest default sequential run guarantees
// it).
void TestEngineClientUiServerHandshake() {
    wchar_t tempPath[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, tempPath) != 0, "handshake temporary path is available");
    const auto root = std::filesystem::path(tempPath) /
        (L"FastFiles-engine-client-handshake-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    Check(std::filesystem::create_directories(root, error), "handshake fixture directory is created");
    {
        std::ofstream output(root / L"handshake.txt", std::ios::binary | std::ios::trunc);
        output << "handshake-test";
    }
    Check(std::filesystem::exists(root / L"handshake.txt"), "handshake fixture file is created");

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);

    ffengine::UiServer server;
    const bool serverStarted = server.Start(sessionId);
    Check(serverStarted, "handshake UI server starts");
    if (!serverStarted) {
        std::filesystem::remove_all(root, error);
        return;
    }

    struct ClientState {
        std::mutex mutex;
        std::condition_variable condition;
        bool connected = false;
        uint64_t lastGeneration = 0;
        bool directoryError = false;
    } state;

    ffui::EngineClient client;
    client.Start(
        [&state](uint64_t generation) {
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                state.lastGeneration = generation;
            }
            state.condition.notify_all();
        },
        [&state](ffui::EngineConnectionState connectionState, bool privilegedPathActive) {
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (connectionState == ffui::EngineConnectionState::Connected) {
                    state.connected = true;
                }
            }
            state.condition.notify_all();
            (void)privilegedPathActive;
        },
        [&state](const std::wstring&, ffprotocol::DirectoryErrorReason) {
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                state.directoryError = true;
            }
            state.condition.notify_all();
        });

    {
        std::unique_lock<std::mutex> lock(state.mutex);
        const bool connected = state.condition.wait_for(lock, kWaitDeadline, [&state] {
            return state.connected;
        });
        Check(connected, "fresh EngineClient connects to a fresh UiServer");
        Check(!state.directoryError, "handshake reports no directory error");
        Check(state.lastGeneration == 0,
              "no NewGeneration is expected before the first directory request");
    }

    auto initial = client.ReadSnapshotWithGeneration();
    Check(initial.has_value(), "ReadSnapshotWithGeneration succeeds on the startup generation");
    Check(initial.has_value() && initial->generation != 0,
          "startup generation is nonzero");
    Check(initial.has_value() && initial->directories.empty(),
          "startup snapshot is an empty directory map");

    client.RequestDirectory(root.wstring());
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        const bool advanced = state.condition.wait_for(lock, kWaitDeadline, [&state] {
            return state.lastGeneration != 0;
        });
        Check(advanced, "the first directory request produces a NewGeneration");
        Check(!state.directoryError, "the requested directory publishes without error");
    }

    auto populated = client.ReadSnapshotWithGeneration();
    Check(populated.has_value(), "later generation is readable after the directory request");
    Check(populated.has_value() && populated->generation == state.lastGeneration,
          "read generation matches the announced generation");
    Check(populated.has_value() && populated->generation == initial->generation + 1,
          "the first directory request advances the generation by exactly one");
    Check(populated.has_value() && populated->directories.contains(root.wstring()),
          "the later generation contains the requested directory");
    if (populated.has_value() && populated->directories.contains(root.wstring())) {
        const auto& directory = populated->directories.at(root.wstring());
        Check(directory.status == ffprotocol::DirectoryEnumerationStatus::Success,
              "the requested directory is present with a successful status");
        bool foundFile = false;
        for (const auto& entry : directory.entries) {
            if (entry.name == L"handshake.txt") {
                foundFile = true;
                break;
            }
        }
        Check(foundFile, "the fixture file appears in the published directory");
    }

    client.Stop();
    server.Stop();

    std::filesystem::remove_all(root, error);
    Check(!std::filesystem::exists(root), "handshake fixture is removed");
}

// Audit regression (engine.md P2): a FolderAggregate request left in flight
// when the engine disconnects must fail its UI callback with NotFound instead
// of leaving the folder-size cell in perpetual "Calculating...". Uses a
// hand-rolled fake engine pipe that answers Subscribe and then drops the
// connection before replying to the aggregate request.
void TestFolderAggregateCallbacksFailOnDisconnect() {
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    wchar_t pipeNameBuffer[128]{};
    swprintf(pipeNameBuffer, std::size(pipeNameBuffer), ffsetup::kUiCtrlPipeNameFormat, sessionId);
    const std::wstring pipeName = pipeNameBuffer;

    // Unique per-run section name; the client maps "<prefix>.<generation>".
    const std::wstring sectionPrefix =
        L"FastFiles.Test.AggregateDisconnect." + std::to_wstring(GetCurrentProcessId());
    constexpr uint64_t kGeneration = 1;
    const std::wstring sectionName = sectionPrefix + L"." + std::to_wstring(kGeneration);
    const auto payload = ffprotocol::SerializeSnapshot(std::map<std::wstring, ffprotocol::SnapshotDirectory>{});
    const size_t sectionBytes = sizeof(ffprotocol::SnapshotSharedHeader) + payload.size();
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        static_cast<DWORD>(sectionBytes), sectionName.c_str());
    Check(mapping != nullptr, "fake-engine snapshot mapping is created");
    auto* view = static_cast<uint8_t*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0));
    Check(view != nullptr, "fake-engine snapshot view is mapped");
    if (view != nullptr) {
        ffprotocol::SnapshotSharedHeader header{};
        header.formatVersion = ffprotocol::kSnapshotFormatVersion;
        header.generation = kGeneration;
        header.payloadSizeBytes = payload.size();
        std::memcpy(view, &header, sizeof(header));
        if (!payload.empty()) {
            std::memcpy(view + sizeof(header), payload.data(), payload.size());
        }
    }

    HANDLE serverPipe = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                         1, 4096, 4096, 0, nullptr);
    Check(serverPipe != INVALID_HANDLE_VALUE, "fake-engine pipe instance is created");

    OVERLAPPED connectOverlapped{};
    connectOverlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(connectOverlapped.hEvent != nullptr, "fake-engine connect event is created");
    if (connectOverlapped.hEvent == nullptr) {
        // Check() already recorded the failure; /analyze cannot prove the
        // handle non-null below without this explicit guard.
        return;
    }
    const BOOL connected = ConnectNamedPipe(serverPipe, &connectOverlapped);
    const bool pending = !connected && GetLastError() == ERROR_IO_PENDING;

    struct ClientState {
        std::mutex mutex;
        std::condition_variable condition;
        bool connected = false;
        bool aggregateFailed = false;
        uint64_t failedRequestId = 0;
    } state;

    ffui::EngineClient client;
    client.Start(
        [&state](uint64_t) { state.condition.notify_all(); },
        [&state](ffui::EngineConnectionState connectionState, bool) {
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (connectionState == ffui::EngineConnectionState::Connected) {
                    state.connected = true;
                }
            }
            state.condition.notify_all();
        },
        [&state](const std::wstring&, ffprotocol::DirectoryErrorReason) {});

    // Wait for the client to reach the fake pipe, then complete the
    // Subscribe/SubscribeAck handshake by hand.
    const bool clientArrived = pending
        ? WaitForSingleObject(connectOverlapped.hEvent, 10000) == WAIT_OBJECT_0
        : connected == TRUE;
    Check(clientArrived, "EngineClient connects to the fake engine pipe");
    if (clientArrived) {
        const auto subscribe = ffipc::ReadFrameOverlapped(
            serverPipe, std::chrono::steady_clock::now() + kWaitDeadline);
        Check(subscribe.has_value()
                  && ffprotocol::ToUiMessageType(subscribe->header.messageType)
                      == ffprotocol::UiMessageType::Subscribe,
              "fake engine receives the Subscribe frame");
        std::vector<uint8_t> ackPayload(sizeof(ffprotocol::SubscribeAckPayload)
                                        + sectionPrefix.size() * sizeof(wchar_t));
        ffprotocol::SubscribeAckPayload ackHeader{kGeneration,
                                                  static_cast<uint16_t>(sectionPrefix.size())};
        std::memcpy(ackPayload.data(), &ackHeader, sizeof(ackHeader));
        std::memcpy(ackPayload.data() + sizeof(ackHeader), sectionPrefix.data(),
                    sectionPrefix.size() * sizeof(wchar_t));
        Check(ffipc::WriteFrameOverlapped(serverPipe,
                                          static_cast<uint16_t>(ffprotocol::UiMessageType::SubscribeAck),
                                          ackPayload.data(), static_cast<uint32_t>(ackPayload.size()),
                                          std::chrono::steady_clock::now() + kWaitDeadline),
              "fake engine writes the SubscribeAck");
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            const bool ok = state.condition.wait_for(lock, kWaitDeadline, [&state] { return state.connected; });
            Check(ok, "EngineClient reports Connected against the fake engine");
        }

        // Register an aggregate request that the fake engine never answers.
        const uint64_t requestId = client.RequestFolderAggregate(
            /*volumeRowId=*/1, /*parentFrnLow=*/2, /*parentFrnHigh=*/0,
            [&state](uint64_t id, ffprotocol::FolderAggregateStatus status, uint64_t, uint64_t, uint64_t,
                     uint64_t, uint64_t) {
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    if (status == ffprotocol::FolderAggregateStatus::NotFound) {
                        state.aggregateFailed = true;
                        state.failedRequestId = id;
                    }
                }
                state.condition.notify_all();
            });
        Check(requestId != 0, "aggregate request is registered");

        // Drop the connection server-side without ever answering: the client's
        // reader loop must unwind and fail every pending aggregate callback.
        CancelIoEx(serverPipe, nullptr);
        DisconnectNamedPipe(serverPipe);

        {
            std::unique_lock<std::mutex> lock(state.mutex);
            const bool failed = state.condition.wait_for(lock, kWaitDeadline,
                                                         [&state] { return state.aggregateFailed; });
            Check(failed, "the in-flight aggregate callback fires on engine disconnect");
            Check(state.failedRequestId == requestId,
                  "the disconnect failure carries the original request id");
        }
        // Stop the client immediately so its management loop cannot wander
        // into engine-launch territory while the fake pipe is broken.
        client.Stop();
    }

    client.Stop();
    if (view != nullptr) UnmapViewOfFile(view);
    if (mapping != nullptr) CloseHandle(mapping);
    if (serverPipe != INVALID_HANDLE_VALUE) CloseHandle(serverPipe);
    if (connectOverlapped.hEvent != nullptr) CloseHandle(connectOverlapped.hEvent);
}

} // namespace

int main() {
    TestEngineClientUiServerHandshake();
    TestFolderAggregateCallbacksFailOnDisconnect();
    return FailureCount() == 0 ? 0 : 1;
}
