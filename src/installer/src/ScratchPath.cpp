#include "ScratchPath.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <iterator>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace ffinstaller {

std::optional<std::wstring> CreateVerifiedScratchDirectory() {
    PWSTR programDataPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programDataPath))) {
        return std::nullopt;
    }
    std::wstring baseDir(programDataPath);
    CoTaskMemFree(programDataPath);
    baseDir += L"\\FastFiles\\InstallScratch";
    CreateDirectoryW((baseDir.substr(0, baseDir.find_last_of(L'\\'))).c_str(), nullptr);
    CreateDirectoryW(baseDir.c_str(), nullptr);

    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return std::nullopt;
    }
    wchar_t guidText[64];
    swprintf(guidText, std::size(guidText),
             L"%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
             guid.Data1, guid.Data2, guid.Data3,
             guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
             guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

    const std::wstring scratchPath = baseDir + L"\\" + guidText;

    // A path that already exists here (predicted or pre-planted) is
    // rejected outright rather than reused -- CreateDirectory fails on an
    // existing name, which is exactly the check we want.
    if (!CreateDirectoryW(scratchPath.c_str(), nullptr)) {
        return std::nullopt;
    }

    const DWORD attributes = GetFileAttributesW(scratchPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        // Something else materialized a reparse point at this exact
        // random name between our CreateDirectory call and this check --
        // treat as compromised and refuse to use it (do not delete
        // through it: leave it for offline investigation).
        return std::nullopt;
    }

    return scratchPath;
}

} // namespace ffinstaller
