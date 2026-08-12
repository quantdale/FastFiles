#include <windows.h>

#include <string>

#include "../TestSupport.h"
#include "ffsetup/PeerImageMatch.h"

namespace {

std::wstring CurrentImagePath() {
    wchar_t buffer[MAX_PATH * 4]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return length == 0 ? std::wstring{} : std::wstring(buffer, length);
}

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

void TestExactAndNamespaceEquivalentPaths() {
    const std::wstring imagePath = CurrentImagePath();
    const std::wstring installDir = DirectoryOf(imagePath);
    const std::wstring fileName = FileNameOf(imagePath);
    fftest::Check(!imagePath.empty() && !installDir.empty() && !fileName.empty(),
                  "peer image fixture resolves the current test image");
    if (imagePath.empty() || installDir.empty() || fileName.empty()) return;

    fftest::Check(ffsetup::IsExpectedInstalledBinary(imagePath, installDir, fileName),
                  "peer image matcher accepts the exact installed image path");
    fftest::Check(ffsetup::IsExpectedInstalledBinary(L"\\\\?\\" + imagePath, installDir, fileName),
                  "peer image matcher treats an extended-length local path as equivalent");
    fftest::Check(!ffsetup::IsExpectedInstalledBinary(imagePath, installDir, L"not-the-test-image.exe"),
                  "peer image matcher rejects a filename mismatch");
    fftest::Check(!ffsetup::IsExpectedInstalledBinary(imagePath, installDir + L"-other", fileName),
                  "peer image matcher rejects a directory mismatch");
}

} // namespace

int wmain() {
    TestExactAndNamespaceEquivalentPaths();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
