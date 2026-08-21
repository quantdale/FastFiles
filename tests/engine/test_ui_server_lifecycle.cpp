#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
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

// fix-initial-snapshot-handshake (tasks.md 8.x): if the initial snapshot
// publication fails, UiServer::Start must fail closed -- no control pipe may
// be exposed and no snapshot state may survive.
void TestUiServerStartFailsOnInitialPublicationFailure() {
#ifdef FF_SNAPSHOT_PUBLISHER_TESTING
    const DWORD sessionId = GetCurrentProcessId() + 9;
    const std::wstring pipeName = PipeName(sessionId);

    ffengine::SnapshotPublisher::SetCreateFileMappingHook(
        [](const std::wstring&) -> HANDLE { return nullptr; });
    ffengine::UiServer server;
    Check(!server.Start(sessionId),
          "initial snapshot publication failure makes UiServer::Start fail");
    HANDLE lateClient = ConnectToServer(pipeName);
    Check(lateClient == INVALID_HANDLE_VALUE,
          "failed UiServer start exposes no usable control pipe");
    if (lateClient != nullptr && lateClient != INVALID_HANDLE_VALUE) CloseHandle(lateClient);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, SnapshotSectionName(sessionId).c_str());
    Check(mapping == nullptr, "failed UiServer start rolls back snapshot mapping state");
    if (mapping != nullptr) CloseHandle(mapping);
    ffengine::SnapshotPublisher::SetCreateFileMappingHook({});

    Check(server.Start(sessionId),
          "UiServer starts normally once the injection is cleared");
    server.Stop();
#endif
}

// fix-initial-snapshot-handshake: reads the next frame from `pipe`, skipping
// frames of other types, until a frame of `messageType` arrives or the
// deadline passes. The server writes SubscribeAck and EngineStatus back to
// back, so a strict single-read sequence is not portable to every scenario.
std::optional<ffipc::ReceivedFrame> ReadFrameUntilType(HANDLE pipe, uint16_t messageType,
                                                       std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        // A non-overlapped named pipe handle is waitable: it signals when a
        // blocking read can make progress. Poll so a hung server cannot turn
        // this regression into a CTest timeout.
        if (WaitForSingleObject(pipe, 200) == WAIT_OBJECT_0) {
            auto frame = ffipc::ReadFrame(pipe);
            if (frame && frame->header.messageType == messageType) {
                return frame;
            }
        }
    }
    return std::nullopt;
}

// fix-initial-snapshot-handshake: validates a published generation the same
// way EngineClient::IsSnapshotGenerationUsable does (format compatible,
// nonzero committed generation matching the expectation, bounded payload,
// region large enough), then parses the payload. Returns nullopt on any
// validation failure and sets ERROR_INVALID_DATA exactly like the client.
std::optional<std::map<std::wstring, ffprotocol::SnapshotDirectory>> ValidateAndParseSnapshotSection(
    const std::wstring& sectionName, uint64_t expectedGeneration) {
    if (sectionName.empty()) return std::nullopt;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sectionName.c_str());
    if (mapping == nullptr) return std::nullopt;
    const auto* view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (view == nullptr) {
        CloseHandle(mapping);
        return std::nullopt;
    }
    const auto* header = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(view);
    bool valid = ffprotocol::IsSnapshotFormatCompatible(header->formatVersion)
        && expectedGeneration != 0
        && header->generation == expectedGeneration
        && header->payloadSizeBytes <= ffprotocol::kSnapshotMaximumPayloadBytes
        && header->payloadSizeBytes <= static_cast<uint64_t>(std::numeric_limits<size_t>::max()
                                                             - sizeof(*header));
    std::optional<std::map<std::wstring, ffprotocol::SnapshotDirectory>> parsed;
    if (valid) {
        parsed = ffprotocol::ParseSnapshot(view + sizeof(*header),
                                           static_cast<size_t>(header->payloadSizeBytes));
        if (!parsed.has_value()) valid = false;
    }
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    if (!valid) {
        SetLastError(ERROR_INVALID_DATA);
        return std::nullopt;
    }
    return parsed;
}

// fix-initial-snapshot-handshake (tasks.md 4.x): the regression that the
// engine-connect-snapshot-map-failed / 0xd startup failure was born from.
// A fresh UI subscribes to a fresh engine BEFORE any directory has been
// requested or published. The SubscribeAck must carry a usable nonzero
// generation containing a canonical parseable empty snapshot, and the first
// RequestDirectory must advance to the next generation with the requested
// directory present.
void TestSubscribeBeforeAnyDirectory() {
    const auto root = MakeFixtureRoot(L"subscribe-first");
    CreateFileIn(root, L"subscribe-first.txt");

    // Real frame writer/pipe canceller: this client reads actual frames off
    // the control pipe, exactly like EngineClient does.
    ffengine::UiServer server;
    const DWORD sessionId = GetCurrentProcessId() + 6;
    const bool started = server.Start(sessionId);
    Check(started, "subscribe-first UI server starts");
    if (!started) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }
    HANDLE client = ConnectToServer(PipeName(sessionId));
    Check(client != INVALID_HANDLE_VALUE, "subscribe-first client connects before any directory request");
    if (client == INVALID_HANDLE_VALUE) {
        server.Stop();
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }

    Check(SendSubscribe(client), "subscribe-first client subscribes before any directory request");
    auto ack = ReadFrameUntilType(client, static_cast<uint16_t>(ffprotocol::UiMessageType::SubscribeAck),
                                  kWaitDeadline);
    Check(ack.has_value(), "subscribe-first client receives a SubscribeAck");
    if (!ack.has_value()) {
        CloseHandle(client);
        server.Stop();
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }
    Check(ack->payload.size() >= sizeof(ffprotocol::SubscribeAckPayload),
          "subscribe-first SubscribeAck carries its fixed header");
    ffprotocol::SubscribeAckPayload ackHeader{};
    std::memcpy(&ackHeader, ack->payload.data(), sizeof(ackHeader));
    std::wstring sectionPrefix(
        reinterpret_cast<const wchar_t*>(ack->payload.data() + sizeof(ackHeader)),
        ackHeader.sectionNameLengthChars);
    std::printf("subscribe-first: acked generation %llu, prefix length %u\n",
                static_cast<unsigned long long>(ackHeader.generationId),
                static_cast<unsigned>(ackHeader.sectionNameLengthChars));
    Check(ackHeader.generationId != 0, "SubscribeAck never advertises generation zero");
    Check(!sectionPrefix.empty(), "SubscribeAck carries a section prefix");

    const std::wstring sectionName = sectionPrefix + L"." + std::to_wstring(ackHeader.generationId);
    auto initialSnapshot = ValidateAndParseSnapshotSection(sectionName, ackHeader.generationId);
    Check(initialSnapshot.has_value(),
          "the startup generation maps, validates, and parses with EngineClient semantics");
    if (initialSnapshot.has_value()) {
        Check(initialSnapshot->empty(),
              "the startup generation contains a canonical empty snapshot (zero directories)");
    }

    // Only now is a directory requested; the next generation must advance by
    // exactly one and contain the requested directory.
    Check(SendRequestDirectory(client, root.wstring()), "subscribe-first client requests a directory");
    auto next = ReadFrameUntilType(client, static_cast<uint16_t>(ffprotocol::UiMessageType::NewGeneration),
                                   kWaitDeadline);
    Check(next.has_value(), "subscribe-first client receives the first NewGeneration");
    if (next.has_value()) {
        Check(next->payload.size() == sizeof(ffprotocol::NewGenerationPayload),
              "NewGeneration carries its fixed payload");
        ffprotocol::NewGenerationPayload nextPayload{};
        std::memcpy(&nextPayload, next->payload.data(), sizeof(nextPayload));
        Check(nextPayload.generationId == ackHeader.generationId + 1,
              "first directory request advances the generation by exactly one");
        auto populated = ValidateAndParseSnapshotSection(sectionPrefix + L"." +
                                                             std::to_wstring(nextPayload.generationId),
                                                         nextPayload.generationId);
        Check(populated.has_value() && populated->contains(root.wstring()),
              "the advanced generation contains the requested directory");
    }

    CloseHandle(client);
    server.Stop();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    Check(!std::filesystem::exists(root), "subscribe-first fixture is removed");
}

// fix-initial-snapshot-handshake (tasks.md 5.x): two UI clients subscribe
// before any directory publication. Both must be acked with the same current
// generation, both must map and parse the same empty snapshot, and both must
// receive the next NewGeneration when a directory is published. No
// client-specific mutation of the shared generation.
void TestMultiClientSubscribeBeforePublication() {
    const auto root = MakeFixtureRoot(L"multi-subscribe");
    CreateFileIn(root, L"multi-subscribe.txt");

    ffengine::UiServer server;
    const DWORD sessionId = GetCurrentProcessId() + 7;
    const bool started = server.Start(sessionId);
    Check(started, "multi-subscribe UI server starts");
    if (!started) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }
    HANDLE first = ConnectToServer(PipeName(sessionId));
    HANDLE second = ConnectToServer(PipeName(sessionId));
    Check(first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE,
          "two clients connect before any directory publication");
    if (first == INVALID_HANDLE_VALUE || second == INVALID_HANDLE_VALUE) {
        if (first != INVALID_HANDLE_VALUE) CloseHandle(first);
        if (second != INVALID_HANDLE_VALUE) CloseHandle(second);
        server.Stop();
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }

    Check(SendSubscribe(first) && SendSubscribe(second), "both clients subscribe before publication");
    auto firstAck = ReadFrameUntilType(first, static_cast<uint16_t>(ffprotocol::UiMessageType::SubscribeAck),
                                       kWaitDeadline);
    auto secondAck = ReadFrameUntilType(second, static_cast<uint16_t>(ffprotocol::UiMessageType::SubscribeAck),
                                        kWaitDeadline);
    Check(firstAck.has_value() && secondAck.has_value(), "both clients receive SubscribeAck");
    if (!firstAck.has_value() || !secondAck.has_value()) {
        CloseHandle(first);
        CloseHandle(second);
        server.Stop();
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }
    ffprotocol::SubscribeAckPayload firstHeader{};
    ffprotocol::SubscribeAckPayload secondHeader{};
    std::memcpy(&firstHeader, firstAck->payload.data(), sizeof(firstHeader));
    std::memcpy(&secondHeader, secondAck->payload.data(), sizeof(secondHeader));
    Check(firstHeader.generationId != 0 && secondHeader.generationId != 0,
          "both clients are acked with a nonzero generation");
    Check(firstHeader.generationId == secondHeader.generationId,
          "both clients receive the same current generation");

    std::wstring firstPrefix(
        reinterpret_cast<const wchar_t*>(firstAck->payload.data() + sizeof(firstHeader)),
        firstHeader.sectionNameLengthChars);
    auto firstSnapshot = ValidateAndParseSnapshotSection(
        firstPrefix + L"." + std::to_wstring(firstHeader.generationId), firstHeader.generationId);
    auto secondSnapshot = ValidateAndParseSnapshotSection(
        firstPrefix + L"." + std::to_wstring(secondHeader.generationId), secondHeader.generationId);
    Check(firstSnapshot.has_value() && secondSnapshot.has_value(),
          "both clients map and parse the startup generation simultaneously");
    Check(firstSnapshot.has_value() && firstSnapshot->empty()
              && secondSnapshot.has_value() && secondSnapshot->empty(),
          "both clients parse the same empty startup snapshot");

    // One client requests a directory; BOTH clients must be notified.
    Check(SendRequestDirectory(first, root.wstring()), "first client requests a directory");
    auto firstNext = ReadFrameUntilType(first, static_cast<uint16_t>(ffprotocol::UiMessageType::NewGeneration),
                                        kWaitDeadline);
    auto secondNext = ReadFrameUntilType(second, static_cast<uint16_t>(ffprotocol::UiMessageType::NewGeneration),
                                         kWaitDeadline);
    Check(firstNext.has_value() && secondNext.has_value(),
          "both clients receive the next NewGeneration after one publication");
    if (firstNext.has_value() && secondNext.has_value()) {
        ffprotocol::NewGenerationPayload firstPayload{};
        ffprotocol::NewGenerationPayload secondPayload{};
        std::memcpy(&firstPayload, firstNext->payload.data(), sizeof(firstPayload));
        std::memcpy(&secondPayload, secondNext->payload.data(), sizeof(secondPayload));
        Check(firstPayload.generationId == firstHeader.generationId + 1
                  && secondPayload.generationId == secondHeader.generationId + 1,
              "both clients observe the same next generation");
        auto populated = ValidateAndParseSnapshotSection(
            firstPrefix + L"." + std::to_wstring(firstPayload.generationId), firstPayload.generationId);
        Check(populated.has_value() && populated->contains(root.wstring()),
              "the shared next generation contains the requested directory for both clients");
    }

    CloseHandle(first);
    CloseHandle(second);
    server.Stop();
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

// fix-initial-snapshot-handshake (tasks.md 6.x): rapid server start ->
// connect -> subscribe -> map -> parse -> disconnect -> stop cycles with
// unique session epochs. No sleeps are used to make it pass; the loop simply
// must not observe a generation-0 ack, ERROR_INVALID_DATA, a missing-mapping
// race, a leaked named-mapping collision, or a hang.
void TestStartupSubscribeStress() {
    constexpr int kIterations = 100;
    int failedIterations = 0;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        ffengine::UiServer server;
        // Offset far beyond the other tests' fixed session ids and unique per
        // iteration: a leaked mapping or pipe from a previous iteration must
        // never be silently reused.
        const DWORD sessionId = static_cast<DWORD>(GetCurrentProcessId() + 100 + iteration);
        if (!server.Start(sessionId)) {
            ++failedIterations;
            continue;
        }
        HANDLE client = ConnectToServer(PipeName(sessionId));
        if (client == INVALID_HANDLE_VALUE) {
            ++failedIterations;
            server.Stop();
            continue;
        }
        bool iterationOk = SendSubscribe(client);
        (void)iterationOk;
        auto ack = ReadFrameUntilType(client, static_cast<uint16_t>(ffprotocol::UiMessageType::SubscribeAck),
                                      kWaitDeadline);
        if (!ack.has_value() || ack->payload.size() < sizeof(ffprotocol::SubscribeAckPayload)) {
            ++failedIterations;
        } else {
            ffprotocol::SubscribeAckPayload ackHeader{};
            std::memcpy(&ackHeader, ack->payload.data(), sizeof(ackHeader));
            if (ackHeader.generationId == 0) {
                ++failedIterations;
            } else {
                std::wstring prefix(
                    reinterpret_cast<const wchar_t*>(ack->payload.data() + sizeof(ackHeader)),
                    ackHeader.sectionNameLengthChars);
                if (!ValidateAndParseSnapshotSection(prefix + L"." + std::to_wstring(ackHeader.generationId),
                                                     ackHeader.generationId).has_value()) {
                    ++failedIterations;
                }
            }
        }
        CloseHandle(client);
        server.Stop();
    }
    Check(failedIterations == 0,
          "100 rapid startup/subscription cycles produce no generation-0 ack, no invalid-data "
          "validation, no mapping race, and no named-mapping collision");
}

// Audit regression (engine.md P1): ClientConnection::Close() must join the
// indexed-search worker WITHOUT holding writeMutex_. The worker's result
// path goes through Write() (writeMutex_), so the old join-under-lock
// ordering could deadlock whenever Close won the race against the worker's
// stopping-check. This stress hammers exactly that interleaving: every
// iteration disconnects while a search result is being written, and Stop()
// must always finish promptly.
void TestCloseWhileSearchWorkerWrites() {
    ffengine::UiServer server;
    server.onRequestIndexedSearch = [](const ffprotocol::IndexedSearchRequest& request,
                                       const ffengine::UiServer::SearchCancellation&) {
        ffprotocol::IndexedSearchResponse response;
        response.requestId = request.requestId;
        response.status = ffprotocol::IndexedSearchStatus::Completed;
        return response;
    };

    constexpr int kIterations = 64;
    bool started = false;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        // Reuse one server across iterations: each cycle creates and closes a
        // ClientConnection, which is where the Close()/worker join lives.
        if (iteration == 0) {
            const DWORD sessionId = static_cast<DWORD>(GetCurrentProcessId() + 300);
            started = server.Start(sessionId);
            Check(started, "search-worker-close server starts");
        }
        if (!started) break;
        const std::wstring pipeName = PipeName(static_cast<DWORD>(GetCurrentProcessId() + 300));
        HANDLE client = ConnectToServer(pipeName);
        Check(client != INVALID_HANDLE_VALUE, "search-worker-close client connects");
        if (client == INVALID_HANDLE_VALUE) break;
        (void)SendSubscribe(client);
        (void)SendIndexedSearch(client, static_cast<uint64_t>(iteration + 1));
        if ((iteration % 3) == 0) Sleep(1); // occasionally let the worker reach its Write
        CloseHandle(client);
    }

    std::atomic<bool> stopped{false};
    const auto start = std::chrono::steady_clock::now();
    std::thread stopper([&] {
        server.Stop();
        stopped = true;
    });
    while (!stopped.load() && std::chrono::steady_clock::now() - start < kShutdownDeadline) {
        Sleep(1);
    }
    Check(stopped.load(), "server stop completes under search-result/close churn");
    stopper.join();
}

} // namespace

int main() {
    TestNormalWatcherUpdate();
    TestBlockedPublicationShutdown();
    TestDisconnectDuringPublication();
    TestIdleShutdown();
    TestIndexedSearchCancellationIsConnectionScoped();
    TestCloseWhileSearchWorkerWrites();
    TestFailedStartRollsBackSnapshot();
    TestUiServerStartFailsOnInitialPublicationFailure();
    TestSubscribeBeforeAnyDirectory();
    TestMultiClientSubscribeBeforePublication();
    TestStartupSubscribeStress();
    return FailureCount() == 0 ? 0 : 1;
}
