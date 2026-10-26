#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <windows.h>

#include "ffprotocol/Diagnostics.h"
#include "ffprotocol/UiProtocol.h"
#include "../TestSupport.h"
#include "../../src/ui/src/ShutdownDiagnostics.h"

using namespace fftest;

int main() {
    Check(std::wstring(ffprotocol::DiagnosticCodeName(
              ffprotocol::DiagnosticCode::FFDIAG_UI_SHUTDOWN_PHASE))
              == L"FFDIAG_UI_SHUTDOWN_PHASE",
          "shutdown phase diagnostic keeps its stable append-only name");
    ffprotocol::DiagnosticEvent shutdown{};
    shutdown.category = ffprotocol::DiagnosticCategory::UiInteraction;
    shutdown.code = ffprotocol::DiagnosticCode::FFDIAG_UI_SHUTDOWN_PHASE;
    shutdown.state = L"engine-client-stop";
    shutdown.outcome = L"begin";
    const auto shutdownJson = ffprotocol::SerializeDiagnosticEvent(shutdown, ffprotocol::DiagnosticFormat::Json);
    Check(shutdownJson.find(L"engine-client-stop") != std::wstring::npos &&
          shutdownJson.find(L"begin") != std::wstring::npos &&
          shutdownJson.find(L"\"pid\":") != std::wstring::npos &&
          shutdownJson.find(L"\"timestamp\":") != std::wstring::npos,
          "shutdown phase serialization identifies phase, outcome, process and time");
    Check(std::wstring(ffprotocol::DiagnosticCodeName(
              ffprotocol::DiagnosticCode::FFDIAG_UI_RESULT_SUPERSEDED))
              == L"FFDIAG_UI_RESULT_SUPERSEDED",
          "superseded-result diagnostic keeps its stable append-only name");
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
    const DWORD oldShutdownLength = GetEnvironmentVariableW(L"FASTFILES_UIA_DIAGNOSTICS", nullptr, 0);
    std::wstring oldShutdownValue(oldShutdownLength, L'\0');
    if (oldShutdownLength != 0) {
        GetEnvironmentVariableW(L"FASTFILES_UIA_DIAGNOSTICS", oldShutdownValue.data(), oldShutdownLength);
    }
    SetEnvironmentVariableW(L"FASTFILES_UIA_DIAGNOSTICS", L"0");
    ffui::AppendShutdownPhase(L"test-phase-disabled", L"begin");
    Check(!std::filesystem::exists(ffprotocol::DiagnosticLogPath()), "shutdown phases are opt-in");
    SetEnvironmentVariableW(L"FASTFILES_UIA_DIAGNOSTICS", L"1");
    ffui::AppendShutdownPhase(L"test-phase-enabled", L"begin");
    SetEnvironmentVariableW(L"FASTFILES_UIA_DIAGNOSTICS",
                            oldShutdownLength == 0 ? nullptr : oldShutdownValue.c_str());

    const ffprotocol::DiagnosticEvent event{
        ffprotocol::DiagnosticCategory::InaccessibleDirectory,
        ffprotocol::DiagnosticCode::FFDIAG_DIRECTORY_ACCESS_DENIED,
        L"C:\\private\\folder", L"C:", L"access-denied", ERROR_ACCESS_DENIED, 3};
    Check(ffprotocol::AppendDiagnostic(event), "diagnostic event is appended");
    std::wifstream input(ffprotocol::DiagnosticLogPath());
    std::wstring contents((std::istreambuf_iterator<wchar_t>(input)), {});
    Check(contents.find(L"test-phase-enabled") != std::wstring::npos &&
          contents.find(L"test-phase-disabled") == std::wstring::npos,
          "enabled shutdown helper appends only the opted-in phase");
    Check(contents.find(L"category=inaccessible-directory") != std::wstring::npos, "category is logged");
    Check(contents.find(L"code=11") != std::wstring::npos, "structured numeric code is logged");
    Check(contents.find(L"codeName=FFDIAG_DIRECTORY_ACCESS_DENIED") != std::wstring::npos,
          "structured code name is logged");
    Check(contents.find(L"path=C:\\private\\folder") != std::wstring::npos, "path metadata is logged");
    Check(contents.find(L"access-denied") != std::wstring::npos, "state metadata is logged");
    Check(contents.find(L"secret file contents") == std::wstring::npos, "file contents are not accepted by the event shape");

    // A record is written by one WriteFile so the cross-process append stays
    // atomic, which means an over-long record is dropped. The size cap must
    // therefore sit above the largest legitimate record, or a long-path
    // diagnostic disappears with no trace -- every caller discards this
    // return value. kMaxPathLengthChars costs up to 3 UTF-8 bytes per code
    // unit, and up to 6 once JSON-escaped.
    ffprotocol::DiagnosticEvent longPathEvent = event;
    longPathEvent.path.assign(ffprotocol::kMaxPathLengthChars, L'é');
    Check(ffprotocol::AppendDiagnostic(longPathEvent),
          "a maximum-length path diagnostic is still recorded");
    Check(ffprotocol::AppendDiagnostic(longPathEvent, ffprotocol::DiagnosticFormat::Json),
          "a maximum-length path diagnostic is still recorded in JSON form");
    Check(ffprotocol::SerializeDiagnosticEvent(longPathEvent).find(longPathEvent.path) != std::wstring::npos,
          "the maximum-length path survives serialization unabridged");

    ffprotocol::DiagnosticEvent jsonEvent = event;
    jsonEvent.state = L"quoted \"message\"\nwith control";
    jsonEvent.signatureThumbprint = L"0123456789ABCDEF0123456789ABCDEF01234567";
    const std::wstring json = ffprotocol::SerializeDiagnosticEvent(jsonEvent, ffprotocol::DiagnosticFormat::Json);
    Check(json.find(L"\"code\":11") != std::wstring::npos, "JSON includes the structured integer code");
    Check(json.find(L"\"codeName\":\"FFDIAG_DIRECTORY_ACCESS_DENIED\"") != std::wstring::npos,
          "JSON includes the stable structured code name");
    Check(json.find(L"\"message\":\"quoted \\\"message\\\"\\nwith control\"") != std::wstring::npos,
          "JSON escapes the human-readable message");
    Check(json.find(L"\"signatureThumbprint\":\"0123456789ABCDEF0123456789ABCDEF01234567\"") != std::wstring::npos,
          "JSON records the signer thumbprint metadata");
    Check(ffprotocol::AppendDiagnostic(jsonEvent, ffprotocol::DiagnosticFormat::Json),
          "JSON diagnostic stream append succeeds");

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
