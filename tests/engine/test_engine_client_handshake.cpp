#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "EngineClient.h"
#include "UiServer.h"
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

} // namespace

int main() {
    TestEngineClientUiServerHandshake();
    return FailureCount() == 0 ? 0 : 1;
}
