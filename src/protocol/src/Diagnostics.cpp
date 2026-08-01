#include "ffprotocol/Diagnostics.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace ffprotocol {
namespace {

std::mutex& LogMutex() {
    static std::mutex mutex;
    return mutex;
}

std::wstring EnvironmentDirectory(const wchar_t* variable) {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetEnvironmentVariableW(variable, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

std::wstring AppDirectory() {
    // Per-user clients keep diagnostics under LOCALAPPDATA. A service token
    // may not receive that user-scoped variable, so fall back to the machine
    // scope rather than silently dropping the only persistent evidence of a
    // raw-volume-open attempt.
    std::wstring base = EnvironmentDirectory(L"LOCALAPPDATA");
    if (base.empty()) {
        base = EnvironmentDirectory(L"PROGRAMDATA");
    }
    return base.empty() ? std::wstring{} : base + L"\\FastFiles";
}

const wchar_t* CategoryName(DiagnosticCategory category) {
    switch (category) {
        case DiagnosticCategory::IndexingError: return L"indexing-error";
        case DiagnosticCategory::InaccessibleDirectory: return L"inaccessible-directory";
        case DiagnosticCategory::VolumeStateTransition: return L"volume-state";
        case DiagnosticCategory::DatabaseError: return L"database-error";
    }
    return L"unknown";
}

std::wstring SafeField(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') result.push_back(L' ');
        else result.push_back(ch);
    }
    return result;
}

} // namespace

std::wstring DiagnosticLogPath() {
    const std::wstring directory = AppDirectory();
    return directory.empty() ? std::wstring{} : directory + L"\\logs\\diagnostics.log";
}

bool AppendDiagnostic(const DiagnosticEvent& event) {
    const std::wstring path = DiagnosticLogPath();
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    if (error) return false;
    std::lock_guard lock(LogMutex());
    std::wofstream output(path, std::ios::app);
    if (!output) return false;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    output << now << L" category=" << CategoryName(event.category)
           << L" path=" << SafeField(event.path)
           << L" volume=" << SafeField(event.volumeId)
           << L" state=" << SafeField(event.state)
           << L" outcome=" << SafeField(event.outcome)
           << L" account=" << SafeField(event.accountName)
           << L" sid=" << SafeField(event.accountSid)
           << L" privilegeHeld=" << (event.privilegeHeld ? 1 : 0)
           << L" privilegeEnabled=" << (event.privilegeEnabled ? 1 : 0)
           << L" error=0x" << std::hex << event.errorCode << std::dec
           << L" items=" << event.itemCount << L"\n";
    output.flush();
    return static_cast<bool>(output);
}

} // namespace ffprotocol
