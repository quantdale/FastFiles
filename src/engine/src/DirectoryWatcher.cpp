#include "DirectoryWatcher.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#include "ffprotocol/Diagnostics.h"

namespace ffengine {

namespace {
constexpr DWORD kNotifyFilter =
    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
    FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
// recovery-degraded-browsing 5.2: after this many consecutive change-buffer
// overflows the watch stops (with a log entry) instead of re-arming forever
// against a directory that changes faster than the buffer can drain.
constexpr int kMaxConsecutiveWatchOverflows = 8;

std::wstring ExtendedApiPath(const std::wstring& path) {
    if (path.starts_with(L"\\\\?\\") || path.starts_with(L"\\\\.\\")) {
        return path;
    }
    if (path.starts_with(L"\\\\")) {
        return L"\\\\?\\UNC\\" + path.substr(2);
    }
    if (path.size() >= 2 && path[1] == L':') {
        return L"\\\\?\\" + path;
    }
    return path;
}
}

DirectoryWatcher::~DirectoryWatcher() {
    UnwatchAll();
}

void DirectoryWatcher::Watch(const std::wstring& path, ChangeCallback onChanged) {
    std::vector<std::unique_ptr<WatchEntry>> toStop;
    std::shared_ptr<WatchStartupState> startupState;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto existing = watches_.find(path);
        if (existing != watches_.end() && !existing->second->finished->load()) {
            // Refresh recency so an actively browsed path is never the
            // eviction victim (recovery-degraded-browsing 5.1).
            auto it = std::find(accessOrder_.begin(), accessOrder_.end(), path);
            if (it != accessOrder_.end()) {
                accessOrder_.erase(it);
                accessOrder_.push_back(path);
            }
            return;
        }
        if (existing != watches_.end()) {
            // A worker that gave up after an error remains joinable in the
            // map. Remove it before replacing the watch so a later browse can
            // actually restart monitoring that directory.
            toStop.push_back(std::move(existing->second));
            watches_.erase(existing);
            accessOrder_.erase(std::remove(accessOrder_.begin(), accessOrder_.end(), path), accessOrder_.end());
        }

        const std::wstring apiPath = ExtendedApiPath(path);
        HANDLE directoryHandle = CreateFileW(
            apiPath.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (directoryHandle == INVALID_HANDLE_VALUE) {
            // e.g. permission-denied or path gone; the old dead entry (if
            // any) is still stopped below, but no replacement is installed.
        } else {
            if (watches_.size() >= kMaxWatchedRoots) {
            // Evict the least-recently-watched root (recovery-degraded-
            // browsing 5.1). The UI re-requests a path when it is browsed
            // again, so the eviction is invisible to normal use.
                while (!accessOrder_.empty() && watches_.count(accessOrder_.front()) == 0) {
                    accessOrder_.pop_front(); // stale entries from evicted watches
                }
                if (!accessOrder_.empty()) {
                    const std::wstring victim = accessOrder_.front();
                    accessOrder_.pop_front();
                    auto victimIt = watches_.find(victim);
                    if (victimIt != watches_.end()) {
                        toStop.push_back(std::move(victimIt->second));
                        watches_.erase(victimIt);
                        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                                      ffprotocol::DiagnosticCode::FFDIAG_WATCH_EVICTED,
                                                      victim, {}, L"watch-evicted", ERROR_TOO_MANY_OPEN_FILES, 0});
                    }
                }
            }

            HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (stopEvent == nullptr) {
                CloseHandle(directoryHandle);
            } else {
                auto entry = std::make_unique<WatchEntry>();
                entry->directoryHandle = directoryHandle;
                entry->stopEvent = stopEvent;
                startupState = entry->startupState;

                const auto stopRequested = entry->stopRequested;
                const auto finished = entry->finished;
#ifdef FF_DIRECTORY_WATCHER_TESTING
                const TestCompletionProvider testCompletionProvider = testCompletionProvider_;
                entry->thread = std::thread([directoryHandle, stopEvent, path,
                                             callback = std::move(onChanged), stopRequested, finished, startupState,
                                             testCompletionProvider] {
                    WatchLoop(directoryHandle, stopEvent, path, callback, stopRequested, finished, startupState,
                              testCompletionProvider);
                });
#else
                entry->thread = std::thread([directoryHandle, stopEvent, path,
                                             callback = std::move(onChanged), stopRequested, finished, startupState] {
                    WatchLoop(directoryHandle, stopEvent, path, callback, stopRequested, finished, startupState);
                });
#endif

                watches_.emplace(path, std::move(entry));
                accessOrder_.push_back(path);
            }
        }
    }

    for (auto& entry : toStop) {
        StopEntry(*entry); // outside the lock: StopEntry joins the thread
    }

    if (startupState) {
        std::unique_lock<std::mutex> lock(startupState->mutex);
        startupState->condition.wait_for(lock, std::chrono::seconds(1), [&startupState] {
            return startupState->signaled;
        });
    }
}

void DirectoryWatcher::WatchLoop(HANDLE directoryHandle, HANDLE stopEvent, const std::wstring& path,
                                 const ChangeCallback& callback,
                                 const std::shared_ptr<std::atomic_bool>& stopRequested,
                                 const std::shared_ptr<std::atomic_bool>& finished
                                 , const std::shared_ptr<WatchStartupState>& startupState
#ifdef FF_DIRECTORY_WATCHER_TESTING
                                 , const TestCompletionProvider& testCompletionProvider
#endif
                                 ) {
    bool startupSignaled = false;
    const auto signalStartup = [&] {
        if (startupSignaled) {
            return;
        }
        startupSignaled = true;
        {
            std::lock_guard<std::mutex> lock(startupState->mutex);
            startupState->signaled = true;
        }
        startupState->condition.notify_all();
    };

    std::vector<uint8_t> buffer(64 * 1024);
    int consecutiveOverflows = 0;
    HANDLE ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ioEvent == nullptr) {
        signalStartup();
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                      ffprotocol::DiagnosticCode::FFDIAG_WATCH_EVENT_CREATE_FAILED,
                                      path, {}, L"watch-event-create-failed", GetLastError(), 0});
        finished->store(true);
        return;
    }

#ifdef FF_DIRECTORY_WATCHER_TESTING
    if (testCompletionProvider) {
        // The injected provider does not issue a real overlapped read, so its
        // worker is ready as soon as the event object exists.
        signalStartup();
    }
#endif

    OVERLAPPED overlapped{};
    overlapped.hEvent = ioEvent;

    const auto processCompletion = [&](DWORD error, DWORD bytesReturned) {
        if (stopRequested->load() || error == ERROR_OPERATION_ABORTED) {
            return false;
        }
        if (error == ERROR_SUCCESS && bytesReturned != 0) {
            if (stopRequested->load()) return false;
            consecutiveOverflows = 0;
            // Coalesced notification: the exact set of FILE_NOTIFY_INFORMATION
            // records isn't needed -- callers re-enumerate the whole
            // directory and republish a snapshot generation.
            callback(path);
            return true;
        }

        // A zero-byte completion means the change buffer overflowed. Treat
        // that form exactly like ERROR_NOTIFY_ENUM_DIR so the directory is
        // re-enumerated instead of silently losing synchronization.
        if (error == ERROR_SUCCESS && bytesReturned == 0) {
            error = ERROR_NOTIFY_ENUM_DIR;
        }
        if (error == ERROR_NOTIFY_ENUM_DIR) {
            // recovery-degraded-browsing 5.2: the change buffer overflowed --
            // the notification stream is unreliable. Detect, log, re-enumerate
            // (the callback republishes a fresh generation), and re-arm the
            // watch; never silently abandon synchronization.
            ++consecutiveOverflows;
            ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                          ffprotocol::DiagnosticCode::FFDIAG_WATCH_BUFFER_OVERFLOW,
                                          path, {}, L"watch-buffer-overflow", error, 0});
            callback(path);
            if (stopRequested->load()) return false;
            if (consecutiveOverflows >= kMaxConsecutiveWatchOverflows) {
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                              ffprotocol::DiagnosticCode::FFDIAG_WATCH_GAVE_UP,
                                              path, {}, L"watch-gave-up", error, 0});
                return false;
            }
            return true;
        }
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                      ffprotocol::DiagnosticCode::FFDIAG_WATCH_FAILED,
                                      path, {}, L"watch-failed", error, 0});
        return false;
    };

    while (!stopRequested->load()) {
#ifdef FF_DIRECTORY_WATCHER_TESTING
        if (testCompletionProvider) {
            const std::optional<TestCompletion> completion = testCompletionProvider();
            if (!completion.has_value()) break;
            if (!processCompletion(completion->error, completion->bytesReturned)) break;
            continue;
        }
#endif
        ResetEvent(ioEvent);
        DWORD bytesReturned = 0;
        const BOOL started = ReadDirectoryChangesW(
            directoryHandle, buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
            kNotifyFilter, &bytesReturned, &overlapped, nullptr);

        DWORD error = ERROR_SUCCESS;
        if (!started) {
            error = GetLastError();
        }

        // Do not let callers race the first directory change with worker
        // startup. A successful or pending call has armed the real watcher;
        // an immediate failure has made the watch terminal and also releases
        // the caller from the bounded startup wait.
        signalStartup();

        // ReadDirectoryChangesW is being used with an OVERLAPPED structure.
        // In that mode lpBytesReturned is not populated by the initial call;
        // it must be read from GetOverlappedResult even when the operation
        // completed synchronously (started == TRUE). The previous code only
        // called GetOverlappedResult for ERROR_IO_PENDING, so every ordinary
        // successful arm looked like a zero-byte buffer overflow and a root
        // watch gave up after eight false strikes.
        if (started || error == ERROR_IO_PENDING) {
            // Even a TRUE return can represent an overlapped operation whose
            // completion event has not yet been observed. Waiting on the stop
            // event as well keeps shutdown interruptible while ensuring the
            // byte count always comes from GetOverlappedResult.
            const HANDLE waitHandles[] = {stopEvent, ioEvent};
            const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
            if (waitResult == WAIT_OBJECT_0 || stopRequested->load()) {
                CancelIoEx(directoryHandle, &overlapped);
                break;
            }
            if (waitResult != WAIT_OBJECT_0 + 1) {
                error = GetLastError();
            } else if (!GetOverlappedResult(directoryHandle, &overlapped, &bytesReturned, FALSE)) {
                error = GetLastError();
            } else {
                error = ERROR_SUCCESS;
            }
        }

        if (!processCompletion(error, bytesReturned)) break;
    }
    CloseHandle(ioEvent);
    finished->store(true);
}

void DirectoryWatcher::StopEntry(WatchEntry& entry) {
    entry.stopRequested->store(true);
    if (entry.stopEvent != nullptr) {
        SetEvent(entry.stopEvent);
    }
    if (entry.thread.joinable()) {
        CancelIoEx(entry.directoryHandle, nullptr);
        entry.thread.join();
    }
    if (entry.directoryHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(entry.directoryHandle);
        entry.directoryHandle = INVALID_HANDLE_VALUE;
    }
    if (entry.stopEvent != nullptr) {
        CloseHandle(entry.stopEvent);
        entry.stopEvent = nullptr;
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
        accessOrder_.erase(std::remove(accessOrder_.begin(), accessOrder_.end(), path), accessOrder_.end());
    }
    StopEntry(*entry);
}

void DirectoryWatcher::UnwatchAll() {
    std::map<std::wstring, std::unique_ptr<WatchEntry>> toStop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        toStop = std::move(watches_);
        watches_.clear();
        accessOrder_.clear();
    }
    for (auto& [path, entry] : toStop) {
        StopEntry(*entry);
    }
}

} // namespace ffengine
