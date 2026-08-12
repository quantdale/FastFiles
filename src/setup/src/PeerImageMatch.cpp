#include "ffsetup/PeerImageMatch.h"

#include <windows.h>

#include <cwchar>
#include <iterator>

namespace ffsetup {

namespace {

// QueryFullProcessImageNameW and the launch path can spell the same local
// image with or without the Win32 extended-length prefix. Normalize only
// those namespace aliases before the exact directory/filename comparison;
// device paths and all other namespaces remain non-equivalent.
std::wstring NormalizePathNamespace(std::wstring path) {
    constexpr wchar_t kExtendedUncPrefix[] = L"\\\\?\\UNC\\";
    constexpr wchar_t kExtendedPathPrefix[] = L"\\\\?\\";
    if (path.rfind(kExtendedUncPrefix, 0) == 0) {
        return L"\\\\" + path.substr(std::size(kExtendedUncPrefix) - 1);
    }
    if (path.rfind(kExtendedPathPrefix, 0) == 0) {
        return path.substr(std::size(kExtendedPathPrefix) - 1);
    }
    return path;
}

} // namespace

bool IsExpectedInstalledBinary(const std::wstring& processImagePath,
                               const std::wstring& expectedInstallDir,
                               const std::wstring& expectedFileName) {
    wchar_t canonicalPath[MAX_PATH * 4];
    wchar_t canonicalInstallDir[MAX_PATH * 4];
    if (GetFullPathNameW(processImagePath.c_str(), static_cast<DWORD>(std::size(canonicalPath)), canonicalPath, nullptr) == 0) {
        return false;
    }
    if (GetFullPathNameW(expectedInstallDir.c_str(), static_cast<DWORD>(std::size(canonicalInstallDir)), canonicalInstallDir, nullptr) == 0) {
        return false;
    }

    std::wstring fullPath = NormalizePathNamespace(canonicalPath);
    std::wstring dir = NormalizePathNamespace(canonicalInstallDir);
    if (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) {
        dir.pop_back();
    }

    const size_t lastSlash = fullPath.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return false;
    }
    const std::wstring fileDir = fullPath.substr(0, lastSlash);
    const std::wstring fileName = fullPath.substr(lastSlash + 1);

    return _wcsicmp(fileDir.c_str(), dir.c_str()) == 0
        && _wcsicmp(fileName.c_str(), expectedFileName.c_str()) == 0;
}

} // namespace ffsetup
