#pragma once
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

namespace ffengine {

// Task 4.6: one ReadDirectoryChangesW watch per directory the user has
// browsed or explicitly pinned, while in degraded mode. onChanged is
// invoked (with the watched path) whenever the OS reports a change --
// callers re-enumerate that path and republish a new snapshot generation
// (spec "A newly created file appears without manual refresh").
class DirectoryWatcher {
public:
    ~DirectoryWatcher();

    using ChangeCallback = std::function<void(const std::wstring& path)>;

    // Idempotent: watching an already-watched path is a no-op.
    void Watch(const std::wstring& path, ChangeCallback onChanged);
    void Unwatch(const std::wstring& path);
    void UnwatchAll();

private:
    struct WatchEntry {
        HANDLE directoryHandle = INVALID_HANDLE_VALUE;
        std::thread thread;
    };

    void StopEntry(WatchEntry& entry);

    std::mutex mutex_;
    std::map<std::wstring, std::unique_ptr<WatchEntry>> watches_;
};

} // namespace ffengine
