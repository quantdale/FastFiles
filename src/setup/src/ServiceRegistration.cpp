#include "ffsetup/ServiceRegistration.h"

#include <ntsecapi.h>

#include <cstdio>

#include "ffsetup/Identifiers.h"
#include "ffsetup/SecurityDescriptors.h"

namespace ffsetup {

namespace {

// LSA_UNICODE_STRING wrapper for a literal privilege name; the buffer must
// outlive the LsaAddAccountRights call, which it does as a local.
LSA_UNICODE_STRING MakeLsaString(const wchar_t* text) noexcept {
    LSA_UNICODE_STRING str{};
    str.Buffer = const_cast<wchar_t*>(text);
    str.Length = static_cast<USHORT>(wcslen(text) * sizeof(wchar_t));
    str.MaximumLength = str.Length + sizeof(wchar_t);
    return str;
}

SetupResult GrantBackupPrivilegeToVirtualAccount() noexcept {
    auto accountSid = LookupAccountSid(kServiceVirtualAccountName);
    if (!accountSid) {
        return SetupResult::FromLastError();
    }

    LSA_OBJECT_ATTRIBUTES objectAttributes{};
    LSA_HANDLE policyHandle = nullptr;
    NTSTATUS status = LsaOpenPolicy(nullptr, &objectAttributes, POLICY_CREATE_ACCOUNT | POLICY_LOOKUP_NAMES, &policyHandle);
    if (status != 0) {
        return SetupResult::Failure(LsaNtStatusToWinError(status));
    }

    LSA_UNICODE_STRING privilege = MakeLsaString(L"SeBackupPrivilege");
    status = LsaAddAccountRights(policyHandle, accountSid->Get(), &privilege, 1);
    LsaClose(policyHandle);

    if (status != 0) {
        return SetupResult::Failure(LsaNtStatusToWinError(status));
    }
    return SetupResult::Ok();
}

// Restart on the first two failures with escalating delay, then stop
// retrying -- a capped action count so a persistently crashing service
// cannot restart-loop forever (task 3.10).
SetupResult ConfigureFailureActions(SC_HANDLE service) noexcept {
    SC_ACTION actions[3] = {
        {SC_ACTION_RESTART, 5000},   // first failure: retry after 5s
        {SC_ACTION_RESTART, 30000},  // second failure: retry after 30s
        {SC_ACTION_NONE, 0},         // third+ failure within the reset window: give up
    };

    SERVICE_FAILURE_ACTIONSW config{};
    config.dwResetPeriod = 24 * 60 * 60; // failure count resets after 1 day of stability
    config.cActions = 3;
    config.lpsaActions = actions;

    if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &config)) {
        std::fwprintf(stderr, L"FastFilesSetup: ChangeServiceConfig2 failure actions failed (error %lu)\n", GetLastError());
        return SetupResult::FromLastError();
    }

    // On Vista+, failure actions only fire on an unexpected process exit
    // unless this flag is set -- required so the self-directed staleness
    // recovery's deliberate-but-abnormal self-termination (task 3.9) also
    // triggers SCM's restart, not just a bare crash.
    SERVICE_FAILURE_ACTIONS_FLAG flag{};
    flag.fFailureActionsOnNonCrashFailures = TRUE;
    if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag)) {
        std::fwprintf(stderr, L"FastFilesSetup: ChangeServiceConfig2 failure-actions flag failed (error %lu)\n", GetLastError());
        return SetupResult::FromLastError();
    }

    SERVICE_DELAYED_AUTO_START_INFO delayedStart{};
    delayedStart.fDelayedAutostart = TRUE;
    if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayedStart)) {
        std::fwprintf(stderr, L"FastFilesSetup: ChangeServiceConfig2 delayed start failed (error %lu)\n", GetLastError());
        return SetupResult::FromLastError();
    }

    return SetupResult::Ok();
}

SetupResult ApplyServiceSecurityDescriptor(SC_HANDLE service, PSID clientGroupSid) noexcept {
    auto descriptor = BuildServiceObjectSecurityDescriptor(clientGroupSid);
    if (!descriptor) {
        return SetupResult::Failure(ERROR_INVALID_PARAMETER);
    }
    if (!SetServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, descriptor->attributes.lpSecurityDescriptor)) {
        return SetupResult::FromLastError();
    }
    return SetupResult::Ok();
}

} // namespace

SetupResult RegisterIndexService(const std::wstring& binaryPath, PSID clientGroupSid) noexcept {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (scm == nullptr) {
        return SetupResult::FromLastError();
    }

    // lpPassword is nullptr: virtual service accounts (NT SERVICE\<name>)
    // are managed by SCM itself and never take a password.
    // SCM parses lpBinaryPathName as a command line. Quote the executable
    // unconditionally so the standard Program Files install path cannot be
    // split at its first space (for example, into C:\Program.exe).
    const std::wstring quotedBinaryPath = L"\"" + binaryPath + L"\"";
    SC_HANDLE service = CreateServiceW(
        scm, kServiceName, kServiceDisplayName,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        quotedBinaryPath.c_str(), nullptr, nullptr, nullptr,
        kServiceVirtualAccountName, nullptr);

    if (service == nullptr) {
        const DWORD error = GetLastError();
        CloseServiceHandle(scm);
        return SetupResult::Failure(error);
    }

    SetupResult result = GrantBackupPrivilegeToVirtualAccount();
    if (result.success) {
        result = ConfigureFailureActions(service);
    }
    if (result.success) {
        result = ApplyServiceSecurityDescriptor(service, clientGroupSid);
    }

    if (!result.success) {
        // Fail loudly: don't leave a half-configured service registered.
        DeleteService(service);
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return result;
}

SetupResult UnregisterIndexService() noexcept {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return SetupResult::FromLastError();
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        const DWORD error = GetLastError();
        CloseServiceHandle(scm);
        // Not-found is not a failure for an uninstall path.
        return error == ERROR_SERVICE_DOES_NOT_EXIST ? SetupResult::Ok() : SetupResult::Failure(error);
    }

    SERVICE_STATUS status{};
    ControlService(service, SERVICE_CONTROL_STOP, &status);

    const BOOL deleted = DeleteService(service);
    const DWORD deleteError = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!deleted) {
        return SetupResult::Failure(deleteError);
    }
    return SetupResult::Ok();
}

SetupResult ReapplyIndexServiceSecurity(PSID clientGroupSid) noexcept {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return SetupResult::FromLastError();
    }

    // SERVICE_START is additionally required when failure actions contain
    // SC_ACTION_RESTART, even though the configuration operation itself is
    // authorized by SERVICE_CHANGE_CONFIG.
    SC_HANDLE service = OpenServiceW(scm, kServiceName, WRITE_DAC | SERVICE_CHANGE_CONFIG | SERVICE_START);
    if (service == nullptr) {
        const DWORD error = GetLastError();
        std::fwprintf(stderr, L"FastFilesSetup: OpenService for security reapply failed (error %lu)\n", error);
        CloseServiceHandle(scm);
        return SetupResult::Failure(error);
    }

    SetupResult result = ConfigureFailureActions(service);
    if (!result.success) {
        std::fwprintf(stderr, L"FastFilesSetup: failure-action reconfiguration failed (error %lu)\n", result.errorCode);
    }
    if (result.success) {
        result = ApplyServiceSecurityDescriptor(service, clientGroupSid);
        if (!result.success) {
            std::fwprintf(stderr, L"FastFilesSetup: service DACL reapply failed (error %lu)\n", result.errorCode);
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return result;
}

} // namespace ffsetup
