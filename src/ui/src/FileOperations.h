#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
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

// Ownership protocol for WM_APP_FILE_OPERATION_CONFLICT: the worker posts a
// heap-allocated shared_ptr copy of the question as lParam; the receiving
// window procedure owns that heap pointer and releases it once the decision
// has been published. The worker keeps its own shared_ptr alive while it
// waits, so Stop()/WM_DESTROY running during the conflict dialog's nested
// message pump cannot free the question while the UI thread still writes to
// it (close-file-ops-conflict-lifetime-and-outcome-truth D1).
using FileOperationConflictQuestionRef = std::shared_ptr<FileOperationConflictQuestion>;

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
    // Items the user skipped at conflict resolution; counted separately from
    // completed transfers and per-item failures so an all-skip batch does not
    // report "0 of 0" (close-file-ops-conflict-lifetime-and-outcome-truth D3).
    unsigned int skipped = 0;
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
    // Set by CancelCurrent() on requests that were already queued when the
    // cancel was observed; such requests report Cancelled without starting
    // (close-file-ops-conflict-lifetime-and-outcome-truth D4).
    bool cancelledBeforeStart = false;
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
    void Execute(const FileOperationRequest& request, bool cancelBeforeStart);
    ConflictDecision RequestConflictDecision(const std::wstring& source, const std::wstring& destination);
    HWND eventWindow_ = nullptr;
    std::atomic<DWORD> workerThreadId_{0};
    std::thread worker_;
    std::mutex queueMutex_;
    std::deque<FileOperationRequest> queue_;
    // Guarded by queueMutex_: true while a dequeued request is executing.
    bool executing_ = false;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> cancelRequested_{false};
    // Presentation cadence only. Queued events from the UI must not reset
    // progress for the operation still executing on the worker.
    mutable std::mutex progressMutex_;
    mutable std::chrono::steady_clock::time_point lastProgressPost_{};
    mutable bool progressPosted_ = false;
    mutable bool quantifiedProgressPosted_ = false;
};

// Last-operation-errors retention contract: only terminal (Completed or
// Cancelled) events carry the authoritative per-item failure list; Progress
// ticks never clear a recorded list. WindowShell calls this for every event.
void RetainLastOperationFailures(std::vector<FileOperationFailure>& retained, const FileOperationEvent& event);

// Human-readable recovery guidance followed by the original diagnostic code.
// Presentation only: never changes failure classification or retries work.
std::wstring DescribeFileOperationFailure(HRESULT error);

} // namespace ffui
