#include "ffsetup/PackageTrust.h"

#include <bcrypt.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "ffsetup/AuthenticodeVerification.h"

#pragma comment(lib, "bcrypt.lib")

namespace ffsetup {

namespace {

constexpr size_t kIoBufferBytes = 64 * 1024;

bool IsValidManifestFileName(const std::wstring& fileName) noexcept {
    return !fileName.empty() && fileName != L"." && fileName != L".." &&
           fileName.find_first_of(L"\\/") == std::wstring::npos;
}

int HexValue(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

bool ParseDigest(std::string_view text, Sha256Digest& digest) noexcept {
    if (text.size() != digest.size() * 2) {
        return false;
    }
    for (size_t index = 0; index < digest.size(); ++index) {
        const int high = HexValue(text[index * 2]);
        const int low = HexValue(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        digest[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

bool ParseUnsigned(std::string_view text, uint64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

std::wstring AsciiToWide(std::string_view value) {
    std::wstring result;
    result.reserve(value.size());
    for (const char character : value) {
        if (static_cast<unsigned char>(character) > 0x7F) {
            return {};
        }
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    return result;
}

bool SameIdentity(const PackageFileIdentity& left, const PackageFileIdentity& right) noexcept {
    return left.volumeSerial == right.volumeSerial && left.fileIndexHigh == right.fileIndexHigh &&
           left.fileIndexLow == right.fileIndexLow;
}

std::optional<PackageFileIdentity> GetIdentity(HANDLE handle) noexcept {
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information)) {
        return std::nullopt;
    }
    return PackageFileIdentity{information.dwVolumeSerialNumber, information.nFileIndexHigh,
                               information.nFileIndexLow};
}

std::optional<uint64_t> GetSize(HANDLE handle) noexcept {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(size.QuadPart);
}

std::optional<std::wstring> GetFinalPath(HANDLE handle) {
    std::vector<wchar_t> buffer(512);
    for (size_t attempt = 0; attempt < 4; ++attempt) {
        const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(),
                                                       static_cast<DWORD>(buffer.size()),
                                                       FILE_NAME_NORMALIZED);
        if (length == 0) {
            return std::nullopt;
        }
        if (length < buffer.size()) {
            return std::wstring(buffer.data(), length);
        }
        if (length >= 32768) {
            return std::nullopt;
        }
        buffer.resize(static_cast<size_t>(length) + 1);
    }
    return std::nullopt;
}

std::optional<std::wstring> GetFinalDirectoryPath(const std::wstring& path) {
    HANDLE directory = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                   nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool readable = GetFileInformationByHandle(directory, &information) != FALSE;
    const bool regularDirectory = readable && (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                                  (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    const auto finalPath = regularDirectory ? GetFinalPath(directory) : std::nullopt;
    CloseHandle(directory);
    return finalPath;
}

bool IsWithinDirectory(const std::wstring& directory, const std::wstring& path) noexcept {
    if (_wcsicmp(directory.c_str(), path.c_str()) == 0) {
        return false;
    }
    if (path.size() <= directory.size() || _wcsnicmp(directory.c_str(), path.c_str(), directory.size()) != 0) {
        return false;
    }
    const wchar_t separator = path[directory.size()];
    return separator == L'\\' || separator == L'/';
}

std::optional<Sha256Digest> HashHandle(HANDLE handle) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> hashObject;
    std::optional<Sha256Digest> result;
    do {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
            break;
        }
        DWORD objectLength = 0;
        DWORD propertyBytes = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                              &propertyBytes, 0) != 0 || objectLength == 0) {
            break;
        }
        hashObject.resize(objectLength);
        if (BCryptCreateHash(algorithm, &hash, hashObject.data(), objectLength, nullptr, 0, 0) != 0) {
            break;
        }
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) {
            break;
        }
        std::array<uint8_t, kIoBufferBytes> buffer{};
        bool readSucceeded = true;
        for (;;) {
            DWORD bytesRead = 0;
            if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
                readSucceeded = false;
                break;
            }
            if (bytesRead == 0) {
                break;
            }
            if (BCryptHashData(hash, buffer.data(), bytesRead, 0) != 0) {
                readSucceeded = false;
                break;
            }
        }
        if (!readSucceeded) {
            break;
        }
        Sha256Digest digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0) {
            break;
        }
        result = digest;
    } while (false);

    LARGE_INTEGER zero{};
    SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN);
    if (hash != nullptr) BCryptDestroyHash(hash);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

HANDLE OpenRegularFile(const std::wstring& path) noexcept {
    return CreateFileW(path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                       nullptr);
}

bool IsRegularNonReparseFile(HANDLE handle) noexcept {
    BY_HANDLE_FILE_INFORMATION information{};
    return GetFileInformationByHandle(handle, &information) != FALSE &&
           (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
           (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

bool DefaultSignatureVerifier(const std::wstring& path,
                              const std::vector<Thumbprint>& approvedSigners) noexcept {
    if (approvedSigners.empty()) {
        return false;
    }
    const auto actual = VerifyAuthenticodeAndGetThumbprint(path);
    if (!actual) {
        return false;
    }
    return std::any_of(approvedSigners.begin(), approvedSigners.end(),
                       [&](const Thumbprint& expected) {
                           return !IsPlaceholderThumbprint(expected) &&
                                  ConstantTimeThumbprintEqual(*actual, expected);
                       });
}

bool RunSignatureVerifier(const std::wstring& path, const std::vector<Thumbprint>& approvedSigners,
                          const PackageSignatureVerifier& verifier) noexcept {
    try {
        return verifier ? verifier(path, approvedSigners) : DefaultSignatureVerifier(path, approvedSigners);
    } catch (...) {
        return false;
    }
}

bool SamePathIdentity(const std::wstring& path, const PackageFileIdentity& expected) noexcept {
    HANDLE handle = OpenRegularFile(path);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    const auto actual = GetIdentity(handle);
    const bool same = actual.has_value() && SameIdentity(*actual, expected) && IsRegularNonReparseFile(handle);
    CloseHandle(handle);
    return same;
}

struct RawInspection {
    HANDLE handle = INVALID_HANDLE_VALUE;
    PackageFileIdentity identity{};
    Sha256Digest sha256{};
    uint64_t sizeBytes = 0;
    std::wstring finalPath;
};

std::optional<RawInspection> InspectPath(const std::wstring& path,
                                          const PackageManifestEntry& expected,
                                          const std::vector<Thumbprint>& approvedSigners,
                                          const PackageSignatureVerifier& verifier) noexcept {
    HANDLE handle = OpenRegularFile(path);
    if (handle == INVALID_HANDLE_VALUE || !IsRegularNonReparseFile(handle)) {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        return std::nullopt;
    }
    const auto identity = GetIdentity(handle);
    const auto size = GetSize(handle);
    const auto finalPath = GetFinalPath(handle);
    const auto digest = HashHandle(handle);
    if (!identity || !size || !finalPath || !digest || *size != expected.sizeBytes ||
        *digest != expected.sha256 || !RunSignatureVerifier(*finalPath, approvedSigners, verifier)) {
        CloseHandle(handle);
        return std::nullopt;
    }

    // The signer API is path-oriented. Re-open the originally requested path
    // after verification and require the same underlying file ID, so a
    // delete/rename/substitution race cannot make the signature refer to a
    // different object than the handle that will be consumed.
    if (!SamePathIdentity(path, *identity)) {
        CloseHandle(handle);
        return std::nullopt;
    }
    const auto afterIdentity = GetIdentity(handle);
    const auto afterDigest = HashHandle(handle);
    if (!afterIdentity || !afterDigest || !SameIdentity(*afterIdentity, *identity) || *afterDigest != *digest) {
        CloseHandle(handle);
        return std::nullopt;
    }
    return RawInspection{handle, *identity, *digest, *size, *finalPath};
}

bool DestinationParentIsSafe(const std::wstring& path) noexcept {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash == 0) {
        return false;
    }
    return GetFinalDirectoryPath(path.substr(0, slash)).has_value();
}

bool ExistingDestinationIsSafe(const std::wstring& path) noexcept {
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool safe = GetFileInformationByHandle(handle, &information) != FALSE &&
                      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    CloseHandle(handle);
    return safe;
}

} // namespace

std::optional<PackageManifest> ReadPackageManifest(const std::wstring& path) noexcept {
    try {
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        PackageManifest manifest;
        std::string line;
        bool sawHeader = false;
        bool sawVersion = false;
        bool sawProductVersion = false;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!sawHeader && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
            if (line.empty()) continue;
            if (!sawHeader) {
                if (line != "FASTFILES_PACKAGE_MANIFEST_V1") return std::nullopt;
                sawHeader = true;
                continue;
            }
            if (line.rfind("version=", 0) == 0) {
                uint64_t version = 0;
                if (sawVersion || !ParseUnsigned(std::string_view(line).substr(8), version) || version > UINT32_MAX) {
                    return std::nullopt;
                }
                manifest.schemaVersion = static_cast<uint32_t>(version);
                sawVersion = true;
                continue;
            }
            if (line.rfind("productVersion=", 0) == 0) {
                if (sawProductVersion) return std::nullopt;
                manifest.productVersion = AsciiToWide(std::string_view(line).substr(15));
                if (manifest.productVersion.empty()) return std::nullopt;
                sawProductVersion = true;
                continue;
            }
            if (line.rfind("file=", 0) == 0) {
                const std::string_view payload = std::string_view(line).substr(5);
                const size_t firstSeparator = payload.find('|');
                const size_t secondSeparator = payload.find('|', firstSeparator == std::string_view::npos ? 0 : firstSeparator + 1);
                if (firstSeparator == std::string_view::npos || secondSeparator == std::string_view::npos ||
                    payload.find('|', secondSeparator + 1) != std::string_view::npos) {
                    return std::nullopt;
                }
                const std::wstring fileName = AsciiToWide(payload.substr(0, firstSeparator));
                uint64_t sizeBytes = 0;
                Sha256Digest digest{};
                if (!IsValidManifestFileName(fileName) ||
                    !ParseUnsigned(payload.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1), sizeBytes) ||
                    !ParseDigest(payload.substr(secondSeparator + 1), digest) ||
                    FindPackageManifestEntry(manifest, fileName) != nullptr) {
                    return std::nullopt;
                }
                manifest.files.push_back(PackageManifestEntry{fileName, sizeBytes, digest});
                continue;
            }
            return std::nullopt;
        }
        if (!sawHeader || !sawVersion || !sawProductVersion || manifest.schemaVersion != 1 || manifest.files.empty()) {
            return std::nullopt;
        }
        return manifest;
    } catch (...) {
        return std::nullopt;
    }
}

const PackageManifestEntry* FindPackageManifestEntry(const PackageManifest& manifest,
                                                     const std::wstring& fileName) noexcept {
    const auto found = std::find_if(manifest.files.begin(), manifest.files.end(),
                                    [&](const PackageManifestEntry& entry) {
                                        return _wcsicmp(entry.fileName.c_str(), fileName.c_str()) == 0;
                                    });
    return found == manifest.files.end() ? nullptr : &*found;
}

bool HasExactPackageFileSet(const PackageManifest& manifest,
                            const std::vector<std::wstring>& expectedNames) noexcept {
    if (manifest.files.size() != expectedNames.size()) return false;
    return std::all_of(expectedNames.begin(), expectedNames.end(), [&](const std::wstring& name) {
        return FindPackageManifestEntry(manifest, name) != nullptr;
    });
}

bool ValidateTrustedPackageSet(const std::wstring& packageRoot, const PackageManifest& manifest,
                              const std::vector<std::wstring>& expectedNames,
                              const PackageSignerSelector& signerSelector,
                              const PackageSignatureVerifier& verifier) noexcept {
    if (!signerSelector || !HasExactPackageFileSet(manifest, expectedNames)) return false;
    try {
        for (const auto& name : expectedNames) {
            const auto* expected = FindPackageManifestEntry(manifest, name);
            if (expected == nullptr ||
                !OpenTrustedPackageFile(packageRoot, name, *expected, signerSelector(name), verifier).has_value()) {
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<Sha256Digest> ComputeSha256FileAtPath(const std::wstring& path) noexcept {
    HANDLE handle = OpenRegularFile(path);
    if (handle == INVALID_HANDLE_VALUE || !IsRegularNonReparseFile(handle)) {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        return std::nullopt;
    }
    const auto digest = HashHandle(handle);
    CloseHandle(handle);
    return digest;
}

TrustedPackageFile::~TrustedPackageFile() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
}

TrustedPackageFile::TrustedPackageFile(TrustedPackageFile&& other) noexcept
    : handle_(other.handle_), identity_(other.identity_), sha256_(other.sha256_), sizeBytes_(other.sizeBytes_),
      finalPath_(std::move(other.finalPath_)) {
    other.handle_ = INVALID_HANDLE_VALUE;
}

TrustedPackageFile& TrustedPackageFile::operator=(TrustedPackageFile&& other) noexcept {
    if (this != &other) {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = other.handle_;
        identity_ = other.identity_;
        sha256_ = other.sha256_;
        sizeBytes_ = other.sizeBytes_;
        finalPath_ = std::move(other.finalPath_);
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
}

std::optional<TrustedPackageFile> OpenTrustedPackageFile(
    const std::wstring& packageRoot, const std::wstring& fileName,
    const PackageManifestEntry& expected, const std::vector<Thumbprint>& approvedSigners,
    const PackageSignatureVerifier& verifier) noexcept {
    if (!IsValidManifestFileName(fileName) || _wcsicmp(fileName.c_str(), expected.fileName.c_str()) != 0) {
        return std::nullopt;
    }
    const auto finalRoot = GetFinalDirectoryPath(packageRoot);
    if (!finalRoot) return std::nullopt;
    const std::wstring path = packageRoot + L"\\" + fileName;
    auto inspection = InspectPath(path, expected, approvedSigners, verifier);
    if (!inspection || !IsWithinDirectory(*finalRoot, inspection->finalPath)) {
        if (inspection) CloseHandle(inspection->handle);
        return std::nullopt;
    }
    TrustedPackageFile trusted;
    trusted.handle_ = inspection->handle;
    trusted.identity_ = inspection->identity;
    trusted.sha256_ = inspection->sha256;
    trusted.sizeBytes_ = inspection->sizeBytes;
    trusted.finalPath_ = std::move(inspection->finalPath);
    return trusted;
}

bool CopyTrustedPackageFile(TrustedPackageFile& source, const std::wstring& destinationPath) noexcept {
    if (source.Handle() == INVALID_HANDLE_VALUE) return false;
    HANDLE destination = CreateFileW(destinationPath.c_str(), GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH,
                                     nullptr);
    if (destination == INVALID_HANDLE_VALUE) return false;

    bool copied = false;
    do {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(source.Handle(), zero, nullptr, FILE_BEGIN)) break;
        std::array<uint8_t, kIoBufferBytes> buffer{};
        bool readSucceeded = true;
        for (;;) {
            DWORD bytesRead = 0;
            if (!ReadFile(source.Handle(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
                readSucceeded = false;
                break;
            }
            if (bytesRead == 0) break;
            DWORD offset = 0;
            while (offset < bytesRead) {
                DWORD bytesWritten = 0;
                if (!WriteFile(destination, buffer.data() + offset, bytesRead - offset, &bytesWritten, nullptr) ||
                    bytesWritten == 0) {
                    readSucceeded = false;
                    break;
                }
                offset += bytesWritten;
            }
            if (!readSucceeded) break;
        }
        copied = readSucceeded && FlushFileBuffers(destination) != FALSE;
    } while (false);
    CloseHandle(destination);

    const auto identity = GetIdentity(source.Handle());
    const auto digest = HashHandle(source.Handle());
    if (!identity || !digest || !SameIdentity(*identity, source.Identity()) || *digest != source.Sha256()) {
        copied = false;
    }
    if (!copied) DeleteFileW(destinationPath.c_str());
    return copied;
}

bool VerifyPackageFileAtPath(const std::wstring& path, const PackageManifestEntry& expected,
                             const std::vector<Thumbprint>& approvedSigners,
                             const PackageSignatureVerifier& verifier) noexcept {
    auto inspection = InspectPath(path, expected, approvedSigners, verifier);
    if (!inspection) return false;
    CloseHandle(inspection->handle);
    return true;
}

bool PromoteVerifiedPackageFile(const std::wstring& stagedPath, const std::wstring& destinationPath,
                                const PackageManifestEntry& expected,
                                const std::vector<Thumbprint>& approvedSigners,
                                const PackageSignatureVerifier& verifier) noexcept {
    if (!VerifyPackageFileAtPath(stagedPath, expected, approvedSigners, verifier)) {
        DeleteFileW(stagedPath.c_str());
        return false;
    }
    if (!DestinationParentIsSafe(destinationPath) || !ExistingDestinationIsSafe(destinationPath)) {
        DeleteFileW(stagedPath.c_str());
        return false;
    }
    // The scratch directory and install directory are both expected to live
    // on the system volume. Omitting MOVEFILE_COPY_ALLOWED is intentional:
    // Windows must not silently downgrade this handle-bound promotion into a
    // path-based copy/delete operation.
    if (!MoveFileExW(stagedPath.c_str(), destinationPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(stagedPath.c_str());
        return false;
    }
    if (!VerifyPackageFileAtPath(destinationPath, expected, approvedSigners, verifier)) {
        DeleteFileW(destinationPath.c_str());
        return false;
    }
    return true;
}

std::vector<Thumbprint> ApprovedPackageSigners() noexcept {
    std::vector<Thumbprint> result;
    if (!IsPlaceholderThumbprint(kExpectedEngineSignatureThumbprint)) {
        result.push_back(kExpectedEngineSignatureThumbprint);
    }
    if (!IsPlaceholderThumbprint(kExpectedIndexSvcSignatureThumbprint) &&
        std::none_of(result.begin(), result.end(), [&](const Thumbprint& value) {
            return ConstantTimeThumbprintEqual(value, kExpectedIndexSvcSignatureThumbprint);
        })) {
        result.push_back(kExpectedIndexSvcSignatureThumbprint);
    }
    return result;
}

} // namespace ffsetup
