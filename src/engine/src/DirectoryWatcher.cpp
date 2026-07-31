#include "DirectoryWatcher.h"

#include <cstdint>
#include <vector>

namespace ffengine {

namespace {
constexpr DWORD kNotifyFilter =
    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
    FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
}

DirectoryWatcher::~DirectoryWatcher() {
    UnwatchAll();
}

void DirectoryWatcher::Watch(const std::wstring& path, ChangeCallback onChanged) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (watches_.count(path) != 0) {
        return;
    }

    HANDLE directoryHandle = CreateFileW(
        path.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directoryHandle == INVALID_HANDLE_VALUE) {
        return; // e.g. permission-denied or path gone; nothing to watch
    }

    auto entry = std::make_unique<WatchEntry>();
    entry->directoryHandle = directoryHandle;

    entry->thread = std::thread([directoryHandle, path, callback = std::move(onChanged)] {
        std::vector<uint8_t> buffer(64 * 1024);
        for (;;) {
            DWORD bytesReturned = 0;
            // Synchronous (lpOverlapped == nullptr): stopped via
            // CancelSynchronousIo on this thread's handle (see StopEntry).
            const BOOL ok = ReadDirectoryChangesW(
                directoryHandle, buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
                kNotifyFilter, &bytesReturned, nullptr, nullptr);
            if (!ok) {
                break; // cancelled (shutdown) or the directory handle broke
            }
            // Coalesced notification: the exact set of FILE_NOTIFY_INFORMATION
            // records isn't needed -- callers re-enumerate the whole
            // directory and republish a snapshot generation.
            callback(path);
        }
    });

    watches_.emplace(path, std::move(entry));
}

void DirectoryWatcher::StopEntry(WatchEntry& entry) {
    if (entry.thread.joinable()) {
        CancelSynchronousIo(entry.thread.native_handle());
        entry.thread.join();
    }
    if (entry.directoryHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(entry.directoryHandle);
        entry.directoryHandle = INVALID_HANDLE_VALUE;
    }
}

void DirectoryWatcher::Unwatch(const std::wstring& path) {
    std::unique_ptr<WatchEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = watches_.find(path);
        if (it == watches_.end()) {
            return;
        }
        entry = std::move(it->second);
        watches_.erase(it);
    }
    StopEntry(*entry);
}

void DirectoryWatcher::UnwatchAll() {
    std::map<std::wstring, std::unique_ptr<WatchEntry>> toStop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        toStop = std::move(watches_);
        watches_.clear();
    }
    for (auto& [path, entry] : toStop) {
        StopEntry(*entry);
    }
}

} // namespace ffengine
