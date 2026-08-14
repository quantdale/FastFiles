#include <windows.h>

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
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

std::wstring ExecutableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return L"";
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path().wstring();
}

std::wstring QuoteCommandLineArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

bool RunPackageTrustFixture(const std::wstring& mode, const std::wstring& packageRoot,
                            const std::wstring& statePath) {
    std::vector<wchar_t> pwshPath(32768);
    const DWORD pwshLength = SearchPathW(nullptr, L"pwsh.exe", nullptr,
                                         static_cast<DWORD>(pwshPath.size()), pwshPath.data(), nullptr);
    const std::wstring fixturePath = Join(ExecutableDirectory(), L"PackageTrustFixture.ps1");
    if (pwshLength == 0 || pwshLength >= pwshPath.size() || fixturePath.empty() ||
        !std::filesystem::is_regular_file(std::filesystem::path(fixturePath))) {
        return false;
    }

    std::wstring command = QuoteCommandLineArgument(std::wstring(pwshPath.data(), pwshLength)) +
                           L" -NoProfile -NonInteractive -File " + QuoteCommandLineArgument(fixturePath) +
                           L" -Mode " + QuoteCommandLineArgument(mode) +
                           L" -PackageRoot " + QuoteCommandLineArgument(packageRoot) +
                           L" -StatePath " + QuoteCommandLineArgument(statePath);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startupInfo, &processInfo)) {
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 120000);
    if (waitResult != WAIT_OBJECT_0) {
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return false;
    }
    DWORD exitCode = 1;
    const bool exitCodeRead = GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE;
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return exitCodeRead && exitCode == 0;
}

std::vector<uint8_t> ReadBytes(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::optional<ffsetup::Thumbprint> ReadStateThumbprint(
    const std::wstring& statePath, const char* fieldName) {
    const std::string state = [&] {
        std::ifstream file(std::filesystem::path(statePath), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }();
    const std::string marker = std::string("\"") + fieldName + "\":\"";
    const size_t start = state.find(marker);
    if (start == std::string::npos || start + marker.size() + 40 > state.size()) return std::nullopt;
    const std::string hex = state.substr(start + marker.size(), 40);
    ffsetup::Thumbprint thumbprint{};
    for (size_t index = 0; index < thumbprint.size(); ++index) {
        const char high = hex[index * 2];
        const char low = hex[index * 2 + 1];
        if (!std::isxdigit(static_cast<unsigned char>(high)) ||
            !std::isxdigit(static_cast<unsigned char>(low))) {
            return std::nullopt;
        }
        const auto nibble = [](char value) -> uint8_t {
            if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
            if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
            return static_cast<uint8_t>(value - 'a' + 10);
        };
        thumbprint[index] = static_cast<uint8_t>((nibble(high) << 4) | nibble(low));
    }
    return thumbprint;
}

std::optional<ffsetup::Thumbprint> ReadSignerThumbprint(const std::wstring& statePath) {
    return ReadStateThumbprint(statePath, "leafThumbprint");
}

void WritePayload(const std::wstring& path, const std::string& content) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
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

void WriteManifest(const std::wstring& path, const ffsetup::PackageManifest& manifest) {
    const auto bytes = ffsetup::SerializePackageManifest(manifest);
    if (!bytes) return;
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
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

void WriteBytes(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void TestTimestampedDefaultManifestVerifier(const std::wstring& root) {
    // close-independent-validation-gaps 4.2: prove the final RFC3161 detached
    // CMS format through the native installer verifier and its real trust path.
    const std::wstring timestampRoot = Join(root, L"default-timestamped-manifest");
    const std::wstring statePath = Join(root, L"default-timestamped-manifest.state.json");
    std::filesystem::create_directories(std::filesystem::path(timestampRoot));
    const std::vector<std::wstring> names{L"FastFiles.exe", L"FastFilesEngine.exe",
                                          L"FastFilesIndexSvc.exe", L"FastFilesSetup.exe"};
    for (const auto& name : names) WritePayload(Join(timestampRoot, name), "native-" + AsciiName(name));
    const auto manifest = MakeCanonicalManifest(timestampRoot, names);
    const auto serialized = ffsetup::SerializePackageManifest(manifest);
    Check(serialized.has_value(), "native timestamp fixture has a canonical manifest");
    const std::wstring manifestPath = Join(timestampRoot, ffsetup::kPackageManifestFileName);
    WriteBytes(manifestPath, serialized.value_or(std::vector<uint8_t>{}));
    const auto originalManifest = ReadBytes(manifestPath);

    const bool prepared = RunPackageTrustFixture(L"Prepare", timestampRoot, statePath);
    Check(prepared, "native timestamp fixture prepares a trusted CurrentUser root and detached CMS");
    if (prepared && serialized) {
        Check(ReadBytes(manifestPath) == originalManifest,
              "SignTool timestamping leaves the detached manifest bytes unchanged");
        const auto signer = ReadSignerThumbprint(statePath);
        const std::wstring signaturePath = Join(timestampRoot, ffsetup::kPackageManifestSignatureFileName);
        const auto originalSignature = ReadBytes(signaturePath);
        Check(signer.has_value() && !originalSignature.empty(),
              "native timestamp fixture publishes an exact signer pin and signature bytes");
        if (signer && !originalSignature.empty()) {
            const auto accepted = ffsetup::ReadAuthenticatedPackageManifest(timestampRoot, {*signer});
            Check(accepted.has_value() && accepted->signatureVerified,
                  "DefaultManifestSignatureVerifier accepts the timestamped detached CMS");

            auto wrongSigner = *signer;
            wrongSigner[0] ^= 0xFF;
            Check(!ffsetup::ReadAuthenticatedPackageManifest(timestampRoot, {wrongSigner}).has_value(),
                  "DefaultManifestSignatureVerifier rejects the wrong manifest signer pin");

            WritePayload(manifestPath, "mutated-after-signing");
            Check(!ffsetup::ReadAuthenticatedPackageManifest(timestampRoot, {*signer}).has_value(),
                  "DefaultManifestSignatureVerifier rejects changed detached manifest bytes");
            WriteBytes(manifestPath, *serialized);

            auto changedSignature = originalSignature;
            changedSignature.back() ^= 0x01;
            WriteBytes(signaturePath, changedSignature);
            Check(!ffsetup::ReadAuthenticatedPackageManifest(timestampRoot, {*signer}).has_value(),
                  "DefaultManifestSignatureVerifier rejects changed timestamp/signature bytes");
            WriteBytes(signaturePath, originalSignature);
            Check(ffsetup::ReadAuthenticatedPackageManifest(timestampRoot, {*signer}).has_value(),
                  "the original timestamped CMS remains valid after negative probes");
        }
    }

    const bool cleaned = RunPackageTrustFixture(L"Cleanup", timestampRoot, statePath);
    Check(cleaned && !std::filesystem::exists(std::filesystem::path(timestampRoot)) &&
              !std::filesystem::exists(std::filesystem::path(statePath)),
          "native timestamp fixture removes CurrentUser trust and every temporary file");
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
    const auto manifest = MakeCanonicalManifest(root, names);
    const std::wstring manifestPath = Join(root, L"FastFiles.package.manifest");
    WriteManifest(manifestPath, manifest);
    const auto parsed = ffsetup::ReadPackageManifest(manifestPath);
    Check(parsed.has_value(), "a well-formed package manifest parses");
    Check(parsed.has_value() && ffsetup::HasExactPackageFileSet(*parsed, names),
          "the manifest contains the complete non-mixed shipped payload set");
    Check(parsed.has_value() && ffsetup::FindPackageManifestEntry(*parsed, L"FastFilesEngine.exe") != nullptr,
          "manifest lookup is case-insensitive for a named payload");

    auto leadingZeroVersion = manifest;
    leadingZeroVersion.release.productVersion = L"01.1.0";
    for (auto& entry : leadingZeroVersion.files) entry.productVersion = leadingZeroVersion.release.productVersion;
    Check(!ffsetup::SerializePackageManifest(leadingZeroVersion).has_value(),
          "leading-zero semantic-version components are rejected by the authenticated manifest serializer");

    const std::wstring malformed = Join(root, L"malformed.manifest");
    {
        std::ofstream file(std::filesystem::path(malformed), std::ios::binary | std::ios::trunc);
        file << "FASTFILES_PACKAGE_MANIFEST_V2\nschema=2\nproductName=FastFiles\n"
                "productVersion=0.1.0\nbuildIdentity=0000000000000000000000000000000000000000\n"
                "architecture=x64\nfile=FastFiles.exe|ui|FastFiles.exe|FastFiles.exe|FastFiles|0.1.0|"
                "0000000000000000000000000000000000000000|x64|not-a-size|00\n";
    }
    Check(!ffsetup::ReadPackageManifest(malformed).has_value(),
          "malformed size/hash fields reject the entire package manifest");

    ffsetup::PackageManifest incomplete = manifest;
    incomplete.files.pop_back();
    Check(!ffsetup::HasExactPackageFileSet(incomplete, names),
          "an incomplete package cannot satisfy the exact payload inventory");
    auto mismatched = ManifestEntry(root, names[0]);
    mismatched.sha256[0] ^= 0xFF;
    Check(mismatched.sha256 != manifest.files[0].sha256 &&
              !ffsetup::VerifyPackageFileAtPath(Join(root, names[0]), mismatched, {},
                                                [](HANDLE, const std::wstring&,
                                                   const std::vector<ffsetup::Thumbprint>&) {
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
    const auto approvedVerifier = [&](HANDLE, const std::wstring&,
                                      const std::vector<ffsetup::Thumbprint>& signers) {
        return signers.size() == 1 && ffsetup::ConstantTimeThumbprintEqual(signers[0], approved);
    };

    const std::vector<ffsetup::Thumbprint> wrongSigner(1);
    const auto rejectedVerifier = [](HANDLE, const std::wstring&,
                                     const std::vector<ffsetup::Thumbprint>&) { return false; };
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
    bool renameDenied = false;
    bool handleStillContainsOriginal = false;
    const auto raceVerifier = [&](HANDLE handle, const std::wstring&,
                                  const std::vector<ffsetup::Thumbprint>&) {
        const std::wstring moved = Join(root, L"race-original.bin");
        const bool movedAway = MoveFileExW(racePath.c_str(), moved.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
        renameDenied = !movedAway;
        if (!movedAway) return false;
        WritePayload(racePath, "race-replacement");
        char buffer[32]{};
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) return false;
        DWORD bytesRead = 0;
        if (!ReadFile(handle, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &bytesRead, nullptr)) return false;
        handleStillContainsOriginal = std::string(buffer, bytesRead) == "race-approved";
        DeleteFileW(racePath.c_str());
        MoveFileExW(moved.c_str(), racePath.c_str(), MOVEFILE_REPLACE_EXISTING);
        // Model a path-oriented signer: it would accept the replacement. A
        // handle-bound verifier must instead inspect the original bytes.
        return !handleStillContainsOriginal;
    };
    Check(!ffsetup::OpenTrustedPackageFile(root, L"race.bin", raceExpected, approvedSigners, raceVerifier).has_value(),
          "the exact double-swap attack cannot make the original handle pass signer verification");
    Check(renameDenied, "the trusted source handle denies rename/delete during the trust window");
    Check(!handleStillContainsOriginal || renameDenied,
          "the signature seam observes the same source object rather than the replacement pathname");

    Check(!ffsetup::OpenTrustedPackageFile(
              root, L"payload.bin", expected, approvedSigners,
              [](HANDLE, const std::wstring&, const std::vector<ffsetup::Thumbprint>&) -> bool {
                  throw std::runtime_error("signature callback failure");
              })
              .has_value(),
          "a signer callback exception fails closed");
}

void TestAuthenticatedManifestAndAntiMix(const std::wstring& root) {
    const std::wstring authRoot = Join(root, L"authenticated-manifest");
    std::filesystem::create_directories(std::filesystem::path(authRoot));
    const std::vector<std::wstring> names{L"FastFiles.exe", L"FastFilesEngine.exe",
                                          L"FastFilesIndexSvc.exe", L"FastFilesSetup.exe"};
    for (const auto& name : names) WritePayload(Join(authRoot, name), "auth-" + AsciiName(name));
    const auto manifest = MakeCanonicalManifest(authRoot, names);
    const auto serialized = ffsetup::SerializePackageManifest(manifest);
    Check(serialized.has_value(), "a coherent release identity has a canonical V2 serialization");
    const std::vector<uint8_t> signature{0xC0, 0xDE, 0x01};
    WriteBytes(Join(authRoot, ffsetup::kPackageManifestFileName), serialized.value_or(std::vector<uint8_t>{}));
    WriteBytes(Join(authRoot, ffsetup::kPackageManifestSignatureFileName), signature);
    ffsetup::Thumbprint approved{};
    approved[0] = 0x77;
    const auto accepted = [&](const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& detached,
                              const std::vector<ffsetup::Thumbprint>& signers) {
        return serialized && bytes == *serialized && detached == signature && signers.size() == 1 &&
               ffsetup::ConstantTimeThumbprintEqual(signers[0], approved);
    };
    const auto authenticated = ffsetup::ReadAuthenticatedPackageManifest(authRoot, {approved}, accepted);
    Check(authenticated.has_value() && authenticated->signatureVerified &&
              authenticated->detachedSignature == signature,
          "a coherent manifest and detached release signature authenticate together");
    Check(ffsetup::HasExactPackageDirectoryInventory(authRoot, names),
          "the package directory contains exactly the authenticated payload inventory and trust files");

    WritePayload(Join(authRoot, L"unexpected.exe"), "unexpected");
    Check(!ffsetup::HasExactPackageDirectoryInventory(authRoot, names),
          "an unexpected executable package member is rejected by inventory validation");
    DeleteFileW(Join(authRoot, L"unexpected.exe").c_str());

    DeleteFileW(Join(authRoot, ffsetup::kPackageManifestSignatureFileName).c_str());
    Check(!ffsetup::ReadAuthenticatedPackageManifest(authRoot, {approved}, accepted).has_value(),
          "an unsigned manifest cannot authorize the package");
    WriteBytes(Join(authRoot, ffsetup::kPackageManifestSignatureFileName), signature);
    const auto wrongSigner = [](const std::vector<uint8_t>&, const std::vector<uint8_t>&,
                                const std::vector<ffsetup::Thumbprint>&) { return false; };
    Check(!ffsetup::ReadAuthenticatedPackageManifest(authRoot, {approved}, wrongSigner).has_value(),
          "a manifest signed by an unapproved signer is rejected");
    const auto throwingVerifier = [](const std::vector<uint8_t>&, const std::vector<uint8_t>&,
                                     const std::vector<ffsetup::Thumbprint>&) -> bool {
        throw std::runtime_error("detached signature callback failure");
    };
    Check(!ffsetup::ReadAuthenticatedPackageManifest(authRoot, {approved}, throwingVerifier).has_value(),
          "a detached manifest verifier exception fails closed");

    auto expected = manifest.files[1];
    auto actual = expected;
    Check(ffsetup::PackageFileMetadataMatches(manifest, expected, actual),
          "matching component metadata is accepted");
    const std::vector<std::pair<std::wstring, std::wstring>> mutations{
        {L"componentRole", L"service"}, {L"originalFileName", L"FastFiles.exe"},
        {L"internalName", L"FastFiles.exe"}, {L"productName", L"OtherProduct"},
        {L"productVersion", L"9.9.9"}, {L"buildIdentity", L"1111111111111111111111111111111111111111"},
        {L"architecture", L"arm64"}};
    for (const auto& mutation : mutations) {
        actual = expected;
        if (mutation.first == L"componentRole") actual.componentRole = mutation.second;
        if (mutation.first == L"originalFileName") actual.originalFileName = mutation.second;
        if (mutation.first == L"internalName") actual.internalName = mutation.second;
        if (mutation.first == L"productName") actual.productName = mutation.second;
        if (mutation.first == L"productVersion") actual.productVersion = mutation.second;
        if (mutation.first == L"buildIdentity") actual.buildIdentity = mutation.second;
        if (mutation.first == L"architecture") actual.architecture = mutation.second;
        Check(!ffsetup::PackageFileMetadataMatches(manifest, expected, actual),
              "a release metadata substitution is rejected");
    }
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
    const auto verifier = [](HANDLE, const std::wstring&, const std::vector<ffsetup::Thumbprint>&) { return true; };
    Check(!ffsetup::OpenTrustedPackageFile(root, L"reparse-source.bin", expected, {approved}, verifier).has_value(),
          "a reparse-point source is rejected before signature or copy consumption");

    const std::wstring regular = Join(root, L"regular.bin");
    WritePayload(regular, "regular");
    const auto regularExpected = ManifestEntry(root, L"regular.bin");
    auto trusted = ffsetup::OpenTrustedPackageFile(root, L"regular.bin", regularExpected, {approved}, verifier);
    const std::wstring stage = Join(root, L"reparse-stage.bin");
    Check(trusted.has_value() && ffsetup::CopyTrustedPackageFile(*trusted, stage),
          "a regular payload can be staged before destination reparse validation");

    const std::wstring hardLink = Join(root, L"regular-hardlink.bin");
    if (CreateHardLinkW(hardLink.c_str(), regular.c_str(), nullptr) != FALSE) {
        const auto hardLinkExpected = ManifestEntry(root, L"regular-hardlink.bin");
        Check(!ffsetup::OpenTrustedPackageFile(root, L"regular-hardlink.bin", hardLinkExpected, {approved}, verifier)
                   .has_value(),
              "a hard-linked source is rejected instead of creating an ambiguous package identity");
        DeleteFileW(hardLink.c_str());
    } else {
        std::printf("SKIPPED: hard-link fixture is unavailable on this host.\n");
    }
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
    auto manifest = MakeManifest(candidateRoot, names);
    manifest.signatureVerified = true;
    manifest.detachedSignature = {0x01};
    WritePayload(Join(candidateRoot, ffsetup::kPackageManifestFileName), "fixture-manifest");
    WritePayload(Join(candidateRoot, ffsetup::kPackageManifestSignatureFileName), "fixture-signature");
    ffsetup::Thumbprint approved{};
    approved[0] = 0x5A;
    const auto selector = [&](const std::wstring&) { return std::vector<ffsetup::Thumbprint>{approved}; };
    const auto accepted = [](HANDLE, const std::wstring&,
                             const std::vector<ffsetup::Thumbprint>& signers) {
        return signers.size() == 1 && signers[0][0] == 0x5A;
    };
    Check(ffsetup::ValidateTrustedPackageSet(candidateRoot, manifest, names, selector, accepted),
          "a complete signed previous installation is an eligible rollback candidate");
    Check(!ffsetup::ValidateTrustedPackageSet(candidateRoot, manifest, names, selector,
                                               [](HANDLE, const std::wstring&,
                                                  const std::vector<ffsetup::Thumbprint>&) {
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
    TestAuthenticatedManifestAndAntiMix(root);
    TestTimestampedDefaultManifestVerifier(root);
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
