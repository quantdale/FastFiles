#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
    DirectoryWatcher() = default;

#ifdef FF_DIRECTORY_WATCHER_TESTING
    // Test-only completion injection keeps the production watcher on the real
    // ReadDirectoryChangesW path while making overflow recovery deterministic.
    struct TestCompletion {
        DWORD error = ERROR_SUCCESS;
        DWORD bytesReturned = 0;
    };
    using TestCompletionProvider = std::function<std::optional<TestCompletion>()>;

    explicit DirectoryWatcher(TestCompletionProvider testCompletionProvider)
        : testCompletionProvider_(std::move(testCompletionProvider)) {}
#endif

    ~DirectoryWatcher();

    using ChangeCallback = std::function<void(const std::wstring& path)>;

    // Idempotent: watching an already-watched path is a no-op (and refreshes
    // its recency). Bounded by kMaxWatchedRoots with LRU eviction.
    void Watch(const std::wstring& path, ChangeCallback onChanged);
    void Unwatch(const std::wstring& path);
    void UnwatchAll();

private:
    struct CaseInsensitivePathLess {
        bool operator()(const std::wstring& left, const std::wstring& right) const noexcept;
    };

    struct WatchStartupState {
        std::mutex mutex;
        std::condition_variable condition;
        bool signaled = false;
    };

    struct WatchEntry {
        HANDLE directoryHandle = INVALID_HANDLE_VALUE;
        HANDLE stopEvent = nullptr;
        std::thread thread;
        std::shared_ptr<std::atomic_bool> stopRequested = std::make_shared<std::atomic_bool>(false);
        std::shared_ptr<std::atomic_bool> finished = std::make_shared<std::atomic_bool>(false);
        std::shared_ptr<WatchStartupState> startupState = std::make_shared<WatchStartupState>();
    };

    void StopEntry(WatchEntry& entry);
    static void WatchLoop(HANDLE directoryHandle, HANDLE stopEvent, const std::wstring& path,
                          const ChangeCallback& callback,
                          const std::shared_ptr<std::atomic_bool>& stopRequested,
                          const std::shared_ptr<std::atomic_bool>& finished,
                          const std::shared_ptr<WatchStartupState>& startupState
#ifdef FF_DIRECTORY_WATCHER_TESTING
                          , const TestCompletionProvider& testCompletionProvider
#endif
                          );

    std::mutex mutex_;
    std::map<std::wstring, std::unique_ptr<WatchEntry>, CaseInsensitivePathLess> watches_;
    std::deque<std::wstring> accessOrder_; // least-recently-watched first
#ifdef FF_DIRECTORY_WATCHER_TESTING
    TestCompletionProvider testCompletionProvider_;
#endif
};

} // namespace ffengine
