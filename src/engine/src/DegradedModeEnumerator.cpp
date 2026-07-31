#include "DegradedModeEnumerator.h"

#include <windows.h>

namespace ffengine {

EnumerationResult EnumerateDirectoryDegraded(const std::wstring& directoryPath) {
    EnumerationResult result;

    std::wstring searchPath = directoryPath;
    if (!searchPath.empty() && searchPath.back() != L'\\' && searchPath.back() != L'/') {
        searchPath += L'\\';
    }
    searchPath += L'*';

    WIN32_FIND_DATAW findData{};
    HANDLE handle = FindFirstFileExW(
        searchPath.c_str(), FindExInfoBasic, &findData, FindExSearchNameMatch, nullptr, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        result.status = (error == ERROR_ACCESS_DENIED) ? EnumerationStatus::AccessDenied : EnumerationStatus::NotFound;
        return result;
    }

    do {
        const std::wstring name(findData.cFileName);
        if (name == L"." || name == L"..") {
            continue;
        }
        DirectoryEntry entry;
        entry.name = name;
        entry.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.accessible = true;
        result.entries.push_back(std::move(entry));
    } while (FindNextFileW(handle, &findData));

    FindClose(handle);
    result.status = EnumerationStatus::Success;
    return result;
}

} // namespace ffengine
