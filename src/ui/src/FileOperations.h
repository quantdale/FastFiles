#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <optional>
#include <condition_variable>
#include <windows.h>

#include "FileOperationPolicy.h"

namespace ffui {

// Posted to the UI HWND.  The receiver owns and deletes the event; it never
// contains a COM interface or any other apartment-bound value.
constexpr UINT WM_APP_FILE_OPERATION_EVENT = WM_APP + 2;
constexpr UINT WM_APP_FILE_OPERATION_CONFLICT = WM_APP + 8;

struct FileOperationConflictQuestion {
    std::wstring source;
    std::wstring destination;
    std::mutex mutex;
    std::condition_variable answeredCondition;
    ConflictDecision decision;
    bool answered = false;
};

enum class FileOperationKind { Copy, Move, Rename, CreateFolder, CreateFile, Delete, Restore, Link };
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
    std::optional<ReversibleOperation> reversibleOperation;
    std::vector<std::wstring> createdPaths;
};

struct FileOperationRequest {
    FileOperationKind kind;
    std::vector<std::wstring> sources;
    std::wstring destination;
    std::wstring newName;
    bool recycle = true;
    std::vector<TransferPlanItem> transferPlan;
    std::vector<ReversiblePath> restorePaths;
    bool recordHistory = true;
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
    ConflictDecision RequestConflictDecision(const std::wstring& source, const std::wstring& destination);
    HWND eventWindow_ = nullptr;
    std::atomic<DWORD> workerThreadId_{0};
    std::thread worker_;
    std::mutex queueMutex_;
    std::deque<FileOperationRequest> queue_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> cancelRequested_{false};
};

} // namespace ffui
