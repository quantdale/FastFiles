#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <windows.h>

#include "ffprotocol/Diagnostics.h"
#include "../TestSupport.h"

using namespace fftest;

int main() {
    wchar_t temp[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, temp) != 0, "temporary path is available");
    const std::filesystem::path localAppData = std::filesystem::path(temp) /
        (L"FastFiles-diagnostics-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(localAppData, error);
    const DWORD oldLength = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    std::wstring oldValue(oldLength, L'\0');
    if (oldLength != 0) GetEnvironmentVariableW(L"LOCALAPPDATA", oldValue.data(), oldLength);
    const DWORD oldProgramDataLength = GetEnvironmentVariableW(L"PROGRAMDATA", nullptr, 0);
    std::wstring oldProgramData(oldProgramDataLength, L'\0');
    if (oldProgramDataLength != 0) {
        GetEnvironmentVariableW(L"PROGRAMDATA", oldProgramData.data(), oldProgramDataLength);
    }
    SetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.c_str());

    const ffprotocol::DiagnosticEvent event{
        ffprotocol::DiagnosticCategory::InaccessibleDirectory,
        L"C:\\private\\folder", L"C:", L"access-denied", ERROR_ACCESS_DENIED, 3};
    Check(ffprotocol::AppendDiagnostic(event), "diagnostic event is appended");
    std::wifstream input(ffprotocol::DiagnosticLogPath());
    std::wstring contents((std::istreambuf_iterator<wchar_t>(input)), {});
    Check(contents.find(L"category=inaccessible-directory") != std::wstring::npos, "category is logged");
    Check(contents.find(L"path=C:\\private\\folder") != std::wstring::npos, "path metadata is logged");
    Check(contents.find(L"access-denied") != std::wstring::npos, "state metadata is logged");
    Check(contents.find(L"secret file contents") == std::wstring::npos, "file contents are not accepted by the event shape");

    const std::filesystem::path fallbackAppData = std::filesystem::path(temp) /
        (L"FastFiles-diagnostics-fallback-test-" + std::to_wstring(GetCurrentProcessId()));
    SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
    SetEnvironmentVariableW(L"PROGRAMDATA", fallbackAppData.c_str());
    Check(ffprotocol::AppendDiagnostic(event), "machine-scope diagnostic fallback is available");
    Check(ffprotocol::DiagnosticLogPath().find(fallbackAppData.wstring()) != std::wstring::npos,
          "machine-scope fallback path is selected when LOCALAPPDATA is unavailable");

    // settings-and-appearance 8.3/8.4: redacted bundle export omits literal
    // paths; opt-in mode includes them; both modes never contain content.
    SetEnvironmentVariableW(L"PROGRAMDATA", fallbackAppData.c_str());
    SetEnvironmentVariableW(L"LOCALAPPDATA", fallbackAppData.c_str());
    const std::filesystem::path bundleRedacted = fallbackAppData / L"bundle-redacted.txt";
    const std::filesystem::path bundleLiteral = fallbackAppData / L"bundle-literal.txt";
    Check(ffprotocol::ExportDiagnosticBundle(bundleRedacted.wstring(), false),
          "redacted bundle export succeeds");
    std::wifstream redactedInput(bundleRedacted);
    std::wstring redacted((std::istreambuf_iterator<wchar_t>(redactedInput)), {});
    Check(redacted.find(L"Per-category event counts") != std::wstring::npos, "bundle contains category aggregates");
    Check(redacted.find(L"inaccessible-directory") != std::wstring::npos, "bundle contains category names");
    Check(redacted.find(L"C:\\private\\folder") == std::wstring::npos,
          "redacted bundle omits literal paths");
    Check(redacted.find(L"access-denied") != std::wstring::npos, "bundle retains state metadata");

    Check(ffprotocol::ExportDiagnosticBundle(bundleLiteral.wstring(), true),
          "literal-path bundle export succeeds");
    std::wifstream literalInput(bundleLiteral);
    std::wstring literal((std::istreambuf_iterator<wchar_t>(literalInput)), {});
    Check(literal.find(L"C:\\private\\folder") != std::wstring::npos,
          "opt-in bundle includes literal paths");
    Check(literal.find(L"secret file contents") == std::wstring::npos,
          "literal-path bundle still excludes file content");

    std::filesystem::remove_all(localAppData, error);
    std::filesystem::remove_all(fallbackAppData, error);
    if (oldLength == 0) SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
    else SetEnvironmentVariableW(L"LOCALAPPDATA", oldValue.c_str());
    if (oldProgramDataLength == 0) SetEnvironmentVariableW(L"PROGRAMDATA", nullptr);
    else SetEnvironmentVariableW(L"PROGRAMDATA", oldProgramData.c_str());
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
