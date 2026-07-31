#include "ffsetup/ScheduledTaskRegistration.h"

#include <windows.h>
#include <comdef.h>
#include <taskschd.h>
#include <wrl/client.h>

#include "ffsetup/Identifiers.h"

// taskschd/comsuppw are linked via CMakeLists.txt (target_link_libraries)
// rather than #pragma comment here, consistent with how wintrust/crypt32
// are declared for AuthenticodeVerification.cpp.

using Microsoft::WRL::ComPtr;

namespace ffsetup {

namespace {

// RAII guard for CoInitializeEx: registration may be called either from an
// already-COM-initialized process (the engine's own lazy-start path) or a
// fresh installer process, so this only tears down COM if this call is the
// one that initialized it.
class ComScope {
public:
    ComScope() noexcept {
        initHr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ownsInit_ = SUCCEEDED(initHr_);
        // RPC_E_CHANGED_MODE means COM is already initialized on this
        // thread with a different concurrency model -- treat as already-
        // initialized-by-someone-else, not an error we own.
        valid_ = SUCCEEDED(initHr_) || initHr_ == RPC_E_CHANGED_MODE || initHr_ == S_FALSE;
    }
    ~ComScope() {
        if (ownsInit_) {
            CoUninitialize();
        }
    }
    bool Valid() const noexcept { return valid_; }
    // The real CoInitializeEx failure code -- GetLastError() is a Win32
    // TLS value that a failing HRESULT never sets, so callers must use
    // this instead when Valid() is false.
    HRESULT InitHResult() const noexcept { return initHr_; }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT initHr_ = E_FAIL;
    bool ownsInit_ = false;
    bool valid_ = false;
};

SetupResult FromHResult(HRESULT hr) noexcept {
    if (SUCCEEDED(hr)) {
        return SetupResult::Ok();
    }
    return SetupResult::Failure(static_cast<DWORD>(hr));
}

_variant_t Empty() noexcept { return _variant_t(); }

SetupResult GetOrCreateFastFilesFolder(ComPtr<ITaskService>& service, ComPtr<ITaskFolder>& outFolder) noexcept {
    ComPtr<ITaskFolder> rootFolder;
    HRESULT hr = service->GetFolder(_bstr_t(L"\\"), &rootFolder);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }

    // Trim the trailing backslash from kEngineTaskFolder ("\FastFiles\")
    // for the folder name itself.
    _bstr_t folderName(L"FastFiles");
    ComPtr<ITaskFolder> targetFolder;
    hr = rootFolder->GetFolder(folderName, &targetFolder);
    if (SUCCEEDED(hr)) {
        outFolder = targetFolder;
        return SetupResult::Ok();
    }

    ComPtr<ITaskFolder> createdFolder;
    hr = rootFolder->CreateFolder(folderName, _variant_t(L""), &createdFolder);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        return FromHResult(hr);
    }
    if (SUCCEEDED(hr)) {
        outFolder = createdFolder;
    } else {
        hr = rootFolder->GetFolder(folderName, &outFolder);
        if (FAILED(hr)) {
            return FromHResult(hr);
        }
    }
    return SetupResult::Ok();
}

} // namespace

SetupResult RegisterEngineScheduledTask(const std::wstring& enginePath) noexcept {
    ComScope comScope;
    if (!comScope.Valid()) {
        return FromHResult(comScope.InitHResult());
    }

    ComPtr<ITaskService> service;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
    if (FAILED(hr)) {
        return FromHResult(hr);
    }

    hr = service->Connect(Empty(), Empty(), Empty(), Empty());
    if (FAILED(hr)) {
        return FromHResult(hr);
    }

    ComPtr<ITaskFolder> folder;
    SetupResult folderResult = GetOrCreateFastFilesFolder(service, folder);
    if (!folderResult.success) {
        return folderResult;
    }

    ComPtr<ITaskDefinition> taskDefinition;
    hr = service->NewTask(0, &taskDefinition);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }

    // Principal: least-privilege RunLevel, triggered for any member of the
    // built-in Users group at logon (task 4.1).
    ComPtr<IPrincipal> principal;
    hr = taskDefinition->get_Principal(&principal);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }
    principal->put_GroupId(_bstr_t(L"Users"));
    principal->put_LogonType(TASK_LOGON_GROUP);
    principal->put_RunLevel(TASK_RUNLEVEL_LUA);

    ComPtr<ITaskSettings> settings;
    hr = taskDefinition->get_Settings(&settings);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }
    settings->put_StartWhenAvailable(VARIANT_TRUE);
    settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    // Don't stop the engine on an arbitrary timeout -- it's meant to be
    // long-lived for the whole logon session.
    settings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
    settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);

    ComPtr<ITriggerCollection> triggers;
    hr = taskDefinition->get_Triggers(&triggers);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }
    ComPtr<ITrigger> trigger;
    hr = triggers->Create(TASK_TRIGGER_LOGON, &trigger);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }

    ComPtr<IActionCollection> actions;
    hr = taskDefinition->get_Actions(&actions);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }
    ComPtr<IAction> action;
    hr = actions->Create(TASK_ACTION_EXEC, &action);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }
    ComPtr<IExecAction> execAction;
    hr = action.As(&execAction);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }
    hr = execAction->put_Path(_bstr_t(enginePath.c_str()));
    if (FAILED(hr)) {
        return FromHResult(hr);
    }

    ComPtr<IRegisteredTask> registeredTask;
    hr = folder->RegisterTaskDefinition(
        _bstr_t(kEngineTaskName), taskDefinition.Get(), TASK_CREATE_OR_UPDATE,
        Empty(), Empty(), TASK_LOGON_GROUP, Empty(), &registeredTask);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }

    return SetupResult::Ok();
}

SetupResult UnregisterEngineScheduledTask() noexcept {
    ComScope comScope;
    if (!comScope.Valid()) {
        return FromHResult(comScope.InitHResult());
    }

    ComPtr<ITaskService> service;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
    if (FAILED(hr)) {
        return FromHResult(hr);
    }
    hr = service->Connect(Empty(), Empty(), Empty(), Empty());
    if (FAILED(hr)) {
        return FromHResult(hr);
    }

    ComPtr<ITaskFolder> rootFolder;
    hr = service->GetFolder(_bstr_t(L"\\"), &rootFolder);
    if (FAILED(hr)) {
        return FromHResult(hr);
    }

    ComPtr<ITaskFolder> folder;
    hr = rootFolder->GetFolder(_bstr_t(L"FastFiles"), &folder);
    if (FAILED(hr)) {
        // Folder already gone: nothing to uninstall.
        return SetupResult::Ok();
    }

    hr = folder->DeleteTask(_bstr_t(kEngineTaskName), 0);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        return FromHResult(hr);
    }

    rootFolder->DeleteFolder(_bstr_t(L"FastFiles"), 0);
    return SetupResult::Ok();
}

} // namespace ffsetup
