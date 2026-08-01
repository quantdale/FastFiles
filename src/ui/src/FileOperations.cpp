#include "FileOperations.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <deque>
#include <shobjidl.h>
#include <shlobj_core.h>
#include <shellapi.h>
#include <wrl/client.h>

namespace ffui {

namespace {

constexpr UINT WM_APP_FILE_OPERATION_REQUEST = WM_APP + 41;

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
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem* item, LPCWSTR, HRESULT result, IShellItem*) override { return PostItem(item, result); }
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR) override { return PreItem(item); }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT result, IShellItem*) override { return PostItem(item, result); }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR) override { return PreItem(item); }
    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT result, IShellItem*) override { return PostItem(item, result); }
    IFACEMETHODIMP PreDeleteItem(DWORD, IShellItem* item) override { return PreItem(item); }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem* item, HRESULT result, IShellItem*) override { return PostItem(item, result); }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return CancelledResult(); }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR newName, LPCWSTR, DWORD, HRESULT result, IShellItem*) override {
        if (FAILED(result)) failures_.push_back({newName ? newName : L"", result});
        else ++completed_;
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
    HRESULT PostItem(IShellItem* item, HRESULT result) {
        if (FAILED(result)) {
            PWSTR path = nullptr;
            if (item != nullptr && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                failures_.push_back({path, result});
                CoTaskMemFree(path);
            }
        } else {
            ++completed_;
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
    unsigned int completed_ = 0;
    std::deque<std::pair<std::chrono::steady_clock::time_point, UINT>> samples_;
};

Microsoft::WRL::ComPtr<IShellItem> MakeItem(const std::wstring& path) {
    Microsoft::WRL::ComPtr<IShellItem> item;
    SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
    return item;
}

bool IsValidLeafName(const std::wstring& name) {
    if (name.empty() || name.back() == L'.' || name.back() == L' ') return false;
    if (name.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos) return false;
    std::wstring stem = name.substr(0, name.find(L'.'));
    for (auto& character : stem) character = static_cast<wchar_t>(std::towupper(character));
    return stem != L"CON" && stem != L"PRN" && stem != L"AUX" && stem != L"NUL" &&
           !(stem.size() == 4 && stem.starts_with(L"COM") && stem[3] >= L'1' && stem[3] <= L'9') &&
           !(stem.size() == 4 && stem.starts_with(L"LPT") && stem[3] >= L'1' && stem[3] <= L'9');
}

std::wstring ParentPath(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator + 1);
}

} // namespace

FileOperations::~FileOperations() { Stop(); }

bool FileOperations::Start(HWND eventWindow) {
    if (worker_.joinable() || eventWindow == nullptr) return false;
    eventWindow_ = eventWindow;
    stopping_ = false;
    worker_ = std::thread(&FileOperations::WorkerMain, this);
    return true;
}

void FileOperations::Stop() {
    cancelRequested_ = true;
    stopping_ = true;
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

void FileOperations::CancelCurrent() { cancelRequested_ = true; }

void FileOperations::PostEvent(FileOperationEvent event) const {
    if (eventWindow_ != nullptr) PostMessageW(eventWindow_, WM_APP_FILE_OPERATION_EVENT, 0, reinterpret_cast<LPARAM>(new FileOperationEvent(std::move(event))));
}

void FileOperations::WorkerMain() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return;
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
                if (queue_.empty()) break;
                request = std::move(queue_.front());
                queue_.pop_front();
            }
            cancelRequested_ = false;
            Execute(request);
        }
    }
    CoUninitialize();
}

void FileOperations::Execute(const FileOperationRequest& request) {
    PostEvent({FileOperationEventKind::Started, request.kind, {}, 0, static_cast<unsigned int>(request.sources.size()), 0, {}});
    if (request.kind == FileOperationKind::Rename) {
        if (request.sources.size() != 1 || !IsValidLeafName(request.newName)) {
            PostEvent({FileOperationEventKind::Completed, request.kind, {}, 0, 1, 0,
                       {{request.newName, HRESULT_FROM_WIN32(ERROR_INVALID_NAME)}}});
            return;
        }
        const std::wstring sibling = ParentPath(request.sources.front()) + request.newName;
        const DWORD attributes = GetFileAttributesW(sibling.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && _wcsicmp(sibling.c_str(), request.sources.front().c_str()) != 0) {
            PostEvent({FileOperationEventKind::Completed, request.kind, {}, 0, 1, 0,
                       {{sibling, HRESULT_FROM_WIN32(ERROR_FILE_EXISTS)}}});
            return;
        }
    }
    Microsoft::WRL::ComPtr<IFileOperation> operation;
    const HRESULT createResult = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&operation));
    if (FAILED(createResult)) {
        const unsigned int total = static_cast<unsigned int>((std::max)(size_t{1}, request.sources.size()));
        std::vector<FileOperationFailure> failures;
        if (request.sources.empty()) failures.push_back({request.destination, createResult});
        else for (const auto& source : request.sources) failures.push_back({source, createResult});
        PostEvent({FileOperationEventKind::Completed, request.kind, {}, 0, total, 0, std::move(failures), request.sources});
        return;
    }
    operation->SetOperationFlags(FOF_NOCONFIRMATION | FOF_NOERRORUI | (request.kind == FileOperationKind::Delete && request.recycle ? FOF_ALLOWUNDO : 0));
    auto* sink = new ProgressSink(*this, request.kind, cancelRequested_);
    DWORD cookie = 0;
    const HRESULT adviseResult = operation->Advise(sink, &cookie);
    HRESULT submitResult = adviseResult;
    std::vector<FileOperationFailure> submissionFailures;
    Microsoft::WRL::ComPtr<IShellItem> destination = MakeItem(request.destination);
    if (SUCCEEDED(submitResult) && (request.kind == FileOperationKind::Copy || request.kind == FileOperationKind::Move || request.kind == FileOperationKind::Delete)) {
        for (const auto& sourcePath : request.sources) {
            auto source = MakeItem(sourcePath);
            if (!source) {
                submissionFailures.push_back({sourcePath, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)});
                continue;
            }
            HRESULT itemResult = S_OK;
            if (request.kind == FileOperationKind::Copy) itemResult = operation->CopyItem(source.Get(), destination.Get(), nullptr, nullptr);
            else if (request.kind == FileOperationKind::Move) itemResult = operation->MoveItem(source.Get(), destination.Get(), nullptr, nullptr);
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
    const unsigned int total = static_cast<unsigned int>((std::max)(size_t{1}, request.sources.size()));
    const unsigned int completed = sink->Completed();
    const unsigned int percent = total == 0 ? 0 : static_cast<unsigned int>((100ull * completed) / total);
    std::vector<std::wstring> affected = request.sources;
    if (!request.destination.empty()) affected.push_back(request.destination);
    PostEvent({cancelled ? FileOperationEventKind::Cancelled : FileOperationEventKind::Completed, request.kind, {}, completed,
               total, percent, std::move(failures), std::move(affected)});
    sink->Release();
}

} // namespace ffui
