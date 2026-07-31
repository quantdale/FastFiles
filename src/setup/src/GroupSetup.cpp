#include "ffsetup/GroupSetup.h"

#include <windows.h>
#include <lm.h>

#include "ffsetup/Identifiers.h"

namespace ffsetup {

namespace {

SetupResult CreateGroupIfMissing() noexcept {
    LOCALGROUP_INFO_1 info{};
    info.lgrpi1_name = const_cast<LPWSTR>(kAuthorizedClientGroupName);
    info.lgrpi1_comment = const_cast<LPWSTR>(L"FastFiles: authorized to reach FastFilesIndexSvc's privileged pipes.");

    DWORD parmErr = 0;
    NET_API_STATUS status = NetLocalGroupAdd(nullptr, 1, reinterpret_cast<LPBYTE>(&info), &parmErr);
    if (status != NERR_Success && status != NERR_GroupExists) {
        return SetupResult::Failure(status);
    }
    return SetupResult::Ok();
}

} // namespace

SetupResult CreateAuthorizedClientGroupAndAddUser(const std::wstring& userName) noexcept {
    SetupResult created = CreateGroupIfMissing();
    if (!created.success) {
        return created;
    }

    LOCALGROUP_MEMBERS_INFO_3 member{};
    member.lgrmi3_domainandname = const_cast<LPWSTR>(userName.c_str());

    NET_API_STATUS status = NetLocalGroupAddMembers(
        nullptr, kAuthorizedClientGroupName, 3, reinterpret_cast<LPBYTE>(&member), 1);
    if (status != NERR_Success && status != ERROR_MEMBER_IN_ALIAS) {
        return SetupResult::Failure(status);
    }
    return SetupResult::Ok();
}

SetupResult DeleteAuthorizedClientGroup() noexcept {
    NET_API_STATUS status = NetLocalGroupDel(nullptr, kAuthorizedClientGroupName);
    if (status != NERR_Success && status != NERR_GroupNotFound) {
        return SetupResult::Failure(status);
    }
    return SetupResult::Ok();
}

bool IsUserInAuthorizedClientGroup(PSID userSid) noexcept {
    if (userSid == nullptr) {
        return false;
    }

    LPBYTE buffer = nullptr;
    DWORD entriesRead = 0;
    DWORD totalEntries = 0;
    NET_API_STATUS status = NetLocalGroupGetMembers(
        nullptr, kAuthorizedClientGroupName, 1, &buffer, MAX_PREFERRED_LENGTH,
        &entriesRead, &totalEntries, nullptr);

    if (status != NERR_Success || buffer == nullptr) {
        if (buffer != nullptr) {
            NetApiBufferFree(buffer);
        }
        return false;
    }

    bool found = false;
    const LOCALGROUP_MEMBERS_INFO_1* members = reinterpret_cast<const LOCALGROUP_MEMBERS_INFO_1*>(buffer);
    for (DWORD i = 0; i < entriesRead; ++i) {
        if (members[i].lgrmi1_sid != nullptr && EqualSid(userSid, members[i].lgrmi1_sid)) {
            found = true;
            break;
        }
    }

    NetApiBufferFree(buffer);
    return found;
}

} // namespace ffsetup
