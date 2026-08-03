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

constexpr wchar_t kGrantedPrivilege[] = L"SeBackupPrivilege";

// Resolves the SID of the account selected by options, or nullopt when the
// account name cannot be resolved (e.g. the virtual account does not exist
// yet because the service has not been created).
std::optional<OwnedSid> ResolveAccountSid(const ServiceAccountOptions& options) noexcept {
    switch (options.type) {
        case ServiceAccountType::VirtualAccount:
            return LookupAccountSid(kServiceVirtualAccountName);
        case ServiceAccountType::LocalSystem:
            return LookupAccountSid(L"NT AUTHORITY\\SYSTEM");
        case ServiceAccountType::NetworkService:
            return LookupAccountSid(L"NT AUTHORITY\\NetworkService");
        case ServiceAccountType::LocalService:
            return LookupAccountSid(L"NT AUTHORITY\\LocalService");
        case ServiceAccountType::NamedUser:
            if (options.userName.empty()) {
                return std::nullopt;
            }
            return LookupAccountSid(options.userName.c_str());
    }
    return std::nullopt;
}

LSA_HANDLE OpenPolicyForPrivilegeGrant() noexcept {
    LSA_OBJECT_ATTRIBUTES objectAttributes{};
    LSA_HANDLE policyHandle = nullptr;
    const NTSTATUS status =
        LsaOpenPolicy(nullptr, &objectAttributes, POLICY_CREATE_ACCOUNT | POLICY_LOOKUP_NAMES, &policyHandle);
    return status == 0 ? policyHandle : nullptr;
}

} // namespace

std::wstring ResolveServiceStartName(const ServiceAccountOptions& options) noexcept {
    switch (options.type) {
        case ServiceAccountType::VirtualAccount:
            return kServiceVirtualAccountName;
        case ServiceAccountType::LocalSystem:
            return L"LocalSystem";
        case ServiceAccountType::NetworkService:
            return L"NT AUTHORITY\\NetworkService";
        case ServiceAccountType::LocalService:
            return L"NT AUTHORITY\\LocalService";
        case ServiceAccountType::NamedUser:
            return options.userName;
    }
    return L"";
}

SetupResult GrantServiceAccountPrivilege(const ServiceAccountOptions& options) noexcept {
    // LocalSystem inherently holds every privilege; an LSA grant against the
    // SYSTEM SID is a no-op and is intentionally skipped.
    if (options.type == ServiceAccountType::LocalSystem) {
        return SetupResult::Ok();
    }

    auto accountSid = ResolveAccountSid(options);
    if (!accountSid) {
        return SetupResult::FromLastError();
    }

    LSA_HANDLE policyHandle = OpenPolicyForPrivilegeGrant();
    if (policyHandle == nullptr) {
        return SetupResult::FromLastError();
    }

    LSA_UNICODE_STRING privilege = MakeLsaString(kGrantedPrivilege);
    const NTSTATUS status = LsaAddAccountRights(policyHandle, accountSid->Get(), &privilege, 1);
    LsaClose(policyHandle);

    if (status != 0) {
        return SetupResult::Failure(LsaNtStatusToWinError(status));
    }
    return SetupResult::Ok();
}

SetupResult RevokeServiceAccountPrivilege(const ServiceAccountOptions& options) noexcept {
    if (options.type == ServiceAccountType::LocalSystem) {
        return SetupResult::Ok();
    }

    auto accountSid = ResolveAccountSid(options);
    if (!accountSid) {
        // A missing account means there is nothing to revoke from; the
        // uninstall/rollback path must stay idempotent.
        return SetupResult::Ok();
    }

    LSA_HANDLE policyHandle = OpenPolicyForPrivilegeGrant();
    if (policyHandle == nullptr) {
        return SetupResult::FromLastError();
    }

    LSA_UNICODE_STRING privilege = MakeLsaString(kGrantedPrivilege);
    const NTSTATUS status = LsaRemoveAccountRights(policyHandle, accountSid->Get(), FALSE, &privilege, 1);
    LsaClose(policyHandle);

    if (status != 0) {
        const DWORD winError = LsaNtStatusToWinError(status);
        // STATUS_SUCCESS / not-assigned-rights both mean the right is gone.
        if (status != 0 && winError != ERROR_NOT_FOUND) {
            return SetupResult::Failure(winError);
        }
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

SetupResult RegisterIndexService(const std::wstring& binaryPath, PSID clientGroupSid,
                                 const ServiceAccountOptions& options) noexcept {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (scm == nullptr) {
        return SetupResult::FromLastError();
    }

    // LocalSystem services take a nullptr start name; everything else is an
    // explicit account SCM resolves.
    const std::wstring startName = ResolveServiceStartName(options);
    if (startName.empty()) {
        CloseServiceHandle(scm);
        return SetupResult::Failure(ERROR_INVALID_PARAMETER);
    }
    const wchar_t* scmStartName = options.type == ServiceAccountType::LocalSystem ? nullptr : startName.c_str();

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
        scmStartName, nullptr);

    if (service == nullptr) {
        const DWORD error = GetLastError();
        CloseServiceHandle(scm);
        return SetupResult::Failure(error);
    }

    // Task 2.2 / spec "Grant failure prevents an invalid service start":
    // every step after creation is part of one transaction. Any failure
    // deletes the service and revokes the rights granted for it, so a failed
    // install never reports a usable indexing service and never orphans a
    // privilege grant on a half-configured account.
    SetupResult result = GrantServiceAccountPrivilege(options);
    if (result.success) {
        result = ConfigureFailureActions(service);
    }
    if (result.success) {
        result = ApplyServiceSecurityDescriptor(service, clientGroupSid);
    }

    if (!result.success) {
        // Fail loudly: don't leave a half-configured service registered.
        DeleteService(service);
        const SetupResult revokeResult = RevokeServiceAccountPrivilege(options);
        if (!revokeResult.success) {
            std::fwprintf(stderr, L"FastFilesSetup: rollback revoke of %ls failed (error %lu)\n",
                          kGrantedPrivilege, revokeResult.errorCode);
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return result;
}

namespace {

// Maps an SCM start name (lpServiceStartName) back to account options so
// registration, uninstall, and upgrade rollback can revoke/regrant rights
// for the account a service is *actually* configured under.
std::optional<ServiceAccountOptions> OptionsFromStartName(const wchar_t* startName) {
    ServiceAccountOptions options;
    const std::wstring name = startName ? startName : L"";
    if (name == kServiceVirtualAccountName) {
        options.type = ServiceAccountType::VirtualAccount;
    } else if (name == L"NT AUTHORITY\\NetworkService") {
        options.type = ServiceAccountType::NetworkService;
    } else if (name == L"NT AUTHORITY\\LocalService") {
        options.type = ServiceAccountType::LocalService;
    } else if (name.empty() || name == L"LocalSystem") {
        options.type = ServiceAccountType::LocalSystem;
    } else {
        options.type = ServiceAccountType::NamedUser;
        options.userName = name;
    }
    return options;
}

} // namespace

std::optional<ServiceAccountOptions> QueryIndexServiceAccount() noexcept {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return std::nullopt;
    }
    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_QUERY_CONFIG);
    if (service == nullptr) {
        const DWORD error = GetLastError();
        CloseServiceHandle(scm);
        return error == ERROR_SERVICE_DOES_NOT_EXIST ? std::optional<ServiceAccountOptions>{} : std::nullopt;
    }

    std::optional<ServiceAccountOptions> result;
    DWORD configBytesNeeded = 0;
    if (QueryServiceConfigW(service, nullptr, 0, &configBytesNeeded)
        || GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        std::vector<BYTE> configBuffer(configBytesNeeded > 0 ? configBytesNeeded : 1);
        if (QueryServiceConfigW(service, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(configBuffer.data()),
                                static_cast<DWORD>(configBuffer.size()), &configBytesNeeded)) {
            const auto* config = reinterpret_cast<const QUERY_SERVICE_CONFIGW*>(configBuffer.data());
            result = OptionsFromStartName(config->lpServiceStartName);
        }
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

    // Revoke the privilege grant from the account the service is actually
    // running under (task 2.1: registration and provisioning must be
    // reversible for every account type).
    const std::optional<ServiceAccountOptions> options = QueryIndexServiceAccount();

    SERVICE_STATUS status{};
    ControlService(service, SERVICE_CONTROL_STOP, &status);

    const BOOL deleted = DeleteService(service);
    const DWORD deleteError = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!deleted) {
        return SetupResult::Failure(deleteError);
    }

    // Best-effort revocation; the service is already gone, so a revocation
    // failure cannot leave a usable service behind (only an unused right on
    // a service account SID, which is removed with the account).
    if (options) {
        const SetupResult revokeResult = RevokeServiceAccountPrivilege(*options);
        if (!revokeResult.success) {
            std::fwprintf(stderr, L"FastFilesSetup: privilege revocation for uninstall failed (error %lu)\n",
                          revokeResult.errorCode);
        }
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
