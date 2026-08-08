#include "EngineDiscovery.h"

#include <iterator>
#include <windows.h>

#include "ffsetup/Identifiers.h"

namespace ffui {

namespace {

std::wstring UiDirectory(const std::wstring& uiExecutablePath) {
    const size_t lastSlash = uiExecutablePath.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return {};
    }
    return uiExecutablePath.substr(0, lastSlash);
}

bool ContainsCandidate(const std::vector<std::wstring>& candidates, const std::wstring& candidate) {
    for (const auto& existing : candidates) {
        if (_wcsicmp(existing.c_str(), candidate.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

void AppendCandidate(std::vector<std::wstring>& candidates, std::wstring candidate) {
    if (!candidate.empty() && !ContainsCandidate(candidates, candidate)) {
        candidates.push_back(std::move(candidate));
    }
}

} // namespace

std::vector<std::wstring> EngineDiscoveryCandidates(const std::wstring& uiExecutablePath) {
    std::vector<std::wstring> candidates;

    const std::wstring uiDir = UiDirectory(uiExecutablePath);
    if (!uiDir.empty()) {
        // 1. Installed layout: all executables staged in one directory.
        AppendCandidate(candidates, uiDir + L"\\" + ffsetup::kEngineExeName);

        // 2. Development layout: the engine lives in the sibling "engine"
        //    directory of the UI's parent (build/<config>/src/ui ->
        //    build/<config>/src/engine). GetFullPathNameW normalizes the
        //    ".." lexically, so the result stays inside the derived tree.
        const std::wstring sibling = uiDir + L"\\..\\engine\\" + ffsetup::kEngineExeName;
        wchar_t normalized[MAX_PATH * 4];
        if (GetFullPathNameW(sibling.c_str(), static_cast<DWORD>(std::size(normalized)), normalized, nullptr) != 0) {
            AppendCandidate(candidates, normalized);
        }
    }

    // 3. Installer layout: %ProgramFiles%\FastFiles.
    wchar_t programFiles[MAX_PATH];
    if (GetEnvironmentVariableW(L"ProgramFiles", programFiles, static_cast<DWORD>(std::size(programFiles))) != 0) {
        AppendCandidate(candidates, std::wstring(programFiles) + L"\\FastFiles\\" + ffsetup::kEngineExeName);
    }

    return candidates;
}

bool IsValidEngineExecutable(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

} // namespace ffui
