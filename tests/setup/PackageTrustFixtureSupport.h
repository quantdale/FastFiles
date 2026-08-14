// Shared manifest/payload helpers used by both the portable package-trust
// tests (test_package_trust.cpp) and the privileged CurrentUser\Root
// detached-CMS integration test (test_package_trust_wintrust_integration.cpp).
// Included (not linked) by each translation unit so every test executable
// stays a single-TU CTest target; the anonymous namespace gives each a
// private copy with internal linkage.
//
// This header is for tests only; product code must not include it.

#pragma once

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ffsetup/PackageTrust.h"
#include "ffsetup/FastFilesVersion.h"

namespace {

std::wstring Join(const std::wstring& root, const std::wstring& name) {
    return root + L"\\" + name;
}

void WritePayload(const std::wstring& path, const std::string& content) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void WriteBytes(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string AsciiName(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) result.push_back(static_cast<char>(character));
    return result;
}

ffsetup::PackageManifestEntry ManifestEntry(const std::wstring& root, const std::wstring& name) {
    const std::wstring path = Join(root, name);
    const auto digest = ffsetup::ComputeSha256FileAtPath(path);
    ffsetup::PackageManifestEntry entry;
    entry.fileName = name;
    entry.sizeBytes = std::filesystem::file_size(std::filesystem::path(path));
    if (digest) entry.sha256 = *digest;
    return entry;
}

ffsetup::PackageManifest MakeManifest(const std::wstring& root,
                                      const std::vector<std::wstring>& names) {
    ffsetup::PackageManifest manifest;
    manifest.schemaVersion = ffsetup::kPackageManifestSchemaV2;
    for (const auto& name : names) {
        auto entry = ManifestEntry(root, name);
        entry.componentRole = ffsetup::PackageComponentRoleForFileName(name).value_or(L"");
        manifest.files.push_back(std::move(entry));
    }
    return manifest;
}

ffsetup::PackageManifest MakeCanonicalManifest(const std::wstring& root,
                                               const std::vector<std::wstring>& names) {
    auto manifest = MakeManifest(root, names);
    manifest.release.productName = ffsetup::kProductName;
    manifest.release.productVersion = ffsetup::kProductVersion;
    manifest.release.buildIdentity = ffsetup::kBuildIdentity;
    manifest.release.architecture = L"x64";
    for (auto& entry : manifest.files) {
        entry.componentRole = ffsetup::PackageComponentRoleForFileName(entry.fileName).value_or(L"");
        entry.originalFileName = entry.fileName;
        entry.internalName = entry.fileName;
        entry.productName = manifest.release.productName;
        entry.productVersion = manifest.release.productVersion;
        entry.buildIdentity = manifest.release.buildIdentity;
        entry.architecture = manifest.release.architecture;
    }
    return manifest;
}

} // namespace
