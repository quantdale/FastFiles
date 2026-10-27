#include "FileOperations.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <memory>
#include <iterator>
#include <shobjidl.h>
#include <shlobj_core.h>
#include <shellapi.h>
#include <sherrors.h>
#include <wrl/client.h>

namespace ffui {

namespace {

constexpr UINT WM_APP_FILE_OPERATION_REQUEST = WM_APP + 41;
constexpr auto kProgressPostInterval = std::chrono::milliseconds(34);

class ProgressSink final : public IFileOperationProgressSink {
public:
    ProgressSink(const FileOperations& owner, FileOperationKind operation, std::atomic<bool>& cancelled)
        : owner_(owner), operation_(operation), cancelled_(cancelled) {}

    IFACEMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IFileOperationProgressSink) {
            *object = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG references = --references_;
        if (references == 0) delete this;
        return references;
    }

    IFACEMETHODIMP StartOperations() override { return S_OK; }
    IFACEMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem* item, LPCWSTR) override { return PreItem(item); }
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem* item, LPCWSTR, HRESULT result, IShellItem* created) override { return PostItem(item, result, created); }
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR) override { return PreItem(item); }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT result, IShellItem* created) override { return PostItem(item, result, created); }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR) override { return PreItem(item); }
    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT result, IShellItem* created) override { return PostItem(item, result, created); }
    IFACEMETHODIMP PreDeleteItem(DWORD, IShellItem* item) override { return PreItem(item); }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem* item, HRESULT result, IShellItem* recycled) override {
        const HRESULT callbackResult = PostItem(item, result, nullptr);
        if (SUCCEEDED(result) && recycled != nullptr && !completedPaths_.empty()) {
            PIDLIST_ABSOLUTE identifier = nullptr;
            if (SUCCEEDED(SHGetIDListFromObject(recycled, &identifier)) && identifier != nullptr) {
                const size_t byteCount = ILGetSize(identifier);
                auto* bytes = reinterpret_cast<const std::byte*>(identifier);
                completedPaths_.back().shellItemId.assign(bytes, bytes + byteCount);
                CoTaskMemFree(identifier);
            }
        }
        return callbackResult;
    }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return CancelledResult(); }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR newName, LPCWSTR, DWORD, HRESULT result, IShellItem* created) override {
        if (FAILED(result)) failures_.push_back({newName ? newName : L"", result});
        else {
            ++completed_;
            completedPaths_.push_back({{}, ItemPath(created), {}});
        }
        return CancelledResult();
    }
    IFACEMETHODIMP UpdateProgress(UINT total, UINT soFar) override {
        const auto now = std::chrono::steady_clock::now();
        samples_.push_back({now, soFar});
        while (samples_.size() > 2 && now - samples_.front().first > std::chrono::seconds(4)) samples_.pop_front();

        FileOperationEvent event{FileOperationEventKind::Progress, operation_, currentItem_, completed_, total,
                                 total == 0 ? 0u : static_cast<unsigned int>((100ull * soFar) / total), {}};
        if (samples_.size() >= 2 && now - samples_.front().first >= std::chrono::seconds(1)) {
            const double elapsed = std::chrono::duration<double>(now - samples_.front().first).count();
            const double advanced = static_cast<double>(soFar - samples_.front().second);
            if (advanced > 0.0 && elapsed > 0.0) {
                event.workUnitsPerSecond = advanced / elapsed;
                event.etaSeconds = static_cast<double>(total - soFar) / event.workUnitsPerSecond;
            }
        }
        owner_.PostEvent(std::move(event));
        return CancelledResult();
    }
    IFACEMETHODIMP ResetTimer() override { return S_OK; }
    IFACEMETHODIMP PauseTimer() override { return S_OK; }
    IFACEMETHODIMP ResumeTimer() override { return S_OK; }

    std::vector<FileOperationFailure> Failures() const { return failures_; }
    unsigned int Completed() const { return completed_; }
    std::vector<ReversiblePath> CompletedPaths() const { return completedPaths_; }

private:
    HRESULT PreItem(IShellItem* item) {
        PWSTR name = nullptr;
        if (item != nullptr && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
            currentItem_ = name;
            CoTaskMemFree(name);
        }
        owner_.PostEvent({FileOperationEventKind::Progress, operation_, currentItem_, completed_, 0, 0, {}});
        return CancelledResult();
    }
    static std::wstring ItemPath(IShellItem* item) {
        PWSTR path = nullptr;
        if (item == nullptr || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
        std::wstring value(path);
        CoTaskMemFree(path);
        return value;
    }
    HRESULT PostItem(IShellItem* item, HRESULT result, IShellItem* created) {
        if (FAILED(result)) {
            failures_.push_back({ItemPath(item), result});
        } else {
            ++completed_;
            completedPaths_.push_back({ItemPath(item), ItemPath(created), {}});
        }
        return PostResult(result);
    }
    HRESULT PostResult(HRESULT) { return CancelledResult(); }
    HRESULT CancelledResult() const { return cancelled_ ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK; }

    std::atomic<ULONG> references_{1};
    const FileOperations& owner_;
    FileOperationKind operation_;
    std::atomic<bool>& cancelled_;
    std::wstring currentItem_;
    std::vector<FileOperationFailure> failures_;
    std::vector<ReversiblePath> completedPaths_;
    unsigned int completed_ = 0;
    std::deque<std::pair<std::chrono::steady_clock::time_point, UINT>> samples_;
};

Microsoft::WRL::ComPtr<IShellItem> MakeItem(const std::wstring& path) {
    Microsoft::WRL::ComPtr<IShellItem> item;
    SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
    return item;
}

Microsoft::WRL::ComPtr<IShellItem> MakeItem(const std::vector<std::byte>& identifier) {
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (!identifier.empty()) {
        SHCreateItemFromIDList(reinterpret_cast<PCIDLIST_ABSOLUTE>(identifier.data()), IID_PPV_ARGS(&item));
    }
    return item;
}

std::wstring ParentPath(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator + 1);
}

std::wstring NormalizedDirectory(const std::wstring& path) {
    std::wstring value = std::filesystem::path(path).lexically_normal().wstring();
    while (value.size() > 1 && (value.back() == L'\\' || value.back() == L'/')) value.pop_back();
    return value;
}

bool OrdinalEquals(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
                                right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

// A transfer whose source already lives in the destination folder, or whose
// source folder contains the destination, would copy a folder onto itself or
// into its own subtree. The shell answers such requests with a misleading
// conflict prompt whose Replace can destroy the only copy, so callers must
// reject them up front.
bool IsSelfTargetTransfer(const std::wstring& source, const std::wstring& destinationFolder) {
    if (source.empty() || destinationFolder.empty()) return false;
    const std::wstring sourceParent = NormalizedDirectory(ParentPath(source));
    const std::wstring destination = NormalizedDirectory(destinationFolder);
    if (OrdinalEquals(sourceParent, destination)) return true;
    const std::wstring sourcePrefix = NormalizedDirectory(source) + L"\\";
    if (destination.size() <= sourcePrefix.size()) return false;
    return OrdinalEquals(destination.substr(0, sourcePrefix.size()), sourcePrefix);
}

// Win32 file APIs without the extended-length prefix silently fail past
// MAX_PATH; shell items keep their unprefixed form.
std::wstring ExtendedLengthPath(const std::wstring& path) {
    if (path.size() >= 4 && path.compare(0, 4, L"\\\\?\\") == 0) return path;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return L"\\\\?\\UNC\\" + path.substr(2);
    if (path.size() >= 2 && std::iswalpha(path[0]) != 0 && path[1] == L':') return L"\\\\?\\" + path;
    return path;
}

bool PathExistsWin32(const std::wstring& path) {
    // GetFileAttributesW honours the \\?\ extended-length prefix;
    // GetFileAttributesExW does not.
    return GetFileAttributesW(ExtendedLengthPath(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

FileOperations::~FileOperations() { Stop(); }

bool FileOperations::Start(HWND eventWindow) {
    if (worker_.joinable() || eventWindow == nullptr) return false;
    eventWindow_ = eventWindow;
    stopping_ = false;
    // A previous Stop() latched the cancel flag; a fresh worker must not
    // inherit it or every operation would complete as instantly cancelled.
    cancelRequested_ = false;
    worker_ = std::thread(&FileOperations::WorkerMain, this);
    return true;
}

void FileOperations::Stop() {
    cancelRequested_ = true;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (auto& queued : queue_) queued.cancelledBeforeStart = true;
    }
    stopping_ = true;
    if (eventWindow_ != nullptr) {
        MSG pending{};
        while (PeekMessageW(&pending, eventWindow_, WM_APP_FILE_OPERATION_CONFLICT,
                            WM_APP_FILE_OPERATION_CONFLICT, PM_REMOVE)) {
            // The posted heap shared_ptr owns the message's reference; release
            // it (and answer Cancel) so a later dispatch cannot see it.
            std::unique_ptr<FileOperationConflictQuestionRef> posted(
                reinterpret_cast<FileOperationConflictQuestionRef*>(pending.lParam));
            if (posted && *posted) {
                {
                    std::lock_guard lock((*posted)->mutex);
                    (*posted)->decision = {};
                    (*posted)->answered = true;
                }
                (*posted)->answeredCondition.notify_one();
            }
        }
    }
    if (workerThreadId_ != 0) PostThreadMessageW(workerThreadId_, WM_QUIT, 0, 0);
    if (worker_.joinable()) worker_.join();
    workerThreadId_ = 0;
}

void FileOperations::Enqueue(FileOperationRequest request) {
    const FileOperationKind operation = request.kind;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.push_back(std::move(request));
    }
    PostEvent({FileOperationEventKind::Queued, operation, {}, 0, 0, 0, {}});
    if (workerThreadId_ != 0) PostThreadMessageW(workerThreadId_, WM_APP_FILE_OPERATION_REQUEST, 0, 0);
}

bool FileOperations::HasOutstandingWork() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return executing_ || !queue_.empty();
}

void FileOperations::CancelCurrent() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    // An idle Escape (no running or queued work) must not latch into the next
    // enqueue: only work outstanding when the cancel is observed is marked
    // (close-file-ops-conflict-lifetime-and-outcome-truth D4).
    if (!executing_ && queue_.empty()) return;
    cancelRequested_ = true;
    for (auto& queued : queue_) queued.cancelledBeforeStart = true;
}

void FileOperations::PostEvent(FileOperationEvent event) const {
    if (eventWindow_ == nullptr) return;
    // original-vision-journey-recovery 4.6: thousands of per-item progress
    // messages can fill the window queue and lose the terminal result. Bound
    // presentation traffic; every sink callback still checks cancellation.
    if (event.kind == FileOperationEventKind::Started) {
        std::lock_guard lock(progressMutex_);
        progressPosted_ = false;
        quantifiedProgressPosted_ = false;
    } else if (event.kind == FileOperationEventKind::Progress) {
        std::lock_guard lock(progressMutex_);
        const auto now = std::chrono::steady_clock::now();
        const bool firstQuantified = event.total > 0 && !quantifiedProgressPosted_;
        if (progressPosted_ && !firstQuantified && now - lastProgressPost_ < kProgressPostInterval) return;
        lastProgressPost_ = now;
        progressPosted_ = true;
        quantifiedProgressPosted_ = quantifiedProgressPosted_ || event.total > 0;
    }
    auto owned = std::make_unique<FileOperationEvent>(std::move(event));
    if (PostMessageW(eventWindow_, WM_APP_FILE_OPERATION_EVENT, 0, reinterpret_cast<LPARAM>(owned.get()))) owned.release();
}

void FileOperations::WorkerMain() {
    const HRESULT coInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coInitResult)) {
        // Without COM no request can ever run; fail everything still queued
        // with a clear error instead of leaving items stuck as Queued.
        for (;;) {
            FileOperationRequest request;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (queue_.empty()) break;
                request = std::move(queue_.front());
                queue_.pop_front();
            }
            std::vector<FileOperationFailure> failures;
            if (request.sources.empty()) failures.push_back({request.destination, coInitResult});
            else for (const auto& source : request.sources) failures.push_back({source, coInitResult});
            PostEvent({FileOperationEventKind::Completed, request.kind, {}, 0,
                       static_cast<unsigned int>(request.sources.size()), 0, std::move(failures)});
        }
        return;
    }
    workerThreadId_ = GetCurrentThreadId();
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    PostThreadMessageW(workerThreadId_, WM_APP_FILE_OPERATION_REQUEST, 0, 0);
    while (GetMessageW(&message, nullptr, 0, 0) > 0 && !stopping_) {
        if (message.message != WM_APP_FILE_OPERATION_REQUEST) continue;
        for (;;) {
            FileOperationRequest request;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (queue_.empty()) {
                    cancelRequested_ = false;
                    break;
                }
                request = std::move(queue_.front());
                queue_.pop_front();
                executing_ = true;
            }
            Execute(request, request.cancelledBeforeStart);
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                executing_ = false;
            }
            // The terminal event may already have reached the UI before the
            // worker became idle. Refresh availability after that transition.
            if (eventWindow_ != nullptr) PostMessageW(eventWindow_, WM_APP_FILE_OPERATION_AVAILABILITY, 0, 0);
            // The batch that just finished consumed any cancel request observed
            // while it ran; siblings queued at that moment carry their own
            // cancelledBeforeStart bit. A request enqueued after the cancel is
            // not born cancelled (design D4). Shutdown keeps the flag set so
            // remaining queued work still aborts.
            if (!stopping_) cancelRequested_ = false;
        }
    }
    CoUninitialize();
}

ConflictDecision FileOperations::RequestConflictDecision(const std::wstring& source, const std::wstring& destination) {
    auto question = std::make_shared<FileOperationConflictQuestion>();
    question->source = source;
    question->destination = destination;
    // The UI handler owns the posted reference until the decision is
    // published; this local shared_ptr keeps the question alive while the
    // worker waits, so Stop()/WM_DESTROY inside the conflict dialog's nested
    // message pump cannot free it under the UI thread's write (design D1).
    auto posted = std::make_unique<FileOperationConflictQuestionRef>(question);
    if (eventWindow_ == nullptr || !PostMessageW(eventWindow_, WM_APP_FILE_OPERATION_CONFLICT, 0,
                                                  reinterpret_cast<LPARAM>(posted.get()))) {
        return {};
    }
    posted.release();
    std::unique_lock lock(question->mutex);
    while (!question->answered && !stopping_) {
        question->answeredCondition.wait_for(lock, std::chrono::milliseconds(100));
    }
    return question->answered ? question->decision : ConflictDecision{};
}

void FileOperations::Execute(const FileOperationRequest& input, bool cancelBeforeStart) {
    // A cancel observed while this request was still queued stops it before
    // any shell work starts; the user sees it reported as cancelled rather
    // than silently dropped (close-file-ops-conflict-lifetime-and-outcome-truth D4).
    if (cancelBeforeStart) {
        PostEvent({FileOperationEventKind::Cancelled, input.kind, {}, 0,
                   static_cast<unsigned int>((std::max)(size_t{1}, input.sources.size())), 0, {}});
        return;
    }
    FileOperationRequest request = input;
    std::vector<FileOperationFailure> rejectedSources;
    std::vector<RestorePlanItem> restorePlan;
    unsigned int skipped = 0;
    if (request.kind == FileOperationKind::Copy || request.kind == FileOperationKind::Move) {
        // Self-target rejection covers every caller (drop, paste, undo):
        // copying a folder onto itself or into its own subtree must never
        // reach the shell, whose Replace answer can destroy the source.
        std::vector<std::wstring> validSources;
        for (const auto& source : request.sources) {
            if (IsSelfTargetTransfer(source, request.destination)) {
                rejectedSources.push_back({source, HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER)});
            } else {
                validSources.push_back(source);
            }
        }
        if (validSources.empty()) {
            PostEvent({FileOperationEventKind::Completed, request.kind, {}, 0,
                       static_cast<unsigned int>(rejectedSources.size()), 0, std::move(rejectedSources)});
            return;
        }
        request.sources = std::move(validSources);
    }
    // Copy/Move always go through conflict planning — including undo-shaped
    // requests that arrive with a pre-baked plan — so an occupied target is
    // never silently overwritten under FOF_NOCONFIRMATION.
    if (request.kind == FileOperationKind::Copy || request.kind == FileOperationKind::Move) {
        const size_t plannedSourceCount = request.sources.size();
        const auto plan = BuildTransferPlan(request.sources, request.destination,
            [](const std::wstring& path) { return PathExistsWin32(path); },
            [this](const std::wstring& source, const std::wstring& destination) {
                return RequestConflictDecision(source, destination);
            });
        if (!plan) {
            PostEvent({FileOperationEventKind::Cancelled, request.kind, {}, 0,
                       static_cast<unsigned int>((std::max)(size_t{1}, plannedSourceCount)), 0, {}});
            return;
        }
        request.transferPlan = *plan;
        request.sources.clear();
        for (const auto& item : request.transferPlan) request.sources.push_back(item.source);
        // Every source that received no plan item was skipped at the conflict
        // prompt. A skip is a user decision, not a failure, and must still be
        // visible in the batch outcome (design D3).
        skipped = static_cast<unsigned int>(plannedSourceCount - request.sources.size());
        if (request.sources.empty()) {
            FileOperationEvent completion{FileOperationEventKind::Completed, request.kind, {}, 0,
                                          static_cast<unsigned int>(plannedSourceCount), 100, {}};
            completion.skipped = skipped;
            PostEvent(std::move(completion));
            return;
        }
    }
    if (request.kind == FileOperationKind::Restore) {
        // original-vision-journey-recovery 4.11: the checkbox applies to this
        // entire restore request, including distinct original folders. Finish
        // preflight before submitting any Shell item; later Cancel mutates none.
        const auto plan = BuildRestorePlan(request.restorePaths,
            [](const std::wstring& path) { return PathExistsWin32(path); },
            [this](const std::wstring& source, const std::wstring& destination) {
                return RequestConflictDecision(source, destination);
            });
        if (!plan) {
            PostEvent({FileOperationEventKind::Cancelled, request.kind, {}, 0,
                       static_cast<unsigned int>((std::max)(size_t{1}, request.restorePaths.size())), 0, {}});
            return;
        }
        restorePlan = *plan;
        skipped = static_cast<unsigned int>(request.restorePaths.size() - restorePlan.size());
        request.sources.clear();
        for (const auto& item : restorePlan) request.sources.push_back(request.restorePaths[item.restoreIndex].originalPath);
        if (restorePlan.empty()) {
            FileOperationEvent completion{FileOperationEventKind::Completed, request.kind, {}, 0,
                static_cast<unsigned int>(request.restorePaths.size()), 100, {}};
            completion.skipped = skipped;
            PostEvent(std::move(completion));
            return;
        }
    }
    PostEvent({FileOperationEventKind::Started, request.kind, {}, 0, static_cast<unsigned int>(request.sources.size()), 0, {}});
    if (request.kind == FileOperationKind::Rename) {
        if (request.sources.size() != 1 || !IsValidFileName(request.newName)) {
            PostEvent({FileOperationEventKind::Completed, request.kind, {}, 0, 1, 0,
                       {{request.newName, HRESULT_FROM_WIN32(ERROR_INVALID_NAME)}}});
            return;
        }
        const std::wstring sibling = ParentPath(request.sources.front()) + request.newName;
        const DWORD attributes = GetFileAttributesW(ExtendedLengthPath(sibling).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && _wcsicmp(sibling.c_str(), request.sources.front().c_str()) != 0) {
            PostEvent({FileOperationEventKind::Completed, request.kind, {}, 0, 1, 0,
                       {{sibling, HRESULT_FROM_WIN32(ERROR_FILE_EXISTS)}}});
            return;
        }
    }
    if (request.kind == FileOperationKind::Link) {
        std::vector<FileOperationFailure> failures;
        unsigned int completed = 0;
        std::vector<std::wstring> affected;
        for (size_t index = 0; index < request.sources.size(); ++index) {
            Microsoft::WRL::ComPtr<IShellLinkW> link;
            HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
            std::wstring name = std::filesystem::path(request.sources[index]).filename().wstring() + L".lnk";
            if (index < request.transferPlan.size() && !request.transferPlan[index].destinationName.empty()) {
                name = request.transferPlan[index].destinationName;
            }
            const auto output = std::filesystem::path(request.destination) / name;
            if (SUCCEEDED(result)) result = link->SetPath(request.sources[index].c_str());
            Microsoft::WRL::ComPtr<IPersistFile> persistence;
            if (SUCCEEDED(result)) result = link.As(&persistence);
            if (SUCCEEDED(result)) result = persistence->Save(output.c_str(), TRUE);
            if (FAILED(result)) failures.push_back({request.sources[index], result});
            else {
                ++completed;
                affected.push_back(output.wstring());
            }
        }
        const unsigned int total = static_cast<unsigned int>(request.sources.size());
        PostEvent({FileOperationEventKind::Completed, request.kind, {}, completed, total,
                   total == 0 ? 0u : static_cast<unsigned int>((100ull * completed) / total),
                   std::move(failures), std::move(affected)});
        return;
    }
    Microsoft::WRL::ComPtr<IFileOperation> operation;
    HRESULT setupResult = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&operation));
    if (SUCCEEDED(setupResult)) {
        // original-vision-journey-recovery 4.7: the shell's separate progress
        // window can steal focus from our live cancellation palette. Progress
        // and cancellation remain on ProgressSink and the native shell UI.
        // FOF_SILENT suppresses only the duplicate Shell progress dialog.
        setupResult = operation->SetOperationFlags(FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI |
            (request.kind == FileOperationKind::Delete && request.recycle ? FOF_ALLOWUNDO : 0));
    }
    if (FAILED(setupResult)) {
        const unsigned int total = static_cast<unsigned int>((std::max)(size_t{1}, request.sources.size() + skipped));
        std::vector<FileOperationFailure> failures;
        if (request.sources.empty()) failures.push_back({request.destination, setupResult});
        else for (const auto& source : request.sources) failures.push_back({source, setupResult});
        FileOperationEvent completion{FileOperationEventKind::Completed, request.kind, {}, 0, total, 0,
                                      std::move(failures), request.sources};
        completion.skipped = skipped;
        PostEvent(std::move(completion));
        return;
    }
    auto* sink = new ProgressSink(*this, request.kind, cancelRequested_);
    DWORD cookie = 0;
    const HRESULT adviseResult = operation->Advise(sink, &cookie);
    HRESULT submitResult = adviseResult;
    std::vector<FileOperationFailure> submissionFailures;
    Microsoft::WRL::ComPtr<IShellItem> destination = MakeItem(request.destination);
    if (SUCCEEDED(submitResult) && !destination &&
        (request.kind == FileOperationKind::Copy || request.kind == FileOperationKind::Move ||
         request.kind == FileOperationKind::CreateFolder || request.kind == FileOperationKind::CreateFile)) {
        // An unparsable destination (e.g. an unsupported long path) must not
        // reach the shell with a null target item.
        submitResult = HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
        if (request.sources.empty()) submissionFailures.push_back({request.destination, submitResult});
        else for (const auto& source : request.sources) submissionFailures.push_back({source, submitResult});
    }
    if (SUCCEEDED(submitResult) && (request.kind == FileOperationKind::Copy || request.kind == FileOperationKind::Move || request.kind == FileOperationKind::Delete)) {
        for (size_t sourceIndex = 0; sourceIndex < request.sources.size(); ++sourceIndex) {
            const auto& sourcePath = request.sources[sourceIndex];
            auto source = MakeItem(sourcePath);
            if (!source) {
                submissionFailures.push_back({sourcePath, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)});
                continue;
            }
            HRESULT itemResult = S_OK;
            const wchar_t* destinationName = nullptr;
            if (sourceIndex < request.transferPlan.size() && !request.transferPlan[sourceIndex].destinationName.empty()) {
                destinationName = request.transferPlan[sourceIndex].destinationName.c_str();
            }
            if (request.kind == FileOperationKind::Copy) itemResult = operation->CopyItem(source.Get(), destination.Get(), destinationName, nullptr);
            else if (request.kind == FileOperationKind::Move) itemResult = operation->MoveItem(source.Get(), destination.Get(), destinationName, nullptr);
            else itemResult = operation->DeleteItem(source.Get(), nullptr);
            if (FAILED(itemResult)) submissionFailures.push_back({sourcePath, itemResult});
        }
    } else if (SUCCEEDED(submitResult) && request.kind == FileOperationKind::Rename) {
        for (const auto& sourcePath : request.sources) {
            auto source = MakeItem(sourcePath);
            if (!source) { submitResult = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND); continue; }
            submitResult = operation->RenameItem(source.Get(), request.newName.c_str(), nullptr);
            if (FAILED(submitResult)) break;
        }
    } else if (SUCCEEDED(submitResult) && request.kind == FileOperationKind::CreateFolder) {
        submitResult = operation->NewItem(destination.Get(), FILE_ATTRIBUTE_DIRECTORY, request.newName.c_str(), nullptr, nullptr);
    } else if (SUCCEEDED(submitResult) && request.kind == FileOperationKind::CreateFile) {
        submitResult = operation->NewItem(destination.Get(), FILE_ATTRIBUTE_NORMAL, request.newName.c_str(), nullptr, nullptr);
    } else if (SUCCEEDED(submitResult) && request.kind == FileOperationKind::Restore) {
        for (const auto& planned : restorePlan) {
            const auto& restore = request.restorePaths[planned.restoreIndex];
            auto recycled = MakeItem(restore.shellItemId);
            const std::wstring originalName = std::filesystem::path(restore.originalPath).filename().wstring();
            auto restoreFolder = MakeItem(std::filesystem::path(restore.originalPath).parent_path().wstring());
            if (!recycled || !restoreFolder) {
                submissionFailures.push_back({restore.originalPath, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)});
                continue;
            }
            const std::wstring& name = planned.destinationName.empty() ? originalName : planned.destinationName;
            const HRESULT itemResult = operation->MoveItem(recycled.Get(), restoreFolder.Get(), name.c_str(), nullptr);
            if (FAILED(itemResult)) submissionFailures.push_back({restore.originalPath, itemResult});
        }
    }
    if (SUCCEEDED(submitResult)) submitResult = operation->PerformOperations();
    if (cookie != 0) operation->Unadvise(cookie);
    BOOL operationsAborted = FALSE;
    operation->GetAnyOperationsAborted(&operationsAborted);
    const bool cancelled = cancelRequested_ || operationsAborted || submitResult == HRESULT_FROM_WIN32(ERROR_CANCELLED);
    auto failures = sink->Failures();
    failures.insert(failures.end(), submissionFailures.begin(), submissionFailures.end());
    if (FAILED(submitResult) && !cancelled && failures.empty()) {
        if (request.sources.empty()) {
            failures.push_back({request.destination, submitResult});
        } else {
            for (const auto& source : request.sources) failures.push_back({source, submitResult});
        }
    }
    failures.insert(failures.end(), rejectedSources.begin(), rejectedSources.end());
    const unsigned int total = static_cast<unsigned int>((std::max)(size_t{1}, request.sources.size() + rejectedSources.size() + skipped));
    const unsigned int completed = sink->Completed();
    // Live progress is byte-based; keep the terminal event consistent with
    // it (full success = 100%) instead of recomputing from item counts.
    const unsigned int percent =
        (!cancelled && failures.empty() && completed >= request.sources.size())
            ? 100u
            : static_cast<unsigned int>((100ull * completed) / total);
    std::vector<std::wstring> affected = request.sources;
    if (!request.destination.empty()) affected.push_back(request.destination);
    FileOperationEvent completion{cancelled ? FileOperationEventKind::Cancelled : FileOperationEventKind::Completed,
                                  request.kind, {}, completed, total, percent, std::move(failures), std::move(affected)};
    completion.skipped = skipped;
    const auto allCompletedPaths = sink->CompletedPaths();
    if (request.kind == FileOperationKind::CreateFolder || request.kind == FileOperationKind::CreateFile) {
        for (const auto& path : allCompletedPaths) {
            if (!path.resultingPath.empty()) completion.createdPaths.push_back(path.resultingPath);
        }
        // file-operations 2.4: some shell providers report a successful
        // NewItem without returning the created IShellItem. The filesystem
        // path is still deterministic; publish it only after verifying that
        // the requested item exists so inline rename never loses the new row.
        if (completion.failures.empty()) {
            const auto expectedPath = (std::filesystem::path(request.destination) / request.newName).wstring();
            if (GetFileAttributesW(ExtendedLengthPath(expectedPath).c_str()) != INVALID_FILE_ATTRIBUTES) {
                completion.createdPaths.clear();
                completion.createdPaths.push_back(expectedPath);
            }
        }
    }
    if (request.recordHistory) {
        auto completedPaths = allCompletedPaths;
        if (request.kind == FileOperationKind::Rename && completedPaths.size() == 1) {
            completion.reversibleOperation = ReversibleOperation{ReversibleOperationKind::Rename, std::move(completedPaths)};
        } else if (request.kind == FileOperationKind::Move) {
            // file-operations 6.6: a Replace destroys the previous
            // destination contents, so the entire move batch is ineligible
            // for undo even if Shell returns a callback path with different
            // canonicalization from the preflight source path.
            const bool overwroteExisting = std::any_of(
                request.transferPlan.begin(), request.transferPlan.end(),
                [](const TransferPlanItem& item) { return item.replacesExisting; });
            if (overwroteExisting) {
                completedPaths.clear();
            }
            std::erase_if(completedPaths, [&](const ReversiblePath& path) {
                const auto found = std::find_if(request.transferPlan.begin(), request.transferPlan.end(), [&](const TransferPlanItem& item) {
                    return _wcsicmp(item.source.c_str(), path.originalPath.c_str()) == 0;
                });
                return found != request.transferPlan.end() && found->replacesExisting;
            });
            if (!completedPaths.empty()) completion.reversibleOperation = ReversibleOperation{ReversibleOperationKind::Move, std::move(completedPaths)};
        } else if (request.kind == FileOperationKind::Delete && request.recycle) {
            std::erase_if(completedPaths, [](const ReversiblePath& path) { return path.shellItemId.empty(); });
            if (!completedPaths.empty()) completion.reversibleOperation = ReversibleOperation{ReversibleOperationKind::RecycleDelete, std::move(completedPaths)};
        }
    }
    PostEvent(std::move(completion));
    sink->Release();
}

std::wstring DescribeFileOperationFailure(HRESULT error) {
    std::wstring description;
    switch (error) {
        case COPYENGINE_E_SHARING_VIOLATION_SRC:
            description = L"The source file is in use. Close the app using it, then try the operation again.";
            break;
        case COPYENGINE_E_SHARING_VIOLATION_DEST:
            description = L"The destination file is in use. Close the app using it, then try the operation again.";
            break;
        case HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION):
        case HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION):
            description = L"A file is in use. Close the app using it, then try the operation again.";
            break;
        case E_ACCESSDENIED:
            description = L"Access was denied. Check the file's permissions and read-only setting, or choose another destination.";
            break;
        case HRESULT_FROM_WIN32(ERROR_DISK_FULL):
        case HRESULT_FROM_WIN32(ERROR_HANDLE_DISK_FULL):
            description = L"There is not enough free space. Free space on the destination drive or choose another destination.";
            break;
        case HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND):
        case HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND):
            description = L"A file or folder is no longer available. Check its location and connected drives, then try again.";
            break;
        default: {
            wchar_t message[1024]{};
            const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, static_cast<DWORD>(error), 0, message, static_cast<DWORD>(std::size(message)), nullptr);
            if (length > 0) {
                description.assign(message, length);
                while (!description.empty() && std::iswspace(description.back())) description.pop_back();
            }
            if (description.empty()) description = L"Windows could not complete this file operation.";
            break;
        }
    }
    wchar_t code[16]{};
    swprintf_s(code, L"0x%08X", static_cast<unsigned int>(error));
    return description + L" (" + code + L")";
}

void RetainLastOperationFailures(std::vector<FileOperationFailure>& retained, const FileOperationEvent& event) {
    if (event.kind != FileOperationEventKind::Completed && event.kind != FileOperationEventKind::Cancelled) {
        return;
    }
    // Terminal events are authoritative (including an empty vector, which
    // clears a previous batch's list); Progress ticks never carry failures and
    // must not wipe what a terminal event recorded (design D2).
    retained = event.failures;
}

} // namespace ffui
