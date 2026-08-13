#include "EngineDiscovery.h"

#include <cwchar>
#include <iterator>
#include <utility>
#include <vector>
#include <windows.h>

#include "ffsetup/AuthenticodeVerification.h"
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

std::wstring NormalizePathForComparison(const std::wstring& path) {
    std::wstring normalized = path;
    if (normalized.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        normalized = L"\\\\" + normalized.substr(8);
    } else if (normalized.rfind(L"\\\\?\\", 0) == 0) {
        normalized.erase(0, 4);
    }
    while (normalized.size() > 3 && (normalized.back() == L'\\' || normalized.back() == L'/')) {
        normalized.pop_back();
    }
    return normalized;
}

std::optional<std::wstring> FullPath(const std::wstring& path) {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0) {
        return std::nullopt;
    }
    if (length >= buffer.size()) {
        buffer.resize(static_cast<size_t>(length) + 1);
        length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length == 0 || length >= buffer.size()) {
            return std::nullopt;
        }
    }
    return std::wstring(buffer.data(), length);
}

std::wstring LongPathOrOriginal(const std::wstring& path) {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetLongPathNameW(path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        return path;
    }
    if (length >= buffer.size()) {
        buffer.resize(static_cast<size_t>(length) + 1);
        length = GetLongPathNameW(path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return path;
        }
    }
    return std::wstring(buffer.data(), length);
}

std::optional<std::wstring> CanonicalPathIfUnchanged(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return std::nullopt;
    }

    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    std::vector<wchar_t> buffer(32768);
    DWORD length = GetFinalPathNameByHandleW(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                                             FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length >= buffer.size() && length != 0) {
        buffer.resize(static_cast<size_t>(length) + 1);
        length = GetFinalPathNameByHandleW(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                                           FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    }
    CloseHandle(file);
    if (length == 0 || length >= buffer.size()) {
        return std::nullopt;
    }

    const auto fullPath = FullPath(path);
    if (!fullPath.has_value() || _wcsicmp(NormalizePathForComparison(LongPathOrOriginal(*fullPath)).c_str(),
                                          NormalizePathForComparison(std::wstring(buffer.data(), length)).c_str()) != 0) {
        return std::nullopt;
    }
    return std::wstring(buffer.data(), length);
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

EngineExecutableValidation ValidateEngineExecutable(const std::wstring& path) {
    EngineExecutableValidation result;
    if (!IsValidEngineExecutable(path)) {
        return result;
    }

    const auto canonicalPath = CanonicalPathIfUnchanged(path);
    if (!canonicalPath.has_value()) {
        result.status = EngineExecutableValidationStatus::CanonicalPathRejected;
        return result;
    }
    result.canonicalPath = *canonicalPath;

    result.signerThumbprint = ffsetup::VerifyAuthenticodeAndGetThumbprintAtPath(path);
    if (ffsetup::VerifyPinnedSignature(path, ffsetup::kExpectedEngineSignatureThumbprint)) {
        result.status = EngineExecutableValidationStatus::Accepted;
    } else if (!result.signerThumbprint.has_value()) {
        result.status = EngineExecutableValidationStatus::Unsigned;
    } else if (ffsetup::IsPlaceholderThumbprint(ffsetup::kExpectedEngineSignatureThumbprint)) {
        result.status = EngineExecutableValidationStatus::PinsUnconfigured;
    } else {
        result.status = EngineExecutableValidationStatus::SignatureMismatch;
    }
    return result;
}

const wchar_t* EngineExecutableValidationStatusName(EngineExecutableValidationStatus status) noexcept {
    switch (status) {
        case EngineExecutableValidationStatus::Accepted: return L"accepted";
        case EngineExecutableValidationStatus::NotRegularFile: return L"not-regular-file";
        case EngineExecutableValidationStatus::CanonicalPathRejected: return L"canonical-path-rejected";
        case EngineExecutableValidationStatus::Unsigned: return L"unsigned";
        case EngineExecutableValidationStatus::SignatureMismatch: return L"signature-mismatch";
        case EngineExecutableValidationStatus::PinsUnconfigured: return L"pins-unconfigured";
    }
    return L"unknown";
}

} // namespace ffui
