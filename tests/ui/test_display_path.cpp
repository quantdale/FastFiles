#include "../TestSupport.h"

#include <string>

#include "ffui/DisplayPath.h"

int main() {
    const auto resolve = [](const std::wstring& path) {
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

    return fftest::FailureCount() == 0 ? 0 : 1;
}
