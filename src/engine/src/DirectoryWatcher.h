#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace ffengine {

// recovery-degraded-browsing D5: at most this many concurrently watched
// roots. When the bound is reached the least-recently-watched root is
// evicted (a Watch of an already-watched path refreshes its recency, so
// actively browsed paths are never the victim).
constexpr size_t kMaxWatchedRoots = 32;

// Task 4.6: one ReadDirectoryChangesW watch per directory the user has
// browsed or explicitly pinned, while in degraded mode. onChanged is
// invoked (with the watched path) whenever the OS reports a change --
// callers re-enumerate that path and republish a new snapshot generation
// (spec "A newly created file appears without manual refresh").
class DirectoryWatcher {
public:
    ~DirectoryWatcher();

    using ChangeCallback = std::function<void(const std::wstring& path)>;

    // Idempotent: watching an already-watched path is a no-op (and refreshes
    // its recency). Bounded by kMaxWatchedRoots with LRU eviction.
    void Watch(const std::wstring& path, ChangeCallback onChanged);
    void Unwatch(const std::wstring& path);
    void UnwatchAll();

private:
    struct WatchEntry {
        HANDLE directoryHandle = INVALID_HANDLE_VALUE;
        HANDLE stopEvent = nullptr;
        std::thread thread;
        std::shared_ptr<std::atomic_bool> stopRequested = std::make_shared<std::atomic_bool>(false);
        std::shared_ptr<std::atomic_bool> finished = std::make_shared<std::atomic_bool>(false);
    };

    void StopEntry(WatchEntry& entry);
    static void WatchLoop(HANDLE directoryHandle, HANDLE stopEvent, const std::wstring& path,
                          const ChangeCallback& callback,
                          const std::shared_ptr<std::atomic_bool>& stopRequested,
                          const std::shared_ptr<std::atomic_bool>& finished);

    std::mutex mutex_;
    std::map<std::wstring, std::unique_ptr<WatchEntry>> watches_;
    std::deque<std::wstring> accessOrder_; // least-recently-watched first
};

} // namespace ffengine
