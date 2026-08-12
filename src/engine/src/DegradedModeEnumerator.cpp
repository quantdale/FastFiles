#include "DegradedModeEnumerator.h"

#include <windows.h>

namespace ffengine {

namespace {

std::wstring ExtendedApiPath(const std::wstring& path) {
    if (path.starts_with(L"\\\\?\\") || path.starts_with(L"\\\\.\\")) {
        return path;
    }
    if (path.starts_with(L"\\\\")) {
        return L"\\\\?\\UNC\\" + path.substr(2);
    }
    if (path.size() >= 2 && path[1] == L':') {
        return L"\\\\?\\" + path;
    }
    return path;
}

EnumerationStatus ClassifyOpenError(DWORD error) {
    if (error == ERROR_ACCESS_DENIED) return EnumerationStatus::AccessDenied;
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
        case ERROR_BAD_NETPATH:
        case ERROR_BAD_NET_NAME:
        case ERROR_DEVICE_NOT_CONNECTED:
            return EnumerationStatus::NotFound;
        default:
            return EnumerationStatus::IoError;
    }
}

} // namespace

EnumerationResult EnumerateDirectoryDegraded(const std::wstring& directoryPath) {
    EnumerationResult result;

    std::wstring searchPath = ExtendedApiPath(directoryPath);
    if (!searchPath.empty() && searchPath.back() != L'\\' && searchPath.back() != L'/') {
        searchPath += L'\\';
    }
    searchPath += L'*';

    WIN32_FIND_DATAW findData{};
    HANDLE handle = FindFirstFileExW(
        searchPath.c_str(), FindExInfoBasic, &findData, FindExSearchNameMatch, nullptr, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        result.status = ClassifyOpenError(GetLastError());
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
        entry.sizeBytes = (static_cast<uint64_t>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
        ULARGE_INTEGER created{}, modified{};
        created.LowPart = findData.ftCreationTime.dwLowDateTime;
        created.HighPart = findData.ftCreationTime.dwHighDateTime;
        modified.LowPart = findData.ftLastWriteTime.dwLowDateTime;
        modified.HighPart = findData.ftLastWriteTime.dwHighDateTime;
        entry.creationTime = created.QuadPart;
        entry.lastModifiedTime = modified.QuadPart;
        entry.attributes = findData.dwFileAttributes;
        result.entries.push_back(std::move(entry));
    } while (FindNextFileW(handle, &findData));

    const DWORD finalError = GetLastError();

    FindClose(handle);
    result.status = finalError == ERROR_NO_MORE_FILES ? EnumerationStatus::Success : ClassifyOpenError(finalError);
    return result;
}

} // namespace ffengine
