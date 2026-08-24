#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "EngineClient.h"
#include "UiServer.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffsearch/Search.h"
#include "ffsetup/Identifiers.h"

namespace {

constexpr wchar_t kFixtureMarker[] = L"FastFiles-active-path-resilience-";
constexpr std::chrono::milliseconds kWaitSlice{25};
constexpr std::chrono::seconds kDefaultSoakDuration{1};
constexpr size_t kMaxFixtureEntries = 96;

struct HarnessState {
    std::mutex mutex;
    std::condition_variable condition;
    bool connected = false;
    bool directoryError = false;
    uint64_t generation = 0;
};

uint64_t NextSeed(uint64_t& seed) {
    seed = seed * 1664525ULL + 1013904223ULL;
    return seed;
}

bool IsOwnedFixture(const std::filesystem::path& fixture) {
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) return false;
    const auto temp = std::filesystem::path(tempPath);
    std::error_code error;
    const bool sameDirectory = std::filesystem::equivalent(fixture.parent_path(), temp, error);
    return sameDirectory && !error && fixture.filename().wstring().starts_with(kFixtureMarker);
}

bool CreateFixtureFile(const std::filesystem::path& path, uint64_t value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "active-path-resilience-" << value;
    output.close();
    return output.good();
}

std::set<std::wstring> EnumeratedNames(const std::filesystem::path& root) {
    std::set<std::wstring> names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (error) break;
        names.insert(entry.path().filename().wstring());
    }
    return names;
}

std::set<std::wstring> SnapshotNames(const ffprotocol::SnapshotDirectory& directory) {
    std::set<std::wstring> names;
    for (const auto& entry : directory.entries) names.insert(entry.name);
    return names;
}

bool TrimFixture(const std::filesystem::path& root) {
    std::error_code error;
    std::vector<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (error) return false;
        entries.push_back(entry.path());
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.filename().wstring() < right.filename().wstring();
    });

    while (entries.size() > kMaxFixtureEntries) {
        const auto candidate = entries.front();
        entries.erase(entries.begin());
        if (candidate.filename() == L"seed.txt" || candidate.filename() == L"unicode-U0001F4C1.txt") {
            entries.push_back(candidate);
            continue;
        }
        error.clear();
        if (!std::filesystem::remove(candidate, error) || error) return false;
    }
    return true;
}

std::vector<ffsearch::Candidate> CandidatesFromSnapshot(const ffui::SnapshotReadResult& snapshot,
                                                         const std::wstring& root) {
    std::vector<ffsearch::Candidate> candidates;
    const auto directory = snapshot.directories.find(root);
    if (directory == snapshot.directories.end()) return candidates;
    candidates.reserve(directory->second.entries.size());
    uint64_t id = 1;
    for (const auto& entry : directory->second.entries) {
        ffsearch::Candidate candidate;
        candidate.name = entry.name;
        candidate.folder = root;
        candidate.sizeBytes = entry.sizeBytes;
        candidate.allocatedSizeBytes = entry.allocatedSizeBytes;
        candidate.allocatedSizeKnown = entry.allocatedSizeKnown;
        candidate.isDirectory = entry.isDirectory;
        candidate.createdTime = entry.creationTime;
        candidate.modifiedTime = entry.lastModifiedTime;
        candidate.id = id++;
        candidate.persistentIndexed = false;
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

bool WaitForRootTruth(ffui::EngineClient& client, HarnessState& state, const std::wstring& root,
                      const std::set<std::wstring>& expected, uint64_t previousGeneration,
                      std::chrono::steady_clock::time_point deadline) {
    uint64_t observedGeneration = previousGeneration;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = client.ReadSnapshotWithGeneration();
        if (snapshot.has_value()) {
            observedGeneration = (std::max)(observedGeneration, snapshot->generation);
            const auto directory = snapshot->directories.find(root);
            if (snapshot->generation > previousGeneration && directory != snapshot->directories.end() &&
                directory->second.status == ffprotocol::DirectoryEnumerationStatus::Success &&
                SnapshotNames(directory->second) == expected) {
                return true;
            }
        }
        std::unique_lock<std::mutex> lock(state.mutex);
        state.condition.wait_for(lock, kWaitSlice, [&] {
            return state.directoryError || state.generation > observedGeneration;
        });
        if (state.directoryError) return false;
    }
    return false;
}

bool StartHarness(std::unique_ptr<ffui::EngineClient>& client, HarnessState& state,
                  DWORD sessionId, const std::wstring& root) {
    state.connected = false;
    state.directoryError = false;
    state.generation = 0;
    client = std::make_unique<ffui::EngineClient>();
    client->Start(
        [&state](uint64_t generation) {
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                state.generation = (std::max)(state.generation, generation);
            }
            state.condition.notify_all();
        },
        [&state](ffui::EngineConnectionState connectionState, bool) {
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (connectionState == ffui::EngineConnectionState::Connected) {
                    state.connected = true;
                }
            }
            state.condition.notify_all();
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
        if (!state.condition.wait_for(lock, std::chrono::seconds(10), [&state] { return state.connected; })) {
            return false;
        }
    }
    client->RequestDirectory(root);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = client->ReadSnapshotWithGeneration();
        if (snapshot.has_value() && snapshot->directories.contains(root)) return true;
        std::unique_lock<std::mutex> lock(state.mutex);
        state.condition.wait_for(lock, kWaitSlice);
    }
    (void)sessionId;
    return false;
}

bool RunSoak(std::chrono::seconds duration, uint64_t seed) {
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) {
        std::cerr << "FAIL: temporary-path-unavailable\n";
        return false;
    }
    const auto root = std::filesystem::path(tempPath) /
        (std::wstring(kFixtureMarker) + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(seed));
    if (!IsOwnedFixture(root)) {
        std::cerr << "FAIL: fixture-boundary-rejected\n";
        return false;
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (!std::filesystem::create_directories(root, error)) {
        std::cerr << "FAIL: fixture-create\n";
        return false;
    }
    if (!CreateFixtureFile(root / L"seed.txt", seed)
        || !CreateFixtureFile(root / L"unicode-U0001F4C1.txt", seed + 1)) {
        std::cerr << "FAIL: fixture-file-create\n";
        std::filesystem::remove_all(root, error);
        return false;
    }

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    ffengine::UiServer server;
    if (!server.Start(sessionId)) {
        std::cerr << "FAIL: ui-server-start\n";
        std::filesystem::remove_all(root, error);
        return false;
    }

    HarnessState state;
    std::unique_ptr<ffui::EngineClient> client;
    bool ok = StartHarness(client, state, sessionId, root.wstring());
    uint64_t generation = 0;
    uint64_t iterations = 0;
    uint64_t restarts = 0;
    const auto deadline = std::chrono::steady_clock::now() + duration;

    while (ok && std::chrono::steady_clock::now() < deadline) {
        const uint64_t step = NextSeed(seed);
        const auto expectedBefore = EnumeratedNames(root);
        bool expectsGeneration = true;
        const auto files = [&] {
            std::vector<std::filesystem::path> values;
            for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
                if (!error && entry.is_regular_file(error)) values.push_back(entry.path());
                error.clear();
            }
            return values;
        }();

        switch (step % 7) {
            case 0:
                ok = CreateFixtureFile(root / (L"created-" + std::to_wstring(iterations) + L"-U0001F600.txt"), step);
                break;
            case 1:
                if (!files.empty()) {
                    const auto replacement = root / (L"renamed-" + std::to_wstring(iterations) + L".txt");
                    error.clear();
                    std::filesystem::rename(files.front(), replacement, error);
                    if (error) ok = false;
                }
                break;
            case 2:
                error.clear();
                ok = std::filesystem::create_directories(
                    root / (L"nested-" + std::to_wstring(iterations)), error) && !error;
                break;
            case 3:
                if (!files.empty()) {
                    const auto copy = root / (L"copy-" + std::to_wstring(iterations) + L".txt");
                    ok = CopyFileW(files.front().c_str(), copy.c_str(), TRUE) != FALSE;
                }
                break;
            case 4:
                if (files.size() > 2) ok = DeleteFileW(files.back().c_str()) != FALSE;
                break;
            case 5:
                client->RequestDirectory(root.wstring());
                break;
            case 6: {
                expectsGeneration = false;
                const auto snapshot = client->ReadSnapshotWithGeneration();
                if (!snapshot.has_value()) {
                    ok = false;
                    break;
                }
                auto candidates = CandidatesFromSnapshot(*snapshot, root.wstring());
                ffsearch::SearchRequest request;
                request.query = ffsearch::ParseQuery(L"txt", ffsearch::FilterRegistry::WithDefaults());
                request.scope = ffsearch::SearchScope::CurrentFolder;
                request.scopePath = root.wstring();
                const auto response = ffsearch::ExecuteSearch(candidates, request);
                ok = !response.cancelled;
                break;
            }
            default:
                break;
        }
        if (!ok) break;

        if (!TrimFixture(root)) {
            ok = false;
            break;
        }

        const auto expected = EnumeratedNames(root);
        if (expectsGeneration) {
            const bool mutationObserved = WaitForRootTruth(
                *client, state, root.wstring(), expected, generation,
                std::chrono::steady_clock::now() + std::chrono::seconds(3));
            if (!mutationObserved && expected != expectedBefore) {
                ok = false;
                break;
            }
        }
        if (const auto snapshot = client->ReadSnapshotWithGeneration(); snapshot.has_value()) {
            generation = (std::max)(generation, snapshot->generation);
        }

        ++iterations;
        if (iterations % 64 == 0 && std::chrono::steady_clock::now() < deadline) {
            std::cout << "PROGRESS iterations=" << iterations << " restarts=" << restarts << std::endl;
            client->Stop();
            client.reset();
            server.Stop();
            if (!server.Start(sessionId) || !StartHarness(client, state, sessionId, root.wstring())) {
                ok = false;
                break;
            }
            ++restarts;
            if (const auto snapshot = client->ReadSnapshotWithGeneration(); snapshot.has_value()) {
                generation = snapshot->generation;
            }
        }
    }

    std::cout << "TEARDOWN client-stop iterations=" << iterations << " restarts=" << restarts << std::endl;
    if (client != nullptr) client->Stop();
    std::cout << "TEARDOWN client-stopped" << std::endl;
    server.Stop();
    std::cout << "TEARDOWN server-stopped" << std::endl;
    const bool cleanupBoundary = IsOwnedFixture(root);
    error.clear();
    std::filesystem::remove_all(root, error);
    const bool cleaned = !error && !std::filesystem::exists(root);
    if (!ok || !cleanupBoundary || !cleaned) {
        std::cerr << "FAIL: active-path-soak iterations=" << iterations
                  << " restarts=" << restarts << "\n";
        return false;
    }
    std::cout << "PASS: active-path-soak iterations=" << iterations
              << " restarts=" << restarts << "\n";
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    uint64_t seed = 0xA17E5EEDULL;
    std::chrono::seconds duration = kDefaultSoakDuration;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--seed" && index + 1 < argc) {
            seed = std::stoull(argv[++index]);
        } else if (argument == L"--soak-seconds" && index + 1 < argc) {
            duration = std::chrono::seconds(std::stoll(argv[++index]));
        } else {
            std::wcerr << L"FAIL: unknown-argument\n";
            return 2;
        }
    }
    if (duration.count() <= 0 || duration.count() > 3600) {
        std::wcerr << L"FAIL: invalid-duration\n";
        return 2;
    }
    return RunSoak(duration, seed) ? 0 : 1;
}
