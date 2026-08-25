#include "../TestSupport.h"

#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

#include "ffui/DisplayPath.h"
#include "NavigationChrome.h"

int main() {
    size_t resolverCalls = 0;
    const auto resolve = [&resolverCalls](const std::wstring& path) {
        ++resolverCalls;
        if (path == L"C:\\Users\\MICHAE~1\\Documents") {
            return std::wstring(L"C:\\Users\\Michael Roy\\Documents");
        }
        if (path == L"C:\\Users\\MICHAE~1\\\u65E5\u672C\u8A9E\U0001F600") {
            return std::wstring(L"C:\\Users\\Michael Roy\\\u65E5\u672C\u8A9E\U0001F600");
        }
        if (path == L"\\\\?\\C:\\Users\\MICHAE~1\\Long") {
            return std::wstring(L"\\\\?\\C:\\Users\\Michael Roy\\Long");
        }
        return std::wstring{};
    };

    fftest::Check(
        ffui::NormalizeDisplayPathWithResolver(L"C:\\Users\\Michael Roy\\Documents", resolve) ==
            L"C:\\Users\\Michael Roy\\Documents",
        "ordinary long path remains unchanged");
    fftest::Check(resolverCalls == 0, "ordinary local path does not invoke the resolver");
    fftest::Check(
        ffui::NormalizeDisplayPathWithResolver(L"C:\\Users\\MICHAE~1\\Documents", resolve) ==
            L"C:\\Users\\Michael Roy\\Documents",
        "resolvable 8.3 path uses long display form");
    fftest::Check(
        ffui::NormalizeDisplayPathWithResolver(L"C:\\Users\\MICHAE~1\\Unavailable", resolve) ==
            L"C:\\Users\\MICHAE~1\\Unavailable",
        "unavailable 8.3 path falls back without changing identity");
    fftest::Check(
        ffui::NormalizeDisplayPathWithResolver(L"C:\\Users\\MICHAE~1\\\u65E5\u672C\u8A9E\U0001F600", resolve) ==
            L"C:\\Users\\Michael Roy\\\u65E5\u672C\u8A9E\U0001F600",
        "Unicode-containing path uses the resolved long display form");
    fftest::Check(
        ffui::NormalizeDisplayPathWithResolver(L"\\\\?\\C:\\Users\\MICHAE~1\\Long", resolve) ==
            L"C:\\Users\\Michael Roy\\Long",
        "long-path prefix is removed only from display text");
    fftest::Check(
        ffui::NormalizeDisplayPathWithResolver(L"C:\\", resolve) == L"C:\\",
        "drive root remains a root path");
    fftest::Check(
        ffui::NormalizeDisplayPathWithResolver(L"\\\\server\\share\\folder", resolve) ==
            L"\\\\server\\share\\folder",
        "UNC path remains intact when no long form is available");

    const size_t callsBeforeUnc = resolverCalls;
    fftest::Check(
        ffui::NormalizeDisplayPathWithResolver(L"\\\\server\\share\\MICHAE~1\\folder", resolve) ==
            L"\\\\server\\share\\MICHAE~1\\folder",
        "UNC short-name text is preserved without resolution");
    fftest::Check(resolverCalls == callsBeforeUnc, "UNC display normalization performs no resolver I/O");
    fftest::Check(
        ffui::NormalizeDisplayPathWithResolver(L"\\\\?\\UNC\\server\\share\\MICHAE~1\\folder", resolve) ==
            L"\\\\server\\share\\MICHAE~1\\folder",
        "extended UNC prefix is stripped without resolution");
    fftest::Check(resolverCalls == callsBeforeUnc, "extended UNC display normalization performs no resolver I/O");
    fftest::Check(
        ffui::NormalizeDisplayPathWithResolver(L"\\\\.\\PIPE\\MICHAE~1", resolve) ==
            L"\\\\.\\PIPE\\MICHAE~1",
        "device namespace text is not treated as a filesystem path");

    const std::wstring canonical = L"\\\\?\\C:\\Users\\MICHAE~1\\Long";
    const ffui::NavigationLocation location = ffui::NavigationLocation::FromPath(canonical);
    fftest::Check(location.path == canonical, "display normalization leaves canonical identity byte-for-byte unchanged");
    fftest::Check(ffui::TabLabelForLocation(2, location) == L"2: Long", "tab label uses a display-only path leaf");
    fftest::Check(ffui::RawDisplayPathLeaf(canonical) == L"Long", "raw leaf extraction preserves canonical path semantics");
    fftest::Check(
        ffui::NormalizePathDerivedDisplayLabel(L"MICHAE~1", L"\\\\server\\share\\MICHAE~1") == L"MICHAE~1",
        "unavailable derived label falls back without changing identity");

    // A real short-name check is conditional because modern Windows volumes
    // may have 8.3 generation disabled. The fixture is heap-backed so it
    // cannot recreate the historical /analyze C6262 stack warning.
    std::error_code error;
    const std::filesystem::path temporaryRoot =
        std::filesystem::temp_directory_path(error) /
        (L"FastFilesDisplayPathTest_" + std::to_wstring(GetCurrentProcessId()));
    if (!error && std::filesystem::create_directories(temporaryRoot, error)) {
        const std::filesystem::path longFolder = temporaryRoot / L"FastFilesLongDisplayFolder";
        if (std::filesystem::create_directory(longFolder, error)) {
            std::vector<wchar_t> shortBuffer(32768);
            const std::wstring longFolderText = longFolder.wstring();
            const DWORD shortLength = GetShortPathNameW(longFolderText.c_str(), shortBuffer.data(),
                                                        static_cast<DWORD>(shortBuffer.size()));
            if (shortLength > 0 && shortLength < shortBuffer.size()) {
                const std::wstring shortFolderText(shortBuffer.data(), shortLength);
                if (shortFolderText.find(L'~') != std::wstring::npos) {
                    const std::wstring realDisplay = ffui::NormalizeDisplayPath(shortFolderText);
                    fftest::Check(realDisplay.find(L"FastFilesLongDisplayFolder") != std::wstring::npos,
                                  "real local 8.3 alias expands for display");
                    const std::wstring shortLeaf = ffui::RawDisplayPathLeaf(shortFolderText);
                    fftest::Check(
                        ffui::NormalizePathDerivedDisplayLabel(shortLeaf, shortFolderText) ==
                            ffui::DisplayPathLeaf(shortFolderText),
                        "path-derived Home/sidebar labels use the expanded local leaf");
                }
            }
        }
        std::filesystem::remove_all(temporaryRoot, error);
    }

    return fftest::FailureCount() == 0 ? 0 : 1;
}
