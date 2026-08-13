#include <windows.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ffsetup/PackageTrust.h"
#include "ffsetup/FastFilesVersion.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

std::wstring FixtureRoot() {
    return (std::filesystem::temp_directory_path() /
            ("FastFiles-package-trust-" + std::to_string(GetCurrentProcessId())))
        .wstring();
}

std::wstring Join(const std::wstring& root, const std::wstring& name) {
    return root + L"\\" + name;
}

void WritePayload(const std::wstring& path, const std::string& content) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string HexDigest(const ffsetup::Sha256Digest& digest) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const uint8_t byte : digest) {
        result.push_back(kHex[(byte >> 4) & 0x0F]);
        result.push_back(kHex[byte & 0x0F]);
    }
    return result;
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
    manifest.schemaVersion = 1;
    manifest.productVersion = ffsetup::kProductVersion;
    for (const auto& name : names) manifest.files.push_back(ManifestEntry(root, name));
    return manifest;
}

void WriteManifest(const std::wstring& path, const ffsetup::PackageManifest& manifest) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file << "FASTFILES_PACKAGE_MANIFEST_V1\n";
    file << "version=" << manifest.schemaVersion << "\n";
    file << "productVersion=" << ffsetup::kProductVersionAscii << "\n";
    for (const auto& entry : manifest.files) {
        std::string name;
        name.reserve(entry.fileName.size());
        for (const wchar_t character : entry.fileName) name.push_back(static_cast<char>(character));
        file << "file=" << name << "|" << entry.sizeBytes << "|" << HexDigest(entry.sha256) << "\n";
    }
}

bool CanCreateFileSymlink(const std::wstring& linkPath, const std::wstring& targetPath) {
    if (CreateSymbolicLinkW(linkPath.c_str(), targetPath.c_str(), 0) != FALSE) return true;
    constexpr DWORD kAllowUnprivilegedCreate = 0x2;
    return CreateSymbolicLinkW(linkPath.c_str(), targetPath.c_str(), kAllowUnprivilegedCreate) != FALSE;
}

void TestManifestParsingAndSetValidation(const std::wstring& root) {
    const std::vector<std::wstring> names{L"FastFiles.exe", L"FastFilesEngine.exe",
                                          L"FastFilesIndexSvc.exe", L"FastFilesSetup.exe"};
    for (const auto& name : names) WritePayload(Join(root, name), "package-payload-" + AsciiName(name));
    const auto manifest = MakeManifest(root, names);
    const std::wstring manifestPath = Join(root, L"FastFiles.package.manifest");
    WriteManifest(manifestPath, manifest);
    const auto parsed = ffsetup::ReadPackageManifest(manifestPath);
    Check(parsed.has_value(), "a well-formed package manifest parses");
    Check(parsed.has_value() && ffsetup::HasExactPackageFileSet(*parsed, names),
          "the manifest contains the complete non-mixed shipped payload set");
    Check(parsed.has_value() && ffsetup::FindPackageManifestEntry(*parsed, L"FastFilesEngine.exe") != nullptr,
          "manifest lookup is case-insensitive for a named payload");

    const std::wstring malformed = Join(root, L"malformed.manifest");
    {
        std::ofstream file(std::filesystem::path(malformed), std::ios::binary | std::ios::trunc);
        file << "FASTFILES_PACKAGE_MANIFEST_V1\nversion=1\nfile=FastFiles.exe|not-a-size|00\n";
    }
    Check(!ffsetup::ReadPackageManifest(malformed).has_value(),
          "malformed size/hash fields reject the entire package manifest");

    ffsetup::PackageManifest incomplete = manifest;
    incomplete.files.pop_back();
    Check(!ffsetup::HasExactPackageFileSet(incomplete, names),
          "an incomplete package cannot satisfy the exact payload inventory");
    auto mismatched = manifest;
    mismatched.files[0].sha256[0] ^= 0xFF;
    Check(mismatched.files[0].sha256 != manifest.files[0].sha256 &&
              !ffsetup::VerifyPackageFileAtPath(Join(root, names[0]), mismatched.files[0], {},
                                                [](const std::wstring&, const std::vector<ffsetup::Thumbprint>&) {
                                                    return false;
                                                }),
          "a mismatched manifest hash rejects the otherwise present payload");
}

void TestSignerAndHandleBoundCopy(const std::wstring& root) {
    const std::wstring sourcePath = Join(root, L"payload.bin");
    const std::wstring stagePath = Join(root, L"stage.bin");
    const std::wstring destinationPath = Join(root, L"installed.bin");
    WritePayload(sourcePath, "approved-payload");
    const auto expected = ManifestEntry(root, L"payload.bin");

    ffsetup::Thumbprint approved{};
    approved[0] = 0xA5;
    const std::vector<ffsetup::Thumbprint> approvedSigners{approved};
    const auto approvedVerifier = [&](const std::wstring&, const std::vector<ffsetup::Thumbprint>& signers) {
        return signers.size() == 1 && ffsetup::ConstantTimeThumbprintEqual(signers[0], approved);
    };

    const std::vector<ffsetup::Thumbprint> wrongSigner(1);
    const auto rejectedVerifier = [](const std::wstring&, const std::vector<ffsetup::Thumbprint>&) { return false; };
    Check(!ffsetup::OpenTrustedPackageFile(root, L"payload.bin", expected, {}, rejectedVerifier).has_value(),
          "unsigned, wrong-signer, and unapproved-signer payloads fail closed");
    Check(!ffsetup::OpenTrustedPackageFile(root, L"payload.bin", expected, wrongSigner, rejectedVerifier).has_value(),
          "a validly shaped but unapproved signer pin cannot authorize a payload");
    auto malformedPayload = expected;
    malformedPayload.sha256[0] ^= 0xFF;
    Check(!ffsetup::OpenTrustedPackageFile(root, L"payload.bin", malformedPayload, approvedSigners,
                                           approvedVerifier).has_value(),
          "a malformed or hash-mismatched payload fails before signer approval can authorize it");

    auto trusted = ffsetup::OpenTrustedPackageFile(root, L"payload.bin", expected, approvedSigners, approvedVerifier);
    Check(trusted.has_value(), "an approved signed payload passes manifest, identity, and signature checks");
    Check(trusted.has_value() && trusted->SizeBytes() == expected.sizeBytes,
          "the trusted handle records the manifest-bound size");

    WritePayload(stagePath, "preplanted-scratch");
    Check(trusted.has_value() && !ffsetup::CopyTrustedPackageFile(*trusted, stagePath),
          "a preplanted staging destination cannot be overwritten by the copy step");
    DeleteFileW(stagePath.c_str());
    Check(trusted.has_value() && ffsetup::CopyTrustedPackageFile(*trusted, stagePath),
          "copy consumes the already-verified source handle into a newly-created stage file");
    Check(ffsetup::VerifyPackageFileAtPath(stagePath, expected, approvedSigners, approvedVerifier),
          "the staged file is revalidated before promotion");

    WritePayload(destinationPath, "old-destination");
    Check(ffsetup::PromoteVerifiedPackageFile(stagePath, destinationPath, expected, approvedSigners,
                                              approvedVerifier),
          "a regular pre-existing destination is atomically replaced and revalidated");
    std::ifstream installed(std::filesystem::path(destinationPath), std::ios::binary);
    std::string installedText((std::istreambuf_iterator<char>(installed)), std::istreambuf_iterator<char>());
    Check(installedText == "approved-payload", "promotion installs exactly the handle-bound approved bytes");

    const std::wstring changedStage = Join(root, L"changed-stage.bin");
    trusted = ffsetup::OpenTrustedPackageFile(root, L"payload.bin", expected, approvedSigners, approvedVerifier);
    Check(trusted.has_value() && ffsetup::CopyTrustedPackageFile(*trusted, changedStage),
          "a second stage is created for substitution testing");
    WritePayload(changedStage, "changed-after-inspection");
    Check(!ffsetup::PromoteVerifiedPackageFile(changedStage, Join(root, L"changed-destination.bin"), expected,
                                               approvedSigners, approvedVerifier),
          "a staged payload changed before promotion is rejected");
    Check(!std::filesystem::exists(std::filesystem::path(changedStage)),
          "failed staged validation cleans up the untrusted staging artifact");

    const std::wstring racePath = Join(root, L"race.bin");
    WritePayload(racePath, "race-approved");
    const auto raceExpected = ManifestEntry(root, L"race.bin");
    const auto raceVerifier = [&](const std::wstring&, const std::vector<ffsetup::Thumbprint>&) {
        const std::wstring moved = Join(root, L"race-original.bin");
        MoveFileExW(racePath.c_str(), moved.c_str(), MOVEFILE_REPLACE_EXISTING);
        WritePayload(racePath, "race-replacement");
        return true;
    };
    Check(!ffsetup::OpenTrustedPackageFile(root, L"race.bin", raceExpected, approvedSigners, raceVerifier).has_value(),
          "a source rename/substitution race after signature inspection fails the identity recheck");
}

void TestReparseProtection(const std::wstring& root) {
    const std::wstring target = Join(root, L"reparse-target.bin");
    const std::wstring sourceLink = Join(root, L"reparse-source.bin");
    const std::wstring destinationLink = Join(root, L"reparse-destination.bin");
    WritePayload(target, "reparse-target");
    if (!CanCreateFileSymlink(sourceLink, target)) {
        std::printf("SKIPPED: symbolic-link reparse fixture is unavailable on this host.\n");
        return;
    }
    const auto expected = ManifestEntry(root, L"reparse-source.bin");
    ffsetup::Thumbprint approved{};
    approved[0] = 0xA5;
    const auto verifier = [](const std::wstring&, const std::vector<ffsetup::Thumbprint>&) { return true; };
    Check(!ffsetup::OpenTrustedPackageFile(root, L"reparse-source.bin", expected, {approved}, verifier).has_value(),
          "a reparse-point source is rejected before signature or copy consumption");

    const std::wstring regular = Join(root, L"regular.bin");
    WritePayload(regular, "regular");
    const auto regularExpected = ManifestEntry(root, L"regular.bin");
    auto trusted = ffsetup::OpenTrustedPackageFile(root, L"regular.bin", regularExpected, {approved}, verifier);
    const std::wstring stage = Join(root, L"reparse-stage.bin");
    Check(trusted.has_value() && ffsetup::CopyTrustedPackageFile(*trusted, stage),
          "a regular payload can be staged before destination reparse validation");
    if (CanCreateFileSymlink(destinationLink, target)) {
        Check(!ffsetup::PromoteVerifiedPackageFile(stage, destinationLink, regularExpected, {approved}, verifier),
              "a reparse-point destination is rejected before promotion");
        DeleteFileW(destinationLink.c_str());
    } else {
        std::printf("SKIPPED: symbolic-link destination fixture is unavailable on this host.\n");
        DeleteFileW(stage.c_str());
    }
    DeleteFileW(sourceLink.c_str());
}

void TestRollbackCandidateTrustMatrix(const std::wstring& root) {
    const std::wstring candidateRoot = Join(root, L"rollback-candidate");
    std::filesystem::create_directories(std::filesystem::path(candidateRoot));
    const std::vector<std::wstring> names{L"FastFiles.exe", L"FastFilesEngine.exe",
                                          L"FastFilesIndexSvc.exe", L"FastFilesSetup.exe"};
    for (const auto& name : names) WritePayload(Join(candidateRoot, name), "rollback-" + AsciiName(name));
    const auto manifest = MakeManifest(candidateRoot, names);
    ffsetup::Thumbprint approved{};
    approved[0] = 0x5A;
    const auto selector = [&](const std::wstring&) { return std::vector<ffsetup::Thumbprint>{approved}; };
    const auto accepted = [](const std::wstring&, const std::vector<ffsetup::Thumbprint>& signers) {
        return signers.size() == 1 && signers[0][0] == 0x5A;
    };
    Check(ffsetup::ValidateTrustedPackageSet(candidateRoot, manifest, names, selector, accepted),
          "a complete signed previous installation is an eligible rollback candidate");
    Check(!ffsetup::ValidateTrustedPackageSet(candidateRoot, manifest, names, selector,
                                               [](const std::wstring&, const std::vector<ffsetup::Thumbprint>&) {
                                                   return false;
                                               }),
          "an unsigned or wrong-signer previous installation is not an eligible rollback candidate");

    WritePayload(Join(candidateRoot, L"FastFilesEngine.exe"), "modified-previous-engine");
    Check(!ffsetup::ValidateTrustedPackageSet(candidateRoot, manifest, names, selector, accepted),
          "a modified previous service/engine payload fails its captured manifest hash");
    auto incomplete = manifest;
    incomplete.files.pop_back();
    Check(!ffsetup::ValidateTrustedPackageSet(candidateRoot, incomplete, names, selector, accepted),
          "an incomplete previous installation cannot be restored automatically");
}

} // namespace

int wmain() {
    const std::wstring root = FixtureRoot();
    std::filesystem::remove_all(std::filesystem::path(root));
    std::filesystem::create_directories(std::filesystem::path(root));

    TestManifestParsingAndSetValidation(root);
    TestSignerAndHandleBoundCopy(root);
    TestReparseProtection(root);
    TestRollbackCandidateTrustMatrix(root);

    std::filesystem::remove_all(std::filesystem::path(root));
    if (FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", FailureCount());
        return 1;
    }
    std::printf("All package-trust tests passed.\n");
    return 0;
}
