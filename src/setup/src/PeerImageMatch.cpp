#include "ffsetup/PeerImageMatch.h"

#include <windows.h>

#include <cwchar>
#include <iterator>

namespace ffsetup {

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

    std::wstring fullPath(canonicalPath);
    std::wstring dir(canonicalInstallDir);
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
