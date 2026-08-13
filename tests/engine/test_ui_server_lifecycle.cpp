#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "UiServer.h"
#include "ffipc/PipeFraming.h"
#include "ffprotocol/UiProtocol.h"
#include "ffsetup/Identifiers.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

constexpr std::chrono::milliseconds kWaitDeadline{5000};
constexpr std::chrono::milliseconds kShutdownDeadline{500};

struct BlockingSink {
    std::mutex mutex;
    std::condition_variable condition;
    int generationWrites = 0;
    bool blockSecondGeneration = false;
    bool publicationEntered = false;
    bool publicationExited = false;
    bool cancellationRequested = false;
    bool disconnectRequested = false;

    bool Write(HANDLE, uint16_t messageType, const void*, uint32_t) {
        if (messageType != static_cast<uint16_t>(ffprotocol::UiMessageType::NewGeneration)) {
            return true;
        }

        std::unique_lock<std::mutex> lock(mutex);
        ++generationWrites;
        if (!blockSecondGeneration || generationWrites != 2) {
            condition.notify_all();
            return true;
        }

        publicationEntered = true;
        condition.notify_all();
        condition.wait(lock, [this] { return cancellationRequested || disconnectRequested; });
        publicationExited = true;
        condition.notify_all();
        return !disconnectRequested;
    }

    void Cancel(HANDLE) {
        std::lock_guard<std::mutex> lock(mutex);
        cancellationRequested = true;
        condition.notify_all();
    }

    void Release() {
        std::lock_guard<std::mutex> lock(mutex);
        cancellationRequested = true;
        condition.notify_all();
    }

    void Disconnect() {
        std::lock_guard<std::mutex> lock(mutex);
        disconnectRequested = true;
        condition.notify_all();
    }

    bool WaitForGeneration(int expected, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [this, expected] {
            return generationWrites >= expected;
        });
    }

    bool WaitForPublication(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [this] {
            return publicationEntered;
        });
    }

    bool WaitForPublicationExit(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [this] {
            return publicationExited;
        });
    }
};

std::filesystem::path MakeFixtureRoot(const wchar_t* suffix) {
    wchar_t tempPath[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, tempPath) != 0, "lifecycle temporary path is available");
    const auto root = std::filesystem::path(tempPath) /
        (L"FastFiles-ui-server-lifecycle-" + std::to_wstring(GetCurrentProcessId()) + L"-" + suffix);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    Check(std::filesystem::create_directories(root, error), "lifecycle fixture directory is created");
    return root;
}

void CreateFileIn(const std::filesystem::path& root, const wchar_t* name) {
    std::ofstream output(root / name, std::ios::binary | std::ios::trunc);
    output << "lifecycle-test";
    output.close();
    Check(!output.fail(), "lifecycle fixture file is created");
}

std::wstring PipeName(DWORD sessionId) {
    wchar_t buffer[128]{};
    swprintf(buffer, std::size(buffer), ffsetup::kUiCtrlPipeNameFormat, sessionId);
    return buffer;
}

std::wstring SnapshotSectionName(DWORD sessionId) {
    wchar_t buffer[128]{};
    swprintf(buffer, std::size(buffer), ffsetup::kSnapshotSectionNameFormat, sessionId);
    return buffer;
}

HANDLE ConnectToServer(const std::wstring& pipeName) {
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(kWaitDeadline.count());
    while (GetTickCount64() < deadline) {
        if (WaitNamedPipeW(pipeName.c_str(), 100)) {
            HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) {
                DWORD mode = PIPE_READMODE_BYTE;
                if (SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
                    return pipe;
                }
                CloseHandle(pipe);
            }
        }
    }
    return INVALID_HANDLE_VALUE;
}

bool SendSubscribe(HANDLE pipe) {
    return ffipc::WriteFrame(pipe, static_cast<uint16_t>(ffprotocol::UiMessageType::Subscribe));
}

bool SendRequestDirectory(HANDLE pipe, const std::wstring& path) {
    ffprotocol::RequestDirectoryHeader header{static_cast<uint16_t>(path.size())};
    std::vector<uint8_t> payload(sizeof(header) + path.size() * sizeof(wchar_t));
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), path.data(), path.size() * sizeof(wchar_t));
    return ffipc::WriteFrame(pipe, static_cast<uint16_t>(ffprotocol::UiMessageType::RequestDirectory),
                             payload.data(), static_cast<uint32_t>(payload.size()));
}

bool SendIndexedSearch(HANDLE pipe, uint64_t requestId) {
    ffprotocol::IndexedSearchRequest request;
    request.requestId = requestId;
    request.scope = 3;
    request.sortField = 0;
    request.maxResults = 100;
    request.query = L"entry";
    const auto payload = ffprotocol::SerializeIndexedSearchRequest(request);
    return !payload.empty()
        && ffipc::WriteFrame(pipe, static_cast<uint16_t>(ffprotocol::UiMessageType::RequestIndexedSearch),
                              payload.data(), static_cast<uint32_t>(payload.size()));
}

bool SendCancelIndexedSearch(HANDLE pipe, uint64_t requestId) {
    const ffprotocol::CancelIndexedSearchPayload payload{requestId};
    return ffipc::WriteFrame(pipe, static_cast<uint16_t>(ffprotocol::UiMessageType::CancelIndexedSearch),
                             &payload, sizeof(payload));
}

struct IndexedSearchCancellationState {
    std::mutex mutex;
    std::condition_variable condition;
    std::set<uint64_t> started;
    std::set<uint64_t> cancelled;
    int resultWrites = 0;

    bool Write(uint16_t messageType) {
        std::lock_guard<std::mutex> lock(mutex);
        if (messageType == static_cast<uint16_t>(ffprotocol::UiMessageType::IndexedSearchResult)) {
            ++resultWrites;
        }
        return true;
    }

    ffprotocol::IndexedSearchResponse Handle(
        const ffprotocol::IndexedSearchRequest& request,
        const ffengine::UiServer::SearchCancellation& cancellation) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            started.insert(request.requestId);
            condition.notify_all();
        }
        while (!cancellation()) {
            Sleep(1);
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            cancelled.insert(request.requestId);
            condition.notify_all();
        }
        ffprotocol::IndexedSearchResponse response;
        response.requestId = request.requestId;
        response.status = ffprotocol::IndexedSearchStatus::Cancelled;
        return response;
    }

    bool WaitForStarted(uint64_t requestId, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [&] { return started.contains(requestId); });
    }

    bool WaitForCancelled(uint64_t requestId, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [&] { return cancelled.contains(requestId); });
    }

    bool IsCancelled(uint64_t requestId) {
        std::lock_guard<std::mutex> lock(mutex);
        return cancelled.contains(requestId);
    }
};

template <typename ConfigureSink, typename CheckResult>
void RunServerScenario(const wchar_t* suffix, ConfigureSink configureSink, CheckResult checkResult) {
    const auto root = MakeFixtureRoot(suffix);
    BlockingSink sink;
    configureSink(sink);
    ffengine::UiServer server(
        [&sink](HANDLE pipe, uint16_t messageType, const void* payload, uint32_t payloadSize) {
            return sink.Write(pipe, messageType, payload, payloadSize);
        },
        [&sink](HANDLE pipe) { sink.Cancel(pipe); });

    const DWORD sessionId = GetCurrentProcessId() + (suffix[0] == L'b' ? 1u : 2u);
    const bool started = server.Start(sessionId);
    Check(started, "UI server starts for lifecycle scenario");
    if (!started) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }
    HANDLE client = ConnectToServer(PipeName(sessionId));
    Check(client != INVALID_HANDLE_VALUE, "lifecycle client connects");
    if (client != INVALID_HANDLE_VALUE) {
        Check(SendSubscribe(client), "lifecycle client subscribes");
        Check(SendRequestDirectory(client, root.wstring()), "lifecycle client requests directory");
        Check(sink.WaitForGeneration(1, kWaitDeadline), "initial directory publication completes");
        checkResult(server, sink, root, client);
        if (client != INVALID_HANDLE_VALUE) {
            CloseHandle(client);
        }
    }
    server.Stop();

    std::error_code error;
    std::filesystem::remove_all(root, error);
    Check(!std::filesystem::exists(root), "lifecycle fixture is removed");
}

void TestNormalWatcherUpdate() {
    RunServerScenario(
        L"normal",
        [](BlockingSink& sink) { sink.blockSecondGeneration = false; },
        [](ffengine::UiServer&, BlockingSink& sink, const std::filesystem::path& root, HANDLE&) {
            CreateFileIn(root, L"normal.txt");
            Check(sink.WaitForGeneration(2, kWaitDeadline),
                  "watcher change reaches the UI publication path");
        });
}

void TestBlockedPublicationShutdown() {
    RunServerScenario(
        L"blocked",
        [](BlockingSink& sink) { sink.blockSecondGeneration = true; },
        [](ffengine::UiServer& server, BlockingSink& sink, const std::filesystem::path& root, HANDLE&) {
            CreateFileIn(root, L"blocked.txt");
            Check(sink.WaitForPublication(kWaitDeadline), "watcher enters the blocked UI publication");

            std::atomic<bool> stopped{false};
            const auto start = std::chrono::steady_clock::now();
            std::thread stopper([&] {
                server.Stop();
                stopped = true;
            });
            while (!stopped.load() && std::chrono::steady_clock::now() - start < kShutdownDeadline) {
                Sleep(1);
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            Check(stopped.load(), "shutdown completes while downstream publication is blocked");
            std::printf("blocked publication shutdown: %lld ms (deadline %lld ms)\n",
                        static_cast<long long>(elapsed.count()),
                        static_cast<long long>(kShutdownDeadline.count()));
            if (!stopped.load()) {
                // Keep a pre-fix test run bounded so the assertion reports the
                // lifecycle failure instead of turning into a CTest timeout.
                sink.Release();
            }
            stopper.join();
            Check(sink.cancellationRequested, "shutdown requests publication cancellation");
        });
}

void TestDisconnectDuringPublication() {
    RunServerScenario(
        L"disconnect",
        [](BlockingSink& sink) { sink.blockSecondGeneration = true; },
        [](ffengine::UiServer& server, BlockingSink& sink, const std::filesystem::path& root, HANDLE& client) {
            CreateFileIn(root, L"disconnect.txt");
            Check(sink.WaitForPublication(kWaitDeadline), "disconnect scenario enters publication");
            CloseHandle(client);
            client = INVALID_HANDLE_VALUE;
            sink.Disconnect();
            Check(sink.WaitForPublicationExit(kShutdownDeadline),
                  "disconnected downstream lets the publication unwind");
            server.Stop();
        });
}

void TestIdleShutdown() {
    const auto root = MakeFixtureRoot(L"idle");
    BlockingSink sink;
    ffengine::UiServer server(
        [&sink](HANDLE pipe, uint16_t messageType, const void* payload, uint32_t payloadSize) {
            return sink.Write(pipe, messageType, payload, payloadSize);
        },
        [&sink](HANDLE pipe) { sink.Cancel(pipe); });
    const DWORD sessionId = GetCurrentProcessId() + 3;
    Check(server.Start(sessionId), "idle UI server starts");
    const auto start = std::chrono::steady_clock::now();
    server.Stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    Check(elapsed < kShutdownDeadline, "idle UI server shutdown is fast");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void TestIndexedSearchCancellationIsConnectionScoped() {
    IndexedSearchCancellationState state;
    ffengine::UiServer server(
        [&state](HANDLE, uint16_t messageType, const void*, uint32_t) {
            return state.Write(messageType);
        });
    server.onRequestIndexedSearch = [&state](
        const ffprotocol::IndexedSearchRequest& request,
        const ffengine::UiServer::SearchCancellation& cancellation) {
        return state.Handle(request, cancellation);
    };

    const DWORD sessionId = GetCurrentProcessId() + 5;
    const bool started = server.Start(sessionId);
    Check(started, "UI server starts for indexed-search cancellation");
    if (!started) return;
    const std::wstring pipeName = PipeName(sessionId);
    HANDLE first = ConnectToServer(pipeName);
    HANDLE second = ConnectToServer(pipeName);
    Check(first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE,
          "two UI clients connect for scoped indexed-search cancellation");
    if (first == INVALID_HANDLE_VALUE || second == INVALID_HANDLE_VALUE) {
        if (first != INVALID_HANDLE_VALUE) CloseHandle(first);
        if (second != INVALID_HANDLE_VALUE) CloseHandle(second);
        server.Stop();
        return;
    }
    Check(SendSubscribe(first) && SendSubscribe(second),
          "both UI clients subscribe before indexed searches");
    Check(SendIndexedSearch(first, 101) && SendIndexedSearch(second, 202),
          "both UI clients issue indexed searches");
    Check(state.WaitForStarted(101, kWaitDeadline) && state.WaitForStarted(202, kWaitDeadline),
          "both indexed searches start independently");
    Check(SendIndexedSearch(first, 102), "first UI client supersedes its indexed search");
    Check(state.WaitForCancelled(101, kWaitDeadline) && state.WaitForStarted(102, kWaitDeadline),
          "a newer indexed search cancels the older request and starts promptly");
    Check(!state.IsCancelled(202), "one UI client cannot cancel another client's search");
    Check(SendCancelIndexedSearch(first, 102) && SendCancelIndexedSearch(second, 202),
          "both UI clients send their explicit cancellations");
    Check(state.WaitForCancelled(102, kWaitDeadline) && state.WaitForCancelled(202, kWaitDeadline),
          "explicit cancellation stops both remaining indexed searches");
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        Check(state.resultWrites == 0, "cancelled indexed searches do not publish stale results");
    }
    CloseHandle(first);
    CloseHandle(second);
    server.Stop();
}

void TestFailedStartRollsBackSnapshot() {
    const DWORD sessionId = GetCurrentProcessId() + 4;
    const std::wstring pipeName = PipeName(sessionId);
    HANDLE squatter = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                       PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr);
    Check(squatter != INVALID_HANDLE_VALUE, "UI pipe collision fixture is created");
    if (squatter == INVALID_HANDLE_VALUE) return;

    ffengine::UiServer server;
    Check(!server.Start(sessionId), "UI server rejects a pre-existing pipe name");
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, SnapshotSectionName(sessionId).c_str());
    Check(mapping == nullptr, "failed UI server start rolls back its snapshot mapping");
    if (mapping != nullptr) CloseHandle(mapping);

    CloseHandle(squatter);
    Check(server.Start(sessionId), "UI server can restart after a failed start is rolled back");
    server.Stop();
}

} // namespace

int main() {
    TestNormalWatcherUpdate();
    TestBlockedPublicationShutdown();
    TestDisconnectDuringPublication();
    TestIdleShutdown();
    TestIndexedSearchCancellationIsConnectionScoped();
    TestFailedStartRollsBackSnapshot();
    return FailureCount() == 0 ? 0 : 1;
}
