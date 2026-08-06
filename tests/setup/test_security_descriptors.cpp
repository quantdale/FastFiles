// SDDL-level assertions on the five ffsetup security-descriptor builders
// (src/setup/src/SecurityDescriptors.cpp). Zero coverage existed for these;
// a regression adding Everyone or WRITE_DAC to any ACL would pass all tests.
//
// Each builder returns a self-contained absolute security descriptor; the
// test converts it to SDDL and asserts on the SDDL text, which is robust
// against internal ACL layout details:
//   (a) no Everyone (S-1-1-0) or Authenticated Users (S-1-5-11) SID appears
//       in any built descriptor;
//   (b) the client group (FastFilesUsers) gets read+write on the Ctrl/Data
//       pipes but never WRITE_DAC (WD)/WRITE_OWNER (WO);
//   (c) the client group gets query-only rights on the service object --
//       SERVICE_QUERY_CONFIG (CC)/SERVICE_QUERY_STATUS (LC), never start
//       (DT)/stop (WO)/change (DC)/user-defined-control (RP);
//   (d) the install dir grants the client group read+execute, never write;
//   (e) the admin-only dirs never contain the client group SID.
//
// The client group is created at install time, so it may not exist on a dev
// machine. The group-dependent builders all return std::nullopt for a null
// client-group SID, so those assertions are skipped with a printed note when
// LookupAccountSid cannot resolve the group (mirrors the skip pattern in
// tests/engine/test_authenticode_verification.cpp).

#include <windows.h>
#include <sddl.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "ffsetup/Identifiers.h"
#include "ffsetup/SecurityDescriptors.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

std::optional<std::wstring> ToSddl(const ffsetup::OwnedSecurityDescriptor& sd) {
    LPWSTR text = nullptr;
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(
            sd.attributes.lpSecurityDescriptor, SDDL_REVISION_1,
            OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            &text, nullptr)) {
        return std::nullopt;
    }
    std::wstring result(text);
    LocalFree(text);
    return result;
}

std::optional<std::wstring> SidToString(PSID sid) {
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(sid, &text)) {
        return std::nullopt;
    }
    std::wstring result(text);
    LocalFree(text);
    return result;
}

// Splits an SDDL string into its parenthesized ACE segments, e.g.
// "(A;;FA;;;S-1-5-18)(A;;GRGW;;;S-1-5-21-...)" ->
// {"A;;FA;;;S-1-5-18", "A;;GRGW;;;S-1-5-21-..."}.
std::vector<std::wstring> AceSegments(const std::wstring& sddl) {
    std::vector<std::wstring> segments;
    size_t start = sddl.find(L'(');
    while (start != std::wstring::npos) {
        const size_t end = sddl.find(L')', start);
        if (end == std::wstring::npos) {
            break;
        }
        segments.push_back(sddl.substr(start + 1, end - start - 1));
        start = sddl.find(L'(', end + 1);
    }
    return segments;
}

// Returns the rights field of the ACE whose SID is sidString. SDDL ACE
// layout: aceType;aceFlags;rights;objectGuid;inheritGuid;sid -- the SID is
// the final ';'-delimited field.
std::optional<std::wstring> RightsForSid(const std::vector<std::wstring>& segments,
                                         const std::wstring& sidString) {
    for (const auto& segment : segments) {
        if (segment.size() > sidString.size() &&
            segment[segment.size() - sidString.size() - 1] == L';' &&
            segment.compare(segment.size() - sidString.size(), sidString.size(), sidString) == 0) {
            const size_t first = segment.find(L';');
            const size_t second =
                first == std::wstring::npos ? std::wstring::npos : segment.find(L';', first + 1);
            const size_t third =
                second == std::wstring::npos ? std::wstring::npos : segment.find(L';', second + 1);
            if (third != std::wstring::npos) {
                return segment.substr(second + 1, third - second - 1);
            }
        }
    }
    return std::nullopt;
}

// Bit groups for the SDDL-derived access masks. The builders grant generic
// rights, which SetEntriesInAclW sometimes maps to file-specific bits
// (FILE_GENERIC_READ|FILE_GENERIC_WRITE -> 0x12019F, printed by the SDDL
// converter as "0x12019f") and sometimes leaves as raw generic bits
// (GENERIC_READ|GENERIC_EXECUTE -> "GXGR"), so accept either form.
constexpr DWORD kReadBits = 0x80000000 | 0x00120089;    // GENERIC_READ | FILE_GENERIC_READ
constexpr DWORD kWriteBits = 0x40000000 | 0x00120116;   // GENERIC_WRITE | FILE_GENERIC_WRITE
constexpr DWORD kExecuteBits = 0x20000000 | 0x001200A0; // GENERIC_EXECUTE | FILE_GENERIC_EXECUTE
constexpr DWORD kWriteDac = 0x00040000;                 // WRITE_DAC (SDDL "WD")
constexpr DWORD kWriteOwner = 0x00080000;               // WRITE_OWNER (SDDL "WO")

// Decodes an SDDL access-rights field ("FA", "CCLC", "0x12019f", ...) into
// the raw access mask. Returns std::nullopt for malformed hex or unknown
// letter pairs. Letter constants are from the Windows SDK sddl.h.
std::optional<DWORD> SddlRightsToMask(const std::wstring& rights) {
    if (rights.size() > 2 && rights[0] == L'0' && (rights[1] == L'x' || rights[1] == L'X')) {
        DWORD mask = 0;
        for (size_t i = 2; i < rights.size(); ++i) {
            const wchar_t ch = rights[i];
            int digit = -1;
            if (ch >= L'0' && ch <= L'9') {
                digit = ch - L'0';
            } else if (ch >= L'a' && ch <= L'f') {
                digit = ch - L'a' + 10;
            } else if (ch >= L'A' && ch <= L'F') {
                digit = ch - L'A' + 10;
            } else {
                return std::nullopt;
            }
            mask = (mask << 4) | static_cast<DWORD>(digit);
        }
        return mask;
    }
    if (rights.size() % 2 != 0) {
        return std::nullopt;
    }
    struct RightLetter {
        const wchar_t* letters;
        DWORD mask;
    };
    static const RightLetter kLetters[] = {
        {L"GA", 0x10000000}, {L"GR", 0x80000000}, {L"GW", 0x40000000}, {L"GX", 0x20000000},
        {L"FA", 0x001F01FF}, {L"FR", 0x00120089}, {L"FW", 0x00120116}, {L"FX", 0x001200A0},
        {L"CC", 0x00000001}, {L"DC", 0x00000002}, {L"LC", 0x00000004}, {L"SW", 0x00000008},
        {L"DT", 0x00000010}, {L"WO", 0x00000020}, {L"RP", 0x00000100},
        {L"RC", 0x00020000}, {L"WD", 0x00040000}, {L"SD", 0x00010000},
    };
    DWORD mask = 0;
    for (size_t i = 0; i < rights.size(); i += 2) {
        const std::wstring pair = rights.substr(i, 2);
        bool found = false;
        for (const RightLetter& letter : kLetters) {
            if (pair == letter.letters) {
                mask |= letter.mask;
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return mask;
}

void AssertNoEveryoneOrAuthenticatedUsers(const std::wstring& sddl, const char* which) {
    char description[160];
    std::snprintf(description, sizeof(description), "%s: DACL contains no Everyone SID (S-1-1-0)", which);
    Check(sddl.find(L"S-1-1-0") == std::wstring::npos, description);
    std::snprintf(description, sizeof(description),
                  "%s: DACL contains no Authenticated Users SID (S-1-5-11)", which);
    Check(sddl.find(L"S-1-5-11") == std::wstring::npos, description);
}

void TestPipeDescriptor(PSID clientGroupSid, const std::wstring& clientSidString) {
    const auto sd = ffsetup::BuildPipeSecurityDescriptor(clientGroupSid);
    Check(sd.has_value(), "pipe: BuildPipeSecurityDescriptor succeeds");
    if (!sd.has_value()) {
        return;
    }
    const auto sddl = ToSddl(*sd);
    Check(sddl.has_value(), "pipe: security descriptor converts to SDDL");
    if (!sddl.has_value()) {
        return;
    }
    std::printf("pipe SDDL: %ls\n", sddl->c_str());
    AssertNoEveryoneOrAuthenticatedUsers(*sddl, "pipe");

    Check(sddl->find(clientSidString) != std::wstring::npos,
          "pipe: DACL contains the client group SID");
    const auto rights = RightsForSid(AceSegments(*sddl), clientSidString);
    Check(rights.has_value(), "pipe: client group has exactly one ACE");
    if (!rights.has_value()) {
        return;
    }
    const auto mask = SddlRightsToMask(*rights);
    Check(mask.has_value(), "pipe: client group ACE rights decode to an access mask");
    if (mask.has_value()) {
        Check((*mask & kReadBits) != 0, "pipe: client group ACE grants read");
        Check((*mask & kWriteBits) != 0, "pipe: client group ACE grants write");
        Check((*mask & (kWriteDac | kWriteOwner)) == 0,
              "pipe: client group ACE grants no WRITE_DAC/WRITE_OWNER");
    }
    Check(rights->find(L"WD") == std::wstring::npos,
          "pipe: client group ACE text contains no WRITE_DAC (WD)");
    Check(rights->find(L"WO") == std::wstring::npos,
          "pipe: client group ACE text contains no WRITE_OWNER (WO)");
}

void TestServiceObjectDescriptor(PSID clientGroupSid, const std::wstring& clientSidString) {
    const auto sd = ffsetup::BuildServiceObjectSecurityDescriptor(clientGroupSid);
    Check(sd.has_value(), "service object: BuildServiceObjectSecurityDescriptor succeeds");
    if (!sd.has_value()) {
        return;
    }
    const auto sddl = ToSddl(*sd);
    Check(sddl.has_value(), "service object: security descriptor converts to SDDL");
    if (!sddl.has_value()) {
        return;
    }
    std::printf("service SDDL: %ls\n", sddl->c_str());
    AssertNoEveryoneOrAuthenticatedUsers(*sddl, "service object");

    const auto rights = RightsForSid(AceSegments(*sddl), clientSidString);
    Check(rights.has_value(), "service object: client group has exactly one ACE");
    if (!rights.has_value()) {
        return;
    }
    // Spec "No Client-Grantable Service Control Rights": query status and
    // query config only. SDDL letters (service-object meaning): CC =
    // SERVICE_QUERY_CONFIG, LC = SERVICE_QUERY_STATUS, DT = SERVICE_START,
    // WO = SERVICE_STOP, RP = SERVICE_USER_DEFINED_CONTROL, DC =
    // SERVICE_CHANGE_CONFIG, SW = SERVICE_ENUMERATE_DEPENDENTS, SD =
    // SERVICE_ALL_ACCESS.
    Check(rights->find(L"CC") != std::wstring::npos,
          "service object: client group ACE grants SERVICE_QUERY_CONFIG (CC)");
    Check(rights->find(L"LC") != std::wstring::npos,
          "service object: client group ACE grants SERVICE_QUERY_STATUS (LC)");
    const auto mask = SddlRightsToMask(*rights);
    Check(mask.has_value(), "service object: client group ACE rights decode to an access mask");
    if (mask.has_value()) {
        Check(*mask == (SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG),
              "service object: client group ACE grants exactly query status and query config");
    }
    Check(rights->find(L"RP") == std::wstring::npos,
          "service object: client group ACE grants no SERVICE_USER_DEFINED_CONTROL (RP)");
    Check(rights->find(L"WP") == std::wstring::npos,
          "service object: client group ACE grants no write-property right (WP)");
    Check(rights->find(L"DT") == std::wstring::npos,
          "service object: client group ACE grants no SERVICE_START (DT)");
    Check(rights->find(L"WO") == std::wstring::npos,
          "service object: client group ACE grants no SERVICE_STOP/WRITE_OWNER (WO)");
    Check(rights->find(L"DC") == std::wstring::npos,
          "service object: client group ACE grants no SERVICE_CHANGE_CONFIG (DC)");
    Check(rights->find(L"SW") == std::wstring::npos,
          "service object: client group ACE grants no SERVICE_ENUMERATE_DEPENDENTS (SW)");
    Check(rights->find(L"SD") == std::wstring::npos,
          "service object: client group ACE grants no SERVICE_ALL_ACCESS (SD)");
}

void TestInstallDirDescriptor(PSID clientGroupSid, const std::wstring& clientSidString) {
    const auto sd = ffsetup::BuildInstallDirSecurityDescriptor(clientGroupSid);
    Check(sd.has_value(), "install dir: BuildInstallDirSecurityDescriptor succeeds");
    if (!sd.has_value()) {
        return;
    }
    const auto sddl = ToSddl(*sd);
    Check(sddl.has_value(), "install dir: security descriptor converts to SDDL");
    if (!sddl.has_value()) {
        return;
    }
    std::printf("install dir SDDL: %ls\n", sddl->c_str());
    AssertNoEveryoneOrAuthenticatedUsers(*sddl, "install dir");

    const auto rights = RightsForSid(AceSegments(*sddl), clientSidString);
    Check(rights.has_value(), "install dir: client group has exactly one ACE");
    if (!rights.has_value()) {
        return;
    }
    const auto mask = SddlRightsToMask(*rights);
    Check(mask.has_value(), "install dir: client group ACE rights decode to an access mask");
    if (mask.has_value()) {
        Check((*mask & kReadBits) != 0, "install dir: client group ACE grants read");
        Check((*mask & kExecuteBits) != 0, "install dir: client group ACE grants execute");
        Check((*mask & kWriteBits) == 0, "install dir: client group ACE grants no write");
        Check((*mask & (kWriteDac | kWriteOwner)) == 0,
              "install dir: client group ACE grants no WRITE_DAC/WRITE_OWNER");
    }
}

void TestAdminOnlyDescriptor(const std::wstring& clientSidString) {
    const auto sd = ffsetup::BuildAdminOnlySecurityDescriptor();
    Check(sd.has_value(), "admin-only: BuildAdminOnlySecurityDescriptor succeeds");
    if (!sd.has_value()) {
        return;
    }
    const auto sddl = ToSddl(*sd);
    Check(sddl.has_value(), "admin-only: security descriptor converts to SDDL");
    if (!sddl.has_value()) {
        return;
    }
    std::printf("admin-only SDDL: %ls\n", sddl->c_str());
    AssertNoEveryoneOrAuthenticatedUsers(*sddl, "admin-only");

    if (!clientSidString.empty()) {
        Check(sddl->find(clientSidString) == std::wstring::npos,
              "admin-only: DACL contains no client group SID");
    }
}

void TestCurrentUserPipeDescriptor() {
    const auto sd = ffsetup::BuildCurrentUserPipeSecurityDescriptor();
    Check(sd.has_value(), "current-user pipe: BuildCurrentUserPipeSecurityDescriptor succeeds");
    if (!sd.has_value()) {
        return;
    }
    const auto sddl = ToSddl(*sd);
    Check(sddl.has_value(), "current-user pipe: security descriptor converts to SDDL");
    if (!sddl.has_value()) {
        return;
    }
    std::printf("current-user pipe SDDL: %ls\n", sddl->c_str());
    AssertNoEveryoneOrAuthenticatedUsers(*sddl, "current-user pipe");
}

}  // namespace

int main() {
    // The client group (FastFilesUsers) is created at install time, so it may
    // not exist on a dev machine. The group-dependent builders return
    // std::nullopt when handed a null SID, so those assertions are skipped
    // with a printed note when the group cannot be resolved.
    const auto clientSid = ffsetup::LookupAccountSid(ffsetup::kAuthorizedClientGroupName);
    std::wstring clientSidString;
    if (!clientSid.has_value()) {
        std::fprintf(stderr,
                     "SKIP: local group '%ls' not resolvable on this host; "
                     "skipping the pipe/service-object/install-dir client-group assertions\n",
                     ffsetup::kAuthorizedClientGroupName);
    } else {
        const auto sidString = SidToString(clientSid->Get());
        Check(sidString.has_value(), "client group SID converts to a string");
        if (sidString.has_value()) {
            clientSidString = *sidString;
        }
    }

    TestAdminOnlyDescriptor(clientSidString);
    TestCurrentUserPipeDescriptor();
    if (!clientSidString.empty()) {
        TestPipeDescriptor(clientSid->Get(), clientSidString);
        TestServiceObjectDescriptor(clientSid->Get(), clientSidString);
        TestInstallDirDescriptor(clientSid->Get(), clientSidString);
    }

    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
