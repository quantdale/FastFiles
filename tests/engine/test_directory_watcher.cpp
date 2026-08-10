#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "DirectoryWatcher.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

std::filesystem::path MakeFixtureRoot() {
    wchar_t tempPath[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, tempPath) != 0, "temporary path is available");
    const std::filesystem::path root = std::filesystem::path(tempPath) /
        (L"FastFiles-directory-watcher-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    Check(std::filesystem::create_directories(root, ec), "watcher fixture directory is created");
    return root;
}

void CreateFileIn(const std::filesystem::path& root, const wchar_t* name) {
    const auto path = root / name;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "watcher-test";
    output.close();
    Check(!output.fail(), "fixture file is created");
}

void WaitForNotification(std::condition_variable& condition, std::mutex& mutex,
                        int& notifications, const char* description) {
    std::unique_lock<std::mutex> lock(mutex);
    const bool notified = condition.wait_for(lock, std::chrono::seconds(5), [&] {
        return notifications > 0;
    });
    Check(notified, description);
}

void TestWatcherPublishesAndStops() {
    const std::filesystem::path root = MakeFixtureRoot();
    std::mutex mutex;
    std::condition_variable condition;
    int notifications = 0;

    {
        ffengine::DirectoryWatcher watcher;
        watcher.Watch(root.wstring(), [&](const std::wstring& changedPath) {
            if (changedPath == root.wstring()) {
                std::lock_guard<std::mutex> lock(mutex);
                ++notifications;
                condition.notify_all();
            }
        });

        Sleep(100);
        {
            std::lock_guard<std::mutex> lock(mutex);
            Check(notifications == 0,
                  "arming an overlapped watcher does not report a false change");
        }
        CreateFileIn(root, L"first.txt");
        WaitForNotification(condition, mutex, notifications,
                            "watcher reports a newly-created file");

        watcher.Unwatch(root.wstring());
        {
            std::lock_guard<std::mutex> lock(mutex);
            notifications = 0;
        }
        watcher.Watch(root.wstring(), [&](const std::wstring& changedPath) {
            if (changedPath == root.wstring()) {
                std::lock_guard<std::mutex> lock(mutex);
                ++notifications;
                condition.notify_all();
            }
        });
        Sleep(100);
        CreateFileIn(root, L"second.txt");
        {
            std::unique_lock<std::mutex> lock(mutex);
            const bool restarted = condition.wait_for(lock, std::chrono::seconds(5), [&] {
                return notifications > 0;
            });
            Check(restarted, "watcher can be stopped and restarted");
        }

        watcher.UnwatchAll();
    }

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    Check(!std::filesystem::exists(root), "watcher fixture is removed");
}

void TestWatcherEvictsLeastRecentlyUsedRoot() {
    const std::filesystem::path root = MakeFixtureRoot();
    const std::filesystem::path localAppData = root / L"local-app-data";
    std::error_code error;
    Check(std::filesystem::create_directories(localAppData, error),
          "watcher diagnostics directory fixture is created");

    std::vector<wchar_t> previousLocalAppData(32768);
    const DWORD previousLength = GetEnvironmentVariableW(
        L"LOCALAPPDATA", previousLocalAppData.data(), static_cast<DWORD>(previousLocalAppData.size()));
    Check(SetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.c_str()),
          "watcher test redirects diagnostics to its disposable fixture");

    std::vector<std::filesystem::path> watchedRoots;
    watchedRoots.reserve(ffengine::kMaxWatchedRoots + 1);
    for (size_t index = 0; index <= ffengine::kMaxWatchedRoots; ++index) {
        const auto watched = root / (L"watched-" + std::to_wstring(index));
        Check(std::filesystem::create_directories(watched, error),
              "watcher LRU directory fixture is created");
        watchedRoots.push_back(watched);
    }

    {
        ffengine::DirectoryWatcher watcher;
        for (size_t index = 0; index < ffengine::kMaxWatchedRoots; ++index) {
            watcher.Watch(watchedRoots[index].wstring(), [](const std::wstring&) {});
        }
        // Refresh root zero so root one is the least-recently-watched victim.
        watcher.Watch(watchedRoots.front().wstring(), [](const std::wstring&) {});
        watcher.Watch(watchedRoots.back().wstring(), [](const std::wstring&) {});
        watcher.UnwatchAll();
    }

    const auto diagnosticsPath = localAppData / L"FastFiles" / L"logs" / L"diagnostics.log";
    std::ifstream diagnostics(diagnosticsPath, std::ios::binary);
    const std::string diagnosticText((std::istreambuf_iterator<char>(diagnostics)),
                                     std::istreambuf_iterator<char>());
    Check(diagnosticText.find("state=watch-evicted") != std::string::npos,
          "watcher LRU eviction is recorded in diagnostics");
    Check(diagnosticText.find(watchedRoots[1].string()) != std::string::npos,
          "the least-recently-watched root is evicted after the active root is refreshed");
    diagnostics.close();

    if (previousLength == 0) {
        SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
    } else {
        SetEnvironmentVariableW(L"LOCALAPPDATA", previousLocalAppData.data());
    }
    std::filesystem::remove_all(root, error);
    Check(!std::filesystem::exists(root), "watcher LRU fixture is removed");
}

#ifdef FF_DIRECTORY_WATCHER_TESTING
void TestWatcherRecoversFromInjectedOverflow() {
    const std::filesystem::path root = MakeFixtureRoot();
    const std::filesystem::path localAppData = root / L"local-app-data";
    std::error_code error;
    Check(std::filesystem::create_directories(localAppData, error),
          "overflow diagnostics directory fixture is created");

    std::vector<wchar_t> previousLocalAppData(32768);
    const DWORD previousLength = GetEnvironmentVariableW(
        L"LOCALAPPDATA", previousLocalAppData.data(), static_cast<DWORD>(previousLocalAppData.size()));
    Check(SetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.c_str()),
          "overflow test redirects diagnostics to its disposable fixture");

    auto completions = std::make_shared<std::deque<ffengine::DirectoryWatcher::TestCompletion>>(
        std::deque<ffengine::DirectoryWatcher::TestCompletion>{
            {ERROR_SUCCESS, 0}, {ERROR_NOTIFY_ENUM_DIR, 0},
            {ERROR_SUCCESS, 16}, {ERROR_NOTIFY_ENUM_DIR, 0}});
    std::mutex callbackMutex;
    std::condition_variable callbackCondition;
    int callbacks = 0;
    {
        ffengine::DirectoryWatcher watcher([completions] {
            if (completions->empty()) return std::optional<ffengine::DirectoryWatcher::TestCompletion>{};
            const auto completion = completions->front();
            completions->pop_front();
            return std::optional<ffengine::DirectoryWatcher::TestCompletion>{completion};
        });
        watcher.Watch(root.wstring(), [&](const std::wstring&) {
            std::lock_guard<std::mutex> lock(callbackMutex);
            ++callbacks;
            callbackCondition.notify_all();
        });
        std::unique_lock<std::mutex> lock(callbackMutex);
        Check(callbackCondition.wait_for(lock, std::chrono::seconds(5), [&] { return callbacks >= 4; }),
              "overflow recovery re-enumerates, re-arms, and resets its consecutive count");
        watcher.Unwatch(root.wstring());
    }

    const auto diagnosticsPath = localAppData / L"FastFiles" / L"logs" / L"diagnostics.log";
    std::ifstream diagnostics(diagnosticsPath, std::ios::binary);
    const std::string diagnosticText((std::istreambuf_iterator<char>(diagnostics)),
                                     std::istreambuf_iterator<char>());
    Check(diagnosticText.find("state=watch-buffer-overflow") != std::string::npos,
          "injected overflow is recorded in diagnostics");
    Check(diagnosticText.find("state=watch-gave-up") == std::string::npos,
          "a recoverable overflow does not give up the watch");
    diagnostics.close();

    if (previousLength == 0) {
        SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
    } else {
        SetEnvironmentVariableW(L"LOCALAPPDATA", previousLocalAppData.data());
    }
    std::filesystem::remove_all(root, error);
    Check(!std::filesystem::exists(root), "overflow recovery fixture is removed");
}

void TestWatcherGivesUpAfterEightInjectedOverflows() {
    const std::filesystem::path root = MakeFixtureRoot();
    const std::filesystem::path localAppData = root / L"local-app-data";
    std::error_code error;
    Check(std::filesystem::create_directories(localAppData, error),
          "give-up diagnostics directory fixture is created");

    std::vector<wchar_t> previousLocalAppData(32768);
    const DWORD previousLength = GetEnvironmentVariableW(
        L"LOCALAPPDATA", previousLocalAppData.data(), static_cast<DWORD>(previousLocalAppData.size()));
    Check(SetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.c_str()),
          "give-up test redirects diagnostics to its disposable fixture");

    auto completions = std::make_shared<std::deque<ffengine::DirectoryWatcher::TestCompletion>>();
    for (int index = 0; index < 8; ++index) {
        completions->push_back({ERROR_NOTIFY_ENUM_DIR, 0});
    }
    std::atomic<int> callbacks{0};
    {
        ffengine::DirectoryWatcher watcher([completions] {
            if (completions->empty()) return std::optional<ffengine::DirectoryWatcher::TestCompletion>{};
            const auto completion = completions->front();
            completions->pop_front();
            return std::optional<ffengine::DirectoryWatcher::TestCompletion>{completion};
        });
        watcher.Watch(root.wstring(), [&](const std::wstring&) { ++callbacks; });
        for (int attempt = 0; attempt < 50 && callbacks.load() < 8; ++attempt) Sleep(10);
        Check(callbacks.load() == 8, "watcher invokes re-enumeration for each consecutive overflow");
        watcher.Unwatch(root.wstring());
    }

    const auto diagnosticsPath = localAppData / L"FastFiles" / L"logs" / L"diagnostics.log";
    std::ifstream diagnostics(diagnosticsPath, std::ios::binary);
    const std::string diagnosticText((std::istreambuf_iterator<char>(diagnostics)),
                                     std::istreambuf_iterator<char>());
    Check(diagnosticText.find("state=watch-buffer-overflow") != std::string::npos,
          "consecutive overflows are recorded in diagnostics");
    Check(diagnosticText.find("state=watch-gave-up") != std::string::npos,
          "watcher gives up after eight consecutive overflows");
    diagnostics.close();

    if (previousLength == 0) {
        SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
    } else {
        SetEnvironmentVariableW(L"LOCALAPPDATA", previousLocalAppData.data());
    }
    std::filesystem::remove_all(root, error);
    Check(!std::filesystem::exists(root), "give-up fixture is removed");
}
#endif

} // namespace

int main() {
    TestWatcherPublishesAndStops();
    TestWatcherEvictsLeastRecentlyUsedRoot();
#ifdef FF_DIRECTORY_WATCHER_TESTING
    TestWatcherRecoversFromInjectedOverflow();
    TestWatcherGivesUpAfterEightInjectedOverflows();
#endif
    return FailureCount() == 0 ? 0 : 1;
}
