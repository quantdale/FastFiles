#include "NewFileWriter.h"

#include <array>
#include <cwctype>

namespace ffui {

NewFileWriteResult WriteNewFile(const std::wstring& path, IStream* stream) {
    if (stream == nullptr) return {E_POINTER};
    HRESULT result = stream->Seek({}, STREAM_SEEK_SET, nullptr);
    if (FAILED(result)) return {result};
    std::wstring extended = path;
    if (path.rfind(L"\\\\?\\", 0) != 0) {
        if (path.rfind(L"\\\\", 0) == 0) extended = L"\\\\?\\UNC\\" + path.substr(2);
        else if (path.size() >= 3 && std::iswalpha(path[0]) != 0 && path[1] == L':' &&
                 (path[2] == L'\\' || path[2] == L'/')) extended = L"\\\\?\\" + path;
    }
    // prevent-shortcut-creation-overwrite: CREATE_NEW is the write boundary;
    // DELETE access permits cleanup of this exact file, never a raced path.
    const HANDLE file = CreateFileW(extended.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {HRESULT_FROM_WIN32(GetLastError())};
    std::array<BYTE, 4096> bytes{};
    for (;;) {
        ULONG read = 0;
        result = stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
        if (FAILED(result)) break;
        if (read > bytes.size()) { result = STG_E_READFAULT; break; }
        DWORD offset = 0;
        while (offset < read) {
            DWORD written = 0;
            if (!WriteFile(file, bytes.data() + offset, read - offset, &written, nullptr)) {
                result = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
            if (written == 0) { result = STG_E_WRITEFAULT; break; }
            offset += written;
        }
        if (FAILED(result) || read == 0 || result == S_FALSE) break;
    }
    HRESULT cleanup = S_OK;
    if (FAILED(result)) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition))) {
            cleanup = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    if (!CloseHandle(file)) {
        const HRESULT closeError = HRESULT_FROM_WIN32(GetLastError());
        if (SUCCEEDED(result)) result = closeError;
        else if (SUCCEEDED(cleanup)) cleanup = closeError;
    }
    return {result, cleanup};
}

}
