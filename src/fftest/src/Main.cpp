#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ClientAuthentication.h"
#include "PrivilegeVerification.h"
#include "ffprotocol/Commands.h"

namespace {

constexpr int kProbePass = 0;
constexpr int kProbeFail = 1;
constexpr int kUsageError = 2;

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::string JsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 16);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20) {
                char escaped[7];
                sprintf_s(escaped, "\\u%04x", character);
                result += escaped;
            } else {
                result += static_cast<char>(character);
            }
        }
    }
    return result;
}

const char* JsonBool(bool value) { return value ? "true" : "false"; }

int RunPrivilegeProbe() {
    const auto result = ffindexsvc::ProbeBackupPrivilegeSufficiency();
    const bool passed = result.privilegeEnabled && result.volumeFound && result.volumeOpened;
    std::printf(
        "{\"tool\":\"fftest\",\"probe\":\"backup-privilege\",\"status\":\"%s\","
        "\"privilegeEnabled\":%s,\"volumeFound\":%s,\"volumeOpened\":%s,"
        "\"journalQueried\":%s,\"driveLetter\":\"%c\",\"volumeOpenError\":%lu,"
        "\"journalQueryError\":%lu}\n",
        passed ? "PASS" : "FAIL", JsonBool(result.privilegeEnabled), JsonBool(result.volumeFound),
        JsonBool(result.volumeOpened), JsonBool(result.journalQueried),
        result.driveLetter == L'\0' ? ' ' : static_cast<char>(result.driveLetter),
        result.volumeOpenError, result.journalQueryError);
    return passed ? kProbePass : kProbeFail;
}

int RunTokenProbe() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        std::printf("{\"tool\":\"fftest\",\"probe\":\"token\",\"status\":\"FAIL\",\"error\":%lu}\n", GetLastError());
        return kProbeFail;
    }

    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned);
    DWORD integritySize = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &integritySize);
    std::vector<unsigned char> integrityBuffer(integritySize);
    DWORD integrityRid = 0;
    if (integritySize > 0 && GetTokenInformation(token, TokenIntegrityLevel,
            integrityBuffer.data(), integritySize, &integritySize)) {
        const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(integrityBuffer.data());
        integrityRid = *GetSidSubAuthority(label->Label.Sid,
            static_cast<DWORD>(*GetSidSubAuthorityCount(label->Label.Sid) - 1));
    }

    DWORD privilegeSize = 0;
    GetTokenInformation(token, TokenPrivileges, nullptr, 0, &privilegeSize);
    std::vector<unsigned char> privilegeBuffer(privilegeSize);
    std::vector<std::string> privileges;
    if (privilegeSize > 0 && GetTokenInformation(token, TokenPrivileges,
            privilegeBuffer.data(), privilegeSize, &privilegeSize)) {
        const auto* tokenPrivileges = reinterpret_cast<const TOKEN_PRIVILEGES*>(privilegeBuffer.data());
        for (DWORD i = 0; i < tokenPrivileges->PrivilegeCount; ++i) {
            wchar_t name[256];
            DWORD nameSize = static_cast<DWORD>(std::size(name));
            LUID luid = tokenPrivileges->Privileges[i].Luid;
            if (LookupPrivilegeNameW(nullptr, &luid, name, &nameSize)) {
                const bool enabled = (tokenPrivileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) != 0;
                privileges.push_back("{\"name\":\"" + JsonEscape(WideToUtf8(std::wstring(name, nameSize)))
                    + "\",\"enabled\":" + JsonBool(enabled) + "}");
            }
        }
    }
    CloseHandle(token);

    std::printf("{\"tool\":\"fftest\",\"probe\":\"token\",\"status\":\"PASS\",\"elevated\":%s,\"integrityRid\":%lu,\"privileges\":[",
                JsonBool(elevation.TokenIsElevated != 0), integrityRid);
    for (size_t i = 0; i < privileges.size(); ++i) {
        if (i != 0) std::printf(",");
        std::printf("%s", privileges[i].c_str());
    }
    std::printf("]}\n");
    return kProbePass;
}

int RunAclProbe(const wchar_t* path) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD error = GetNamedSecurityInfoW(const_cast<LPWSTR>(path), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr, &descriptor);
    if (error != ERROR_SUCCESS) {
        std::printf("{\"tool\":\"fftest\",\"probe\":\"acl\",\"status\":\"FAIL\",\"error\":%lu}\n", error);
        return kProbeFail;
    }
    LPWSTR sddl = nullptr;
    const bool converted = ConvertSecurityDescriptorToStringSecurityDescriptorW(
        descriptor, SDDL_REVISION_1,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &sddl, nullptr) != FALSE;
    const DWORD conversionError = converted ? ERROR_SUCCESS : GetLastError();
    const std::string utf8Path = JsonEscape(WideToUtf8(path));
    const std::string utf8Sddl = converted ? JsonEscape(WideToUtf8(sddl)) : std::string();
    if (sddl != nullptr) LocalFree(sddl);
    LocalFree(descriptor);
    std::printf("{\"tool\":\"fftest\",\"probe\":\"acl\",\"status\":\"%s\",\"path\":\"%s\",\"sddl\":\"%s\",\"error\":%lu}\n",
                converted ? "PASS" : "FAIL", utf8Path.c_str(), utf8Sddl.c_str(), conversionError);
    return converted ? kProbePass : kProbeFail;
}

int RunMappingProbe(const wchar_t* mappingName) {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName);
    const bool readable = mapping != nullptr;
    const DWORD error = readable ? ERROR_SUCCESS : GetLastError();
    if (mapping != nullptr) CloseHandle(mapping);
    std::printf("{\"tool\":\"fftest\",\"probe\":\"shared-memory\",\"status\":\"%s\",\"name\":\"%s\",\"readable\":%s,\"error\":%lu}\n",
                readable ? "PASS" : "FAIL", JsonEscape(WideToUtf8(mappingName)).c_str(),
                JsonBool(readable), error);
    return readable ? kProbePass : kProbeFail;
}

int RunHandshakeImpostorProbe(const wchar_t* installDirectory) {
    const std::wstring pipeName = L"\\\\.\\pipe\\FastFiles.Verify.Handshake."
        + std::to_wstring(GetCurrentProcessId());
    HANDLE server = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 5000, nullptr);
    if (server == INVALID_HANDLE_VALUE) {
        std::printf("{\"tool\":\"fftest\",\"probe\":\"handshake-impostor\",\"status\":\"FAIL\",\"error\":%lu}\n", GetLastError());
        return kProbeFail;
    }
    std::thread client([pipeName] {
        if (WaitNamedPipeW(pipeName.c_str(), 5000)) {
            HANDLE handle = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE,
                                        0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                Sleep(1000);
                CloseHandle(handle);
            }
        }
    });
    const BOOL connected = ConnectNamedPipe(server, nullptr);
    const DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();
    ffprotocol::HandshakeRejectReason rejectReason = ffprotocol::HandshakeRejectReason::NotAuthorizedGroup;
    auto identity = (connected || connectError == ERROR_PIPE_CONNECTED)
        ? ffindexsvc::VerifyClientAtHandshake(server, installDirectory, rejectReason)
        : std::nullopt;
    if (identity) {
        ffindexsvc::CloseClientIdentity(*identity);
    }
    DisconnectNamedPipe(server);
    CloseHandle(server);
    client.join();
    const bool rejectedAsImpostor = (connected || connectError == ERROR_PIPE_CONNECTED) && !identity
        && rejectReason == ffprotocol::HandshakeRejectReason::UnverifiedImagePath;
    std::printf("{\"tool\":\"fftest\",\"probe\":\"handshake-impostor\",\"status\":\"%s\",\"accepted\":%s,\"rejectReason\":%u}\n",
                rejectedAsImpostor ? "PASS" : "FAIL", JsonBool(identity.has_value()),
                static_cast<unsigned>(rejectReason));
    return rejectedAsImpostor ? kProbePass : kProbeFail;
}

void PrintUsage() {
    std::fprintf(stderr, "usage: fftest <privilege|token|acl <path>|mapping <name>|handshake-impostor <install-dir>>\n");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        PrintUsage();
        return kUsageError;
    }
    if (_wcsicmp(argv[1], L"privilege") == 0 && argc == 2) return RunPrivilegeProbe();
    if (_wcsicmp(argv[1], L"token") == 0 && argc == 2) return RunTokenProbe();
    if (_wcsicmp(argv[1], L"acl") == 0 && argc == 3) return RunAclProbe(argv[2]);
    if (_wcsicmp(argv[1], L"mapping") == 0 && argc == 3) return RunMappingProbe(argv[2]);
    if (_wcsicmp(argv[1], L"handshake-impostor") == 0 && argc == 3) return RunHandshakeImpostorProbe(argv[2]);
    PrintUsage();
    return kUsageError;
}
