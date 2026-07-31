#pragma once
#include <string>
#include <windows.h>

#include "ffsetup/SetupResult.h"

namespace ffsetup {

// Creates the FastFilesUsers local group (idempotent: already-exists is
// treated as success) and adds the given user to it (task 6.1).
SetupResult CreateAuthorizedClientGroupAndAddUser(const std::wstring& userName) noexcept;

// Reverses CreateAuthorizedClientGroupAndAddUser: deletes the local group
// (task 6.4 uninstall path). Membership is implicitly removed with the
// group.
SetupResult DeleteAuthorizedClientGroup() noexcept;

// Queries CURRENT membership of the FastFilesUsers local group by
// enumerating its live member list -- deliberately not a check against a
// cached logon token, since a token captured at logon does not reflect a
// group membership change made afterwards (spec "Revoked membership closes
// an active connection"; task 3.5 periodic re-validation).
bool IsUserInAuthorizedClientGroup(PSID userSid) noexcept;

} // namespace ffsetup
