#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

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

} // namespace

int main() {
    TestWatcherPublishesAndStops();
    return FailureCount() == 0 ? 0 : 1;
}
