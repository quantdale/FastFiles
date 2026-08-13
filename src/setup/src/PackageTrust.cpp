#include "ffsetup/PackageTrust.h"

#include <bcrypt.h>
#include <wincrypt.h>
#include <winver.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>

#include "ffsetup/AuthenticodeVerification.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "version.lib")

namespace ffsetup {

namespace {

constexpr size_t kIoBufferBytes = 64 * 1024;
constexpr uint64_t kMaxManifestBytes = 1024 * 1024;

void CloseHandleIfValid(HANDLE handle) noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
}

bool IsValidManifestFileName(const std::wstring& fileName) noexcept {
    return !fileName.empty() && fileName != L"." && fileName != L".." &&
           fileName.find_first_of(L"\\/") == std::wstring::npos;
}

bool IsValidAsciiField(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const unsigned char character : value) {
        if (character < 0x21 || character > 0x7E || character == '|') {
            return false;
        }
    }
    return true;
}

std::string WideToAscii(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        if (character < 0x21 || character > 0x7E || character == L'|') {
            return {};
        }
        result.push_back(static_cast<char>(character));
    }
    return result;
}

std::wstring AsciiToWide(std::string_view value) {
    if (!IsValidAsciiField(value)) {
        return {};
    }
    std::wstring result;
    result.reserve(value.size());
    for (const char character : value) {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    return result;
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

bool IsHexIdentity(std::string_view text) noexcept {
    if (text.size() != 40) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](const char value) { return HexValue(value) >= 0; });
}

std::vector<std::string_view> SplitFields(std::string_view value) {
    std::vector<std::string_view> fields;
    size_t start = 0;
    for (;;) {
        const size_t separator = value.find('|', start);
        if (separator == std::string_view::npos) {
            fields.push_back(value.substr(start));
            return fields;
        }
        fields.push_back(value.substr(start, separator - start));
        start = separator + 1;
    }
}

std::optional<std::array<uint32_t, 3>> ParseProductVersion(const std::wstring& value) noexcept {
    const size_t first = value.find(L'.');
    const size_t second = first == std::wstring::npos ? std::wstring::npos : value.find(L'.', first + 1);
    if (first == std::wstring::npos || second == std::wstring::npos || value.find(L'.', second + 1) != std::wstring::npos ||
        first == 0 || second == first + 1 || second + 1 >= value.size()) {
        return std::nullopt;
    }
    std::array<uint32_t, 3> components{};
    const std::wstring parts[] = {value.substr(0, first), value.substr(first + 1, second - first - 1),
                                  value.substr(second + 1)};
    for (size_t index = 0; index < std::size(parts); ++index) {
        if (parts[index].size() > 1 && parts[index].front() == L'0') {
            return std::nullopt;
        }
        uint32_t parsed = 0;
        for (const wchar_t character : parts[index]) {
            if (character < L'0' || character > L'9') {
                return std::nullopt;
            }
            const uint32_t digit = static_cast<uint32_t>(character - L'0');
            if (parsed > 65535U / 10U || (parsed == 65535U / 10U && digit > 65535U % 10U)) {
                return std::nullopt;
            }
            parsed = parsed * 10U + digit;
        }
        components[index] = parsed;
    }
    return components;
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
                                                       static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
        if (length == 0 || length >= 32768) {
            return std::nullopt;
        }
        if (length < buffer.size()) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(static_cast<size_t>(length) + 1);
    }
    return std::nullopt;
}

std::optional<std::wstring> GetFinalDirectoryPath(const std::wstring& path) {
    HANDLE directory = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool readable = GetFileInformationByHandle(directory, &information) != FALSE;
    const bool regularDirectory = readable && (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                                  (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    const auto finalPath = regularDirectory ? GetFinalPath(directory) : std::nullopt;
    CloseHandleIfValid(directory);
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

HANDLE OpenRegularFile(const std::wstring& path) noexcept {
    // Do not grant delete/write sharing during any trust or copy operation.
    // The handle must keep the file object stable until its consumer closes it.
    return CreateFileW(path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
}

bool IsRegularNonReparseFile(HANDLE handle) noexcept {
    BY_HANDLE_FILE_INFORMATION information{};
    return GetFileInformationByHandle(handle, &information) != FALSE &&
           (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
           (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

bool HasSingleHardLink(HANDLE handle) noexcept {
    BY_HANDLE_FILE_INFORMATION information{};
    return GetFileInformationByHandle(handle, &information) != FALSE && information.nNumberOfLinks == 1;
}

std::optional<Sha256Digest> HashHandle(HANDLE handle) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> hashObject;
    std::optional<Sha256Digest> result;
    do {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) break;
        DWORD objectLength = 0;
        DWORD propertyBytes = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
                              sizeof(objectLength), &propertyBytes, 0) != 0 || objectLength == 0) {
            break;
        }
        hashObject.resize(objectLength);
        if (BCryptCreateHash(algorithm, &hash, hashObject.data(), objectLength, nullptr, 0, 0) != 0) break;
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) break;
        std::vector<uint8_t> buffer(kIoBufferBytes);
        bool readSucceeded = true;
        for (;;) {
            DWORD bytesRead = 0;
            if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
                readSucceeded = false;
                break;
            }
            if (bytesRead == 0) break;
            if (BCryptHashData(hash, buffer.data(), bytesRead, 0) != 0) {
                readSucceeded = false;
                break;
            }
        }
        if (!readSucceeded) break;
        Sha256Digest digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0) break;
        result = digest;
    } while (false);

    LARGE_INTEGER zero{};
    SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN);
    if (hash != nullptr) BCryptDestroyHash(hash);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

std::optional<std::vector<uint8_t>> ReadHandleBytes(HANDLE handle, uint64_t maximumBytes) noexcept {
    const auto size = GetSize(handle);
    if (!size || *size > maximumBytes || *size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return std::nullopt;
    }
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(*size));
    size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD request = static_cast<DWORD>((std::min)(bytes.size() - offset, kIoBufferBytes));
        DWORD read = 0;
        if (!ReadFile(handle, bytes.data() + offset, request, &read, nullptr) || read == 0) {
            return std::nullopt;
        }
        offset += read;
    }
    return bytes;
}

bool ReadAt(HANDLE handle, uint64_t offset, void* destination, DWORD size) noexcept {
    LARGE_INTEGER position{};
    if (offset > static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max())) return false;
    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD bytesRead = 0;
    return SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) &&
           ReadFile(handle, destination, size, &bytesRead, nullptr) != FALSE && bytesRead == size;
}

std::optional<std::wstring> ReadPeArchitecture(HANDLE handle) noexcept {
    const auto fileSize = GetSize(handle);
    if (!fileSize || *fileSize < sizeof(IMAGE_DOS_HEADER)) return std::nullopt;
    IMAGE_DOS_HEADER dos{};
    DWORD dosBytes = sizeof(dos);
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN) || !ReadFile(handle, &dos, sizeof(dos), &dosBytes, nullptr) ||
        dosBytes != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) {
        return std::nullopt;
    }
    const uint64_t ntOffset = static_cast<uint64_t>(dos.e_lfanew);
    if (ntOffset > *fileSize || *fileSize - ntOffset < sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER)) return std::nullopt;
    DWORD signature = 0;
    IMAGE_FILE_HEADER fileHeader{};
    if (!ReadAt(handle, ntOffset, &signature, sizeof(signature)) || signature != IMAGE_NT_SIGNATURE ||
        !ReadAt(handle, ntOffset + sizeof(signature), &fileHeader, sizeof(fileHeader))) {
        return std::nullopt;
    }
    switch (fileHeader.Machine) {
        case IMAGE_FILE_MACHINE_AMD64: return L"x64";
        case IMAGE_FILE_MACHINE_ARM64: return L"arm64";
        case IMAGE_FILE_MACHINE_I386: return L"x86";
        default: return L"unknown";
    }
}

std::optional<std::wstring> ReadVersionString(const std::vector<uint8_t>& data, const wchar_t* key) {
    LPWSTR value = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(const_cast<uint8_t*>(data.data()), key, reinterpret_cast<LPVOID*>(&value), &length) ||
        value == nullptr || length == 0) {
        return std::nullopt;
    }
    const size_t count = length > 0 && value[length - 1] == L'\0' ? length - 1 : length;
    return std::wstring(value, count);
}

std::optional<PackageManifestEntry> ReadExecutableMetadata(HANDLE handle, const std::wstring& finalPath,
                                                           const std::wstring& fileName) {
    DWORD ignored = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(finalPath.c_str(), &ignored);
    if (bytes == 0) return std::nullopt;
    std::vector<uint8_t> data(bytes);
    if (!GetFileVersionInfoW(finalPath.c_str(), 0, bytes, data.data())) return std::nullopt;
    PackageManifestEntry actual;
    actual.fileName = fileName;
    actual.componentRole = PackageComponentRoleForFileName(fileName).value_or(L"");
    actual.originalFileName = ReadVersionString(data, L"\\StringFileInfo\\040904B0\\OriginalFilename").value_or(L"");
    actual.internalName = ReadVersionString(data, L"\\StringFileInfo\\040904B0\\InternalName").value_or(L"");
    actual.productName = ReadVersionString(data, L"\\StringFileInfo\\040904B0\\ProductName").value_or(L"");
    actual.productVersion = ReadVersionString(data, L"\\StringFileInfo\\040904B0\\ProductVersion").value_or(L"");
    actual.buildIdentity = ReadVersionString(data, L"\\StringFileInfo\\040904B0\\PrivateBuild").value_or(L"");
    actual.architecture = ReadPeArchitecture(handle).value_or(L"");
    if (actual.originalFileName.empty() || actual.internalName.empty() || actual.productName.empty() ||
        actual.productVersion.empty() || actual.buildIdentity.empty() || actual.architecture.empty()) {
        return std::nullopt;
    }
    return actual;
}

bool DefaultSignatureVerifier(HANDLE handle, const std::wstring& canonicalPath,
                             const std::vector<Thumbprint>& approvedSigners) noexcept {
    if (approvedSigners.empty()) return false;
    const auto actual = VerifyAuthenticodeAndGetThumbprint(handle, canonicalPath);
    if (!actual) return false;
    return std::any_of(approvedSigners.begin(), approvedSigners.end(), [&](const Thumbprint& expected) {
        return !IsPlaceholderThumbprint(expected) && ConstantTimeThumbprintEqual(*actual, expected);
    });
}

bool RunSignatureVerifier(HANDLE handle, const std::wstring& canonicalPath,
                          const std::vector<Thumbprint>& approvedSigners,
                          const PackageSignatureVerifier& verifier) noexcept {
    try {
        return verifier ? verifier(handle, canonicalPath, approvedSigners)
                        : DefaultSignatureVerifier(handle, canonicalPath, approvedSigners);
    } catch (...) {
        return false;
    }
}

std::optional<PackageFileIdentity> GetPathIdentity(const std::wstring& path) noexcept {
    HANDLE handle = OpenRegularFile(path);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
    const auto identity = GetIdentity(handle);
    CloseHandleIfValid(handle);
    return identity;
}

bool SamePathIdentity(const std::wstring& path, const PackageFileIdentity& expected) noexcept {
    HANDLE handle = OpenRegularFile(path);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const auto actual = GetIdentity(handle);
    const bool same = actual.has_value() && SameIdentity(*actual, expected) && IsRegularNonReparseFile(handle);
    CloseHandleIfValid(handle);
    return same;
}

std::optional<std::vector<uint8_t>> ReadManifestBytes(const std::wstring& path) noexcept {
    HANDLE handle = OpenRegularFile(path);
    if (handle == INVALID_HANDLE_VALUE || !IsRegularNonReparseFile(handle)) {
        CloseHandleIfValid(handle);
        return std::nullopt;
    }
    const auto bytes = ReadHandleBytes(handle, kMaxManifestBytes);
    CloseHandleIfValid(handle);
    return bytes;
}

std::optional<PackageManifest> ParseManifestBytes(std::string_view bytes) noexcept {
    try {
        if (bytes.empty() || bytes.back() != '\n') return std::nullopt;
        PackageManifest manifest;
        std::vector<std::string_view> lines;
        size_t start = 0;
        while (start < bytes.size()) {
            const size_t end = bytes.find('\n', start);
            const size_t length = end == std::string_view::npos ? bytes.size() - start : end - start;
            if (length == 0 || bytes[start + length - 1] == '\r') return std::nullopt;
            lines.push_back(bytes.substr(start, length));
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        if (lines.size() < 7 || lines[0] != "FASTFILES_PACKAGE_MANIFEST_V2") return std::nullopt;

        bool sawSchema = false;
        bool sawProduct = false;
        bool sawProductVersion = false;
        bool sawBuild = false;
        bool sawArchitecture = false;
        for (size_t index = 1; index < lines.size(); ++index) {
            const std::string_view line = lines[index];
            const auto readSingle = [&](std::string_view prefix, bool& seen, std::wstring& target) {
                if (line.rfind(prefix, 0) != 0 || seen) return false;
                const std::string_view text = line.substr(prefix.size());
                target = AsciiToWide(text);
                seen = !target.empty();
                return seen;
            };
            if (line.rfind("schema=", 0) == 0) {
                if (sawSchema) return std::nullopt;
                uint64_t schema = 0;
                if (!ParseUnsigned(line.substr(7), schema) || schema != kPackageManifestSchemaV2) return std::nullopt;
                manifest.schemaVersion = static_cast<uint32_t>(schema);
                sawSchema = true;
            } else if (readSingle("productName=", sawProduct, manifest.release.productName)) {
            } else if (readSingle("productVersion=", sawProductVersion, manifest.release.productVersion)) {
            } else if (readSingle("buildIdentity=", sawBuild, manifest.release.buildIdentity)) {
            } else if (readSingle("architecture=", sawArchitecture, manifest.release.architecture)) {
            } else if (line.rfind("file=", 0) == 0) {
                const auto fields = SplitFields(line.substr(5));
                if (fields.size() != 10) return std::nullopt;
                PackageManifestEntry entry;
                entry.fileName = AsciiToWide(fields[0]);
                entry.componentRole = AsciiToWide(fields[1]);
                entry.originalFileName = AsciiToWide(fields[2]);
                entry.internalName = AsciiToWide(fields[3]);
                entry.productName = AsciiToWide(fields[4]);
                entry.productVersion = AsciiToWide(fields[5]);
                entry.buildIdentity = AsciiToWide(fields[6]);
                entry.architecture = AsciiToWide(fields[7]);
                if (!IsValidManifestFileName(entry.fileName) || entry.componentRole.empty() ||
                    !IsValidManifestFileName(entry.originalFileName) || !IsValidManifestFileName(entry.internalName) ||
                    entry.productName.empty() || entry.productVersion.empty() || entry.buildIdentity.empty() ||
                    entry.architecture.empty() || !ParseUnsigned(fields[8], entry.sizeBytes) ||
                    !ParseDigest(fields[9], entry.sha256) || FindPackageManifestEntry(manifest, entry.fileName) != nullptr) {
                    return std::nullopt;
                }
                manifest.files.push_back(std::move(entry));
            } else {
                return std::nullopt;
            }
        }
        if (!sawSchema || !sawProduct || !sawProductVersion || !sawBuild || !sawArchitecture || manifest.files.empty() ||
            manifest.release.productName != L"FastFiles" || manifest.release.architecture != L"x64" ||
            !ParseProductVersion(manifest.release.productVersion) || !IsHexIdentity(WideToAscii(manifest.release.buildIdentity))) {
            return std::nullopt;
        }
        for (const auto& entry : manifest.files) {
            if (entry.productName != manifest.release.productName || entry.productVersion != manifest.release.productVersion ||
                entry.buildIdentity != manifest.release.buildIdentity || entry.architecture != manifest.release.architecture ||
                !ParseProductVersion(entry.productVersion) || !IsHexIdentity(WideToAscii(entry.buildIdentity))) {
                return std::nullopt;
            }
        }
        return manifest;
    } catch (...) {
        return std::nullopt;
    }
}

bool DefaultManifestSignatureVerifier(const std::vector<uint8_t>& manifestBytes,
                                     const std::vector<uint8_t>& signatureBytes,
                                     const std::vector<Thumbprint>& approvedSigners) noexcept {
    if (manifestBytes.empty() || signatureBytes.empty() || approvedSigners.empty() ||
        manifestBytes.size() > UINT32_MAX || signatureBytes.size() > UINT32_MAX) {
        return false;
    }
    CRYPT_VERIFY_MESSAGE_PARA verify{};
    verify.cbSize = sizeof(verify);
    verify.dwMsgAndCertEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    const BYTE* content = manifestBytes.data();
    DWORD contentSize = static_cast<DWORD>(manifestBytes.size());
    PCCERT_CONTEXT signer = nullptr;
    if (!CryptVerifyDetachedMessageSignature(&verify, 0, signatureBytes.data(), static_cast<DWORD>(signatureBytes.size()),
                                             1, &content, &contentSize, &signer) || signer == nullptr) {
        return false;
    }

    bool valid = false;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    CERT_CHAIN_PARA chainParameters{};
    chainParameters.cbSize = sizeof(chainParameters);
    if (CertGetCertificateChain(nullptr, signer, nullptr, signer->hCertStore, &chainParameters, 0, nullptr, &chain)) {
        CERT_CHAIN_POLICY_PARA policyParameters{};
        policyParameters.cbSize = sizeof(policyParameters);
        CERT_CHAIN_POLICY_STATUS policyStatus{};
        policyStatus.cbSize = sizeof(policyStatus);
        valid = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain, &policyParameters, &policyStatus) != FALSE &&
                policyStatus.dwError == ERROR_SUCCESS;
        CertFreeCertificateChain(chain);
    }
    Thumbprint thumbprint{};
    DWORD thumbprintSize = static_cast<DWORD>(thumbprint.size());
    if (!valid || !CertGetCertificateContextProperty(signer, CERT_SHA1_HASH_PROP_ID, thumbprint.data(), &thumbprintSize) ||
        thumbprintSize != thumbprint.size()) {
        CertFreeCertificateContext(signer);
        return false;
    }
    valid = std::any_of(approvedSigners.begin(), approvedSigners.end(), [&](const Thumbprint& expected) {
        return !IsPlaceholderThumbprint(expected) && ConstantTimeThumbprintEqual(thumbprint, expected);
    });
    CertFreeCertificateContext(signer);
    return valid;
}

struct RawInspection {
    HANDLE handle = INVALID_HANDLE_VALUE;
    PackageFileIdentity identity{};
    Sha256Digest sha256{};
    uint64_t sizeBytes = 0;
    std::wstring finalPath;
};

std::optional<RawInspection> InspectPath(const std::wstring& path, const PackageManifestEntry& expected,
                                          const std::vector<Thumbprint>& approvedSigners,
                                          const PackageSignatureVerifier& verifier) noexcept {
    HANDLE handle = OpenRegularFile(path);
    if (handle == INVALID_HANDLE_VALUE || !IsRegularNonReparseFile(handle) || !HasSingleHardLink(handle)) {
        CloseHandleIfValid(handle);
        return std::nullopt;
    }
    const auto identity = GetIdentity(handle);
    const auto size = GetSize(handle);
    const auto finalPath = GetFinalPath(handle);
    const auto digest = HashHandle(handle);
    if (!identity || !size || !finalPath || !digest || *size != expected.sizeBytes || *digest != expected.sha256) {
        CloseHandleIfValid(handle);
        return std::nullopt;
    }
    if (!expected.productName.empty()) {
        const auto actualMetadata = ReadExecutableMetadata(handle, *finalPath, expected.fileName);
        if (!actualMetadata || !PackageFileMetadataMatches(PackageManifest{}, expected, *actualMetadata)) {
            CloseHandleIfValid(handle);
            return std::nullopt;
        }
    }
    if (!RunSignatureVerifier(handle, *finalPath, approvedSigners, verifier)) {
        CloseHandleIfValid(handle);
        return std::nullopt;
    }

    // Defense in depth: source handles deny delete/write sharing and the
    // signer was bound to this handle. Keep the identity/path and final-hash
    // checks because they also detect unexpected filesystem/provider changes.
    if (!SamePathIdentity(path, *identity)) {
        CloseHandleIfValid(handle);
        return std::nullopt;
    }
    const auto afterIdentity = GetIdentity(handle);
    const auto afterDigest = HashHandle(handle);
    if (!afterIdentity || !afterDigest || !SameIdentity(*afterIdentity, *identity) || *afterDigest != *digest) {
        CloseHandleIfValid(handle);
        return std::nullopt;
    }
    return RawInspection{handle, *identity, *digest, *size, *finalPath};
}

bool DestinationParentIsSafe(const std::wstring& path) noexcept {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash == 0) return false;
    return GetFinalDirectoryPath(path.substr(0, slash)).has_value();
}

bool ExistingDestinationIsSafe(const std::wstring& path) noexcept {
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool safe = GetFileInformationByHandle(handle, &information) != FALSE &&
                      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    CloseHandleIfValid(handle);
    return safe;
}

} // namespace

std::optional<std::vector<uint8_t>> SerializePackageManifest(const PackageManifest& manifest) noexcept {
    try {
        if (manifest.schemaVersion != kPackageManifestSchemaV2 || manifest.release.productName != L"FastFiles" ||
            manifest.release.architecture != L"x64" || !ParseProductVersion(manifest.release.productVersion) ||
            !IsHexIdentity(WideToAscii(manifest.release.buildIdentity)) || manifest.files.empty()) {
            return std::nullopt;
        }
        struct SortEntry {
            const PackageManifestEntry* entry;
        };
        std::vector<SortEntry> sorted;
        sorted.reserve(manifest.files.size());
        for (const auto& entry : manifest.files) sorted.push_back(SortEntry{&entry});
        std::sort(sorted.begin(), sorted.end(), [](const SortEntry& left, const SortEntry& right) {
            const int insensitive = _wcsicmp(left.entry->fileName.c_str(), right.entry->fileName.c_str());
            return insensitive == 0 ? left.entry->fileName < right.entry->fileName : insensitive < 0;
        });
        std::ostringstream output;
        output << "FASTFILES_PACKAGE_MANIFEST_V2\n"
               << "schema=2\n"
               << "productName=" << WideToAscii(manifest.release.productName) << "\n"
               << "productVersion=" << WideToAscii(manifest.release.productVersion) << "\n"
               << "buildIdentity=" << WideToAscii(manifest.release.buildIdentity) << "\n"
               << "architecture=" << WideToAscii(manifest.release.architecture) << "\n";
        std::wstring previous;
        for (const auto& sortedEntry : sorted) {
            const auto& entry = *sortedEntry.entry;
            if (!IsValidManifestFileName(entry.fileName) || !IsValidManifestFileName(entry.originalFileName) ||
                !IsValidManifestFileName(entry.internalName) || entry.componentRole.empty() || entry.productName.empty() ||
                entry.productVersion.empty() || entry.buildIdentity.empty() || entry.architecture.empty() ||
                FindPackageManifestEntry(manifest, entry.fileName) != sortedEntry.entry ||
                (!previous.empty() && _wcsicmp(previous.c_str(), entry.fileName.c_str()) == 0) ||
                entry.productName != manifest.release.productName || entry.productVersion != manifest.release.productVersion ||
                entry.buildIdentity != manifest.release.buildIdentity || entry.architecture != manifest.release.architecture) {
                return std::nullopt;
            }
            previous = entry.fileName;
            output << "file=" << WideToAscii(entry.fileName) << '|'
                   << WideToAscii(entry.componentRole) << '|'
                   << WideToAscii(entry.originalFileName) << '|'
                   << WideToAscii(entry.internalName) << '|'
                   << WideToAscii(entry.productName) << '|'
                   << WideToAscii(entry.productVersion) << '|'
                   << WideToAscii(entry.buildIdentity) << '|'
                   << WideToAscii(entry.architecture) << '|'
                   << entry.sizeBytes << '|';
            constexpr char kHex[] = "0123456789ABCDEF";
            for (const uint8_t byte : entry.sha256) output << kHex[(byte >> 4) & 0x0F] << kHex[byte & 0x0F];
            output << '\n';
        }
        const std::string bytes = output.str();
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<PackageManifest> ReadPackageManifest(const std::wstring& path) noexcept {
    const auto bytes = ReadManifestBytes(path);
    if (!bytes) return std::nullopt;
    const std::string text(bytes->begin(), bytes->end());
    auto manifest = ParseManifestBytes(text);
    if (!manifest) return std::nullopt;
    const auto canonical = SerializePackageManifest(*manifest);
    if (!canonical || *canonical != *bytes) return std::nullopt;
    return manifest;
}

std::optional<PackageManifest> ReadAuthenticatedPackageManifest(
    const std::wstring& packageRoot, const std::vector<Thumbprint>& approvedSigners,
    const PackageManifestSignatureVerifier& verifier) noexcept {
    try {
        const auto finalRoot = GetFinalDirectoryPath(packageRoot);
        if (!finalRoot) return std::nullopt;
        const std::wstring manifestPath = packageRoot + L"\\" + kPackageManifestFileName;
        const std::wstring signaturePath = packageRoot + L"\\" + kPackageManifestSignatureFileName;
        HANDLE manifestHandle = OpenRegularFile(manifestPath);
        HANDLE signatureHandle = OpenRegularFile(signaturePath);
        if (manifestHandle == INVALID_HANDLE_VALUE || signatureHandle == INVALID_HANDLE_VALUE ||
            !IsRegularNonReparseFile(manifestHandle) || !IsRegularNonReparseFile(signatureHandle)) {
            CloseHandleIfValid(manifestHandle);
            CloseHandleIfValid(signatureHandle);
            return std::nullopt;
        }
        const auto manifestFinalPath = GetFinalPath(manifestHandle);
        const auto signatureFinalPath = GetFinalPath(signatureHandle);
        const auto manifestBytes = ReadHandleBytes(manifestHandle, kMaxManifestBytes);
        const auto signatureBytes = ReadHandleBytes(signatureHandle, kMaxManifestBytes);
        const bool confined = manifestFinalPath && signatureFinalPath && IsWithinDirectory(*finalRoot, *manifestFinalPath) &&
                              IsWithinDirectory(*finalRoot, *signatureFinalPath);
        if (!confined || !manifestBytes || !signatureBytes) {
            CloseHandleIfValid(manifestHandle);
            CloseHandleIfValid(signatureHandle);
            return std::nullopt;
        }
        const bool verified = [&] {
            try {
                return verifier ? verifier(*manifestBytes, *signatureBytes, approvedSigners)
                                 : DefaultManifestSignatureVerifier(*manifestBytes, *signatureBytes, approvedSigners);
            } catch (...) {
                return false;
            }
        }();
        CloseHandleIfValid(manifestHandle);
        CloseHandleIfValid(signatureHandle);
        if (!verified) return std::nullopt;
        const std::string text(manifestBytes->begin(), manifestBytes->end());
        auto manifest = ParseManifestBytes(text);
        const auto canonical = manifest ? SerializePackageManifest(*manifest) : std::nullopt;
        if (!manifest || !canonical || *canonical != *manifestBytes) return std::nullopt;
        manifest->signatureVerified = true;
        manifest->detachedSignature = *signatureBytes;
        return manifest;
    } catch (...) {
        return std::nullopt;
    }
}

const PackageManifestEntry* FindPackageManifestEntry(const PackageManifest& manifest,
                                                     const std::wstring& fileName) noexcept {
    const auto found = std::find_if(manifest.files.begin(), manifest.files.end(), [&](const auto& entry) {
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

bool HasExactPackageDirectoryInventory(const std::wstring& packageRoot,
                                       const std::vector<std::wstring>& expectedNames) noexcept {
    if (expectedNames.empty() || !GetFinalDirectoryPath(packageRoot)) return false;
    const std::wstring pattern = packageRoot + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW(pattern.c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) return false;
    bool valid = true;
    size_t payloadCount = 0;
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        if ((data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
            valid = false;
            break;
        }
        const bool isManifest = _wcsicmp(name.c_str(), kPackageManifestFileName) == 0;
        const bool isSignature = _wcsicmp(name.c_str(), kPackageManifestSignatureFileName) == 0;
        const auto expected = std::find_if(expectedNames.begin(), expectedNames.end(), [&](const auto& item) {
            return _wcsicmp(item.c_str(), name.c_str()) == 0;
        });
        if (!isManifest && !isSignature && expected == expectedNames.end()) {
            valid = false;
            break;
        }
        if (expected != expectedNames.end()) ++payloadCount;
    } while (FindNextFileW(search, &data) != FALSE);
    const DWORD lastError = GetLastError();
    FindClose(search);
    return valid && lastError == ERROR_NO_MORE_FILES && payloadCount == expectedNames.size();
}

std::optional<std::wstring> PackageComponentRoleForFileName(const std::wstring& fileName) noexcept {
    if (_wcsicmp(fileName.c_str(), L"FastFiles.exe") == 0) return L"ui";
    if (_wcsicmp(fileName.c_str(), L"FastFilesEngine.exe") == 0) return L"engine";
    if (_wcsicmp(fileName.c_str(), L"FastFilesIndexSvc.exe") == 0) return L"service";
    if (_wcsicmp(fileName.c_str(), L"FastFilesSetup.exe") == 0) return L"installer";
    return std::nullopt;
}

std::optional<int> CompareProductVersions(const std::wstring& left, const std::wstring& right) noexcept {
    const auto leftParts = ParseProductVersion(left);
    const auto rightParts = ParseProductVersion(right);
    if (!leftParts || !rightParts) return std::nullopt;
    for (size_t index = 0; index < leftParts->size(); ++index) {
        if (leftParts->at(index) < rightParts->at(index)) return -1;
        if (leftParts->at(index) > rightParts->at(index)) return 1;
    }
    return 0;
}

bool PackageFileMetadataMatches(const PackageManifest& manifest, const PackageManifestEntry& expected,
                                const PackageManifestEntry& actual) noexcept {
    if (expected.fileName.empty() || _wcsicmp(expected.fileName.c_str(), actual.fileName.c_str()) != 0) return false;
    if (expected.productName.empty()) {
        // Programmatic unit fixtures may exercise the handle/signature seam
        // with non-PE payloads. V2 manifests parsed from disk always carry all
        // metadata, so this test-only relaxation cannot authorize a package.
        return true;
    }
    const auto expectedRole = PackageComponentRoleForFileName(expected.fileName);
    if (!expectedRole || expected.componentRole != *expectedRole) return false;
    const bool releaseMatches = manifest.release.productName.empty() ||
                                (manifest.release.productName == actual.productName &&
                                 manifest.release.productVersion == actual.productVersion &&
                                 manifest.release.buildIdentity == actual.buildIdentity &&
                                 manifest.release.architecture == actual.architecture);
    return expected.componentRole == actual.componentRole && expected.originalFileName == actual.originalFileName &&
           expected.internalName == actual.internalName && expected.productName == actual.productName &&
           expected.productVersion == actual.productVersion && expected.buildIdentity == actual.buildIdentity &&
           expected.architecture == actual.architecture && releaseMatches;
}

bool ValidateTrustedPackageSet(const std::wstring& packageRoot, const PackageManifest& manifest,
                              const std::vector<std::wstring>& expectedNames,
                              const PackageSignerSelector& signerSelector,
                              const PackageSignatureVerifier& verifier) noexcept {
    if (!manifest.signatureVerified || manifest.detachedSignature.empty() || !signerSelector ||
        manifest.schemaVersion != kPackageManifestSchemaV2 || !HasExactPackageFileSet(manifest, expectedNames) ||
        !HasExactPackageDirectoryInventory(packageRoot, expectedNames)) {
        return false;
    }
    try {
        for (const auto& name : expectedNames) {
            const auto* expected = FindPackageManifestEntry(manifest, name);
            const auto role = PackageComponentRoleForFileName(name);
            if (expected == nullptr || !role || expected->componentRole != *role ||
                expected->productName != manifest.release.productName ||
                expected->productVersion != manifest.release.productVersion ||
                expected->buildIdentity != manifest.release.buildIdentity ||
                expected->architecture != manifest.release.architecture ||
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
        CloseHandleIfValid(handle);
        return std::nullopt;
    }
    const auto digest = HashHandle(handle);
    CloseHandleIfValid(handle);
    return digest;
}

TrustedPackageFile::~TrustedPackageFile() {
    CloseHandleIfValid(handle_);
}

TrustedPackageFile::TrustedPackageFile(TrustedPackageFile&& other) noexcept
    : handle_(other.handle_), identity_(other.identity_), sha256_(other.sha256_), sizeBytes_(other.sizeBytes_),
      finalPath_(std::move(other.finalPath_)) {
    other.handle_ = INVALID_HANDLE_VALUE;
}

TrustedPackageFile& TrustedPackageFile::operator=(TrustedPackageFile&& other) noexcept {
    if (this != &other) {
        CloseHandleIfValid(handle_);
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
    const std::wstring& packageRoot, const std::wstring& fileName, const PackageManifestEntry& expected,
    const std::vector<Thumbprint>& approvedSigners, const PackageSignatureVerifier& verifier) noexcept {
    if (!IsValidManifestFileName(fileName) || _wcsicmp(fileName.c_str(), expected.fileName.c_str()) != 0) {
        return std::nullopt;
    }
    const auto finalRoot = GetFinalDirectoryPath(packageRoot);
    if (!finalRoot) return std::nullopt;
    const std::wstring path = packageRoot + L"\\" + fileName;
    auto inspection = InspectPath(path, expected, approvedSigners, verifier);
    if (!inspection || !IsWithinDirectory(*finalRoot, inspection->finalPath)) {
        if (inspection) CloseHandleIfValid(inspection->handle);
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
    HANDLE destination = CreateFileW(destinationPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH,
                                     nullptr);
    if (destination == INVALID_HANDLE_VALUE) return false;

    bool copied = false;
    do {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(source.Handle(), zero, nullptr, FILE_BEGIN)) break;
        std::vector<uint8_t> buffer(kIoBufferBytes);
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
    CloseHandleIfValid(destination);

    const auto identity = GetIdentity(source.Handle());
    const auto digest = HashHandle(source.Handle());
    if (!identity || !digest || !SameIdentity(*identity, source.Identity()) || *digest != source.Sha256()) copied = false;
    if (!copied) DeleteFileW(destinationPath.c_str());
    return copied;
}

bool VerifyPackageFileAtPath(const std::wstring& path, const PackageManifestEntry& expected,
                             const std::vector<Thumbprint>& approvedSigners,
                             const PackageSignatureVerifier& verifier) noexcept {
    auto inspection = InspectPath(path, expected, approvedSigners, verifier);
    if (!inspection) return false;
    CloseHandleIfValid(inspection->handle);
    return true;
}

bool PromoteVerifiedPackageFile(const std::wstring& stagedPath, const std::wstring& destinationPath,
                                const PackageManifestEntry& expected, const std::vector<Thumbprint>& approvedSigners,
                                const PackageSignatureVerifier& verifier) noexcept {
    if (!VerifyPackageFileAtPath(stagedPath, expected, approvedSigners, verifier)) {
        DeleteFileW(stagedPath.c_str());
        return false;
    }
    if (!DestinationParentIsSafe(destinationPath) || !ExistingDestinationIsSafe(destinationPath)) {
        DeleteFileW(stagedPath.c_str());
        return false;
    }
    // No MOVEFILE_COPY_ALLOWED: promotion must remain same-volume rename,
    // never silently degrade into a path-based copy/delete operation.
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
    if (!IsPlaceholderThumbprint(kExpectedEngineSignatureThumbprint)) result.push_back(kExpectedEngineSignatureThumbprint);
    if (!IsPlaceholderThumbprint(kExpectedIndexSvcSignatureThumbprint) &&
        std::none_of(result.begin(), result.end(), [&](const Thumbprint& value) {
            return ConstantTimeThumbprintEqual(value, kExpectedIndexSvcSignatureThumbprint);
        })) {
        result.push_back(kExpectedIndexSvcSignatureThumbprint);
    }
    return result;
}

} // namespace ffsetup
