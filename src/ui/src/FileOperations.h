#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace ffui {

// Posted to the UI HWND.  The receiver owns and deletes the event; it never
// contains a COM interface or any other apartment-bound value.
constexpr UINT WM_APP_FILE_OPERATION_EVENT = WM_APP + 2;

enum class FileOperationKind { Copy, Move, Rename, CreateFolder, CreateFile, Delete };
enum class FileOperationEventKind { Queued, Started, Progress, Completed, Cancelled };

struct FileOperationFailure {
    std::wstring path;
    HRESULT error = S_OK;
};

struct FileOperationEvent {
    FileOperationEventKind kind;
    FileOperationKind operation;
    std::wstring currentItem;
    unsigned int completed = 0;
    unsigned int total = 0;
    unsigned int percent = 0;
    std::vector<FileOperationFailure> failures;
    std::vector<std::wstring> affectedPaths;
    double workUnitsPerSecond = 0.0;
    double etaSeconds = -1.0;
};

struct FileOperationRequest {
    FileOperationKind kind;
    std::vector<std::wstring> sources;
    std::wstring destination;
    std::wstring newName;
    bool recycle = true;
};

class FileOperations {
public:
    FileOperations() = default;
    ~FileOperations();

    bool Start(HWND eventWindow);
    void Stop();
    void Enqueue(FileOperationRequest request);
    void CancelCurrent();

    // Used by the worker-owned progress sink to return plain-data events to
    // the UI message loop.
    void PostEvent(FileOperationEvent event) const;

private:
    void WorkerMain();
    void Execute(const FileOperationRequest& request);
    HWND eventWindow_ = nullptr;
    std::atomic<DWORD> workerThreadId_{0};
    std::thread worker_;
    std::mutex queueMutex_;
    std::deque<FileOperationRequest> queue_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> cancelRequested_{false};
};

} // namespace ffui
