#include "ffsetup/InstallDirAcl.h"

#include <windows.h>
#include <aclapi.h>

#include "ffsetup/Identifiers.h"
#include "ffsetup/SecurityDescriptors.h"

namespace ffsetup {

SetupResult ApplyInstallDirectorySecurity(const std::wstring& installDirPath) noexcept {
    auto clientGroupSid = LookupAccountSid(kAuthorizedClientGroupName);
    if (!clientGroupSid) {
        return SetupResult::FromLastError();
    }

    auto descriptor = BuildInstallDirSecurityDescriptor(clientGroupSid->Get());
    if (!descriptor) {
        return SetupResult::Failure(ERROR_INVALID_PARAMETER);
    }

    PACL dacl = nullptr;
    BOOL daclPresent = FALSE;
    BOOL daclDefaulted = FALSE;
    if (!GetSecurityDescriptorDacl(descriptor->attributes.lpSecurityDescriptor, &daclPresent, &dacl, &daclDefaulted)
        || !daclPresent) {
        return SetupResult::Failure(ERROR_INVALID_SECURITY_DESCR);
    }

    const DWORD result = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(installDirPath.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, dacl, nullptr);

    if (result != ERROR_SUCCESS) {
        return SetupResult::Failure(result);
    }
    return SetupResult::Ok();
}

} // namespace ffsetup
