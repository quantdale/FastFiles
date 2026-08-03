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



struct FileIdentity {
    DWORD volumeSerial = 0;
    DWORD indexHigh = 0;
    DWORD indexLow = 0;
    bool operator==(const FileIdentity& other) const {
        return volumeSerial == other.volumeSerial && indexHigh == other.indexHigh
            && indexLow == other.indexLow;
    }
};

struct WalkFrame {
    std::wstring path;
    uint32_t depth = 0;
    std::vector<FileIdentity> ancestors;
};

// Walks a directory tree with the exact FindFirstFileExW(FindExInfoBasic)
// call the product's degraded-mode enumerator uses (metadata only - never
// file data). Reparse-point directories (junctions/symlinks) are followed
// but bounded: a directory whose NTFS file id already appears among its
// ancestors is a cycle and is severed via the loop guard, so a junction-to-
// ancestor fixture terminates instead of hanging or overflowing the path
// buffer. Iterative DFS avoids the known nesting limits of FindFirstFileEx.
// `listed` is capped so a huge tree still aggregates without serializing
// every entry.
int RunWalkProbe(const wchar_t* rootPath) {
    constexpr uint32_t kMaxDepth = 32;
    constexpr size_t kMaxListed = 512;

    uint64_t entries = 0;
    uint64_t directories = 0;
    uint64_t reparsePoints = 0;
    uint64_t loopGuards = 0;
    uint64_t accessDenied = 0;
    uint32_t maxDepthSeen = 0;
    std::vector<std::wstring> listed;
    std::vector<std::wstring> deniedPaths;

    std::vector<WalkFrame> work;
    work.push_back({rootPath, 0, {}});
    while (!work.empty()) {
        WalkFrame frame = std::move(work.back());
        work.pop_back();
        if (frame.depth > maxDepthSeen) maxDepthSeen = frame.depth;

        std::wstring searchPath = frame.path;
        if (!searchPath.empty() && searchPath.back() != L'\\' && searchPath.back() != L'/') {
            searchPath += L'\\';
        }
        searchPath += L'*';

        WIN32_FIND_DATAW findData{};
        HANDLE handle = FindFirstFileExW(searchPath.c_str(), FindExInfoBasic, &findData,
                                         FindExSearchNameMatch, nullptr, 0);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_ACCESS_DENIED) {
                ++accessDenied;
                if (deniedPaths.size() < 16) deniedPaths.push_back(frame.path);
            }
            continue;
        }

        do {
            const std::wstring name(findData.cFileName);
            if (name == L"." || name == L"..") {
                continue;
            }
            ++entries;
            const uint32_t attributes = findData.dwFileAttributes;
            const bool isDirectory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool isReparse = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (isDirectory) ++directories;
            if (isReparse) ++reparsePoints;
            if (listed.size() < kMaxListed) {
                std::wstring marker = isDirectory ? L"D" : L"F";
                if (isReparse) marker += L"R";
                marker += name;
                listed.push_back(marker);
            }

            std::wstring childPath = frame.path;
            if (!childPath.empty() && childPath.back() != L'\\' && childPath.back() != L'/') {
                childPath += L'\\';
            }
            childPath += name;

            const bool descend = isDirectory && frame.depth + 1 <= kMaxDepth;
            if (!descend) continue;

            bool cycle = false;
            if (isReparse) {
                HANDLE dirHandle = CreateFileW(childPath.c_str(), FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                if (dirHandle != INVALID_HANDLE_VALUE) {
                    BY_HANDLE_FILE_INFORMATION info{};
                    if (GetFileInformationByHandle(dirHandle, &info)) {
                        const FileIdentity fileId{info.dwVolumeSerialNumber, info.nFileIndexHigh,
                                                 info.nFileIndexLow};
                        for (const FileIdentity& ancestorId : frame.ancestors) {
                            if (ancestorId == fileId) {
                                cycle = true;
                                ++loopGuards;
                                break;
                            }
                        }
                        if (!cycle && frame.depth < kMaxDepth) {
                            WalkFrame child;
                            child.path = childPath;
                            child.depth = frame.depth + 1;
                            child.ancestors = frame.ancestors;
                            child.ancestors.push_back(fileId);
                            work.push_back(std::move(child));
                        }
                    } else {
                        // Identity unreadable under this token: keep the
                        // walk bounded rather than risk an aliased loop.
                        cycle = true;
                        ++loopGuards;
                    }
                    CloseHandle(dirHandle);
                    if (cycle) continue;
                }
            } else {
                if (frame.depth + 1 <= kMaxDepth) {
                    WalkFrame child = frame;
                    child.path = childPath;
                    child.depth = frame.depth + 1;
                    work.push_back(std::move(child));
                }
            }
        } while (FindNextFileW(handle, &findData));
        FindClose(handle);
    }

    std::printf(
        "{\"tool\":\"fftest\",\"probe\":\"walk\",\"status\":\"PASS\",\"root\":\"%s\","
        "\"entries\":%llu,\"directories\":%llu,\"reparsePoints\":%llu,"
        "\"loopGuards\":%llu,\"accessDenied\":%llu,\"maxDepth\":%u,\"list\":[",
        JsonEscape(WideToUtf8(rootPath)).c_str(), entries, directories, reparsePoints,
        loopGuards, accessDenied, maxDepthSeen);
    for (size_t i = 0; i < listed.size(); ++i) {
        if (i != 0) std::printf(",");
        std::printf("\"%s\"", JsonEscape(WideToUtf8(listed[i])).c_str());
    }
    std::printf("]}\n");
    return kProbePass;
}

constexpr int kMaxListedStreamProbe = 64;

int RunStreamsProbe(const wchar_t* filePath) {
    std::vector<std::wstring> streams;
    WIN32_FIND_STREAM_DATA streamData{};
    HANDLE handle = FindFirstStreamW(filePath, FindStreamInfoStandard, &streamData, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        std::printf("{\"tool\":\"fftest\",\"probe\":\"streams\",\"status\":\"FAIL\",\"path\":\"%s\",\"error\":%lu}\n",
                    JsonEscape(WideToUtf8(filePath)).c_str(), GetLastError());
        return kProbeFail;
    }
    do {
        streams.push_back(std::wstring(streamData.cStreamName));
    } while (FindNextStreamW(handle, &streamData));
    FindClose(handle);

    std::printf("{\"tool\":\"fftest\",\"probe\":\"streams\",\"status\":\"PASS\",\"path\":\"%s\",\"streams\":[",
                JsonEscape(WideToUtf8(filePath)).c_str());
    for (size_t i = 0; i < streams.size() && i < kMaxListedStreamProbe; ++i) {
        if (i != 0) std::printf(",");
        std::printf("\"%s\"", JsonEscape(WideToUtf8(streams[i])).c_str());
    }
    std::printf("]}\n");
    return kProbePass;
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
    std::fprintf(stderr, "usage: fftest <privilege|token|acl <path>|mapping <name>|walk <root>|streams <file>|handshake-impostor <install-dir>>\n");
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
    if (_wcsicmp(argv[1], L"walk") == 0 && argc == 3) return RunWalkProbe(argv[2]);
    if (_wcsicmp(argv[1], L"streams") == 0 && argc == 3) return RunStreamsProbe(argv[2]);
    if (_wcsicmp(argv[1], L"handshake-impostor") == 0 && argc == 3) return RunHandshakeImpostorProbe(argv[2]);
    PrintUsage();
    return kUsageError;
}
