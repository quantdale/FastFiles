#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ffsetup/PinnedSignatures.h"

namespace ffsetup {

using Sha256Digest = std::array<uint8_t, 32>;

// The release kit carries this small, strict manifest beside the four
// executable payloads. It is deliberately not treated as an authentication
// boundary by itself: each payload is also Authenticode-checked against the
// approved release pins. The manifest binds the package members to one
// coherent build and prevents package mixing.
struct PackageManifestEntry {
    std::wstring fileName;
    uint64_t sizeBytes = 0;
    Sha256Digest sha256{};
};

struct PackageManifest {
    uint32_t schemaVersion = 0;
    std::wstring productVersion;
    std::vector<PackageManifestEntry> files;
};

std::optional<PackageManifest> ReadPackageManifest(const std::wstring& path) noexcept;
const PackageManifestEntry* FindPackageManifestEntry(const PackageManifest& manifest,
                                                     const std::wstring& fileName) noexcept;
bool HasExactPackageFileSet(const PackageManifest& manifest,
                            const std::vector<std::wstring>& expectedNames) noexcept;

std::optional<Sha256Digest> ComputeSha256FileAtPath(const std::wstring& path) noexcept;

struct PackageFileIdentity {
    DWORD volumeSerial = 0;
    DWORD fileIndexHigh = 0;
    DWORD fileIndexLow = 0;
};

class TrustedPackageFile {
public:
    TrustedPackageFile() noexcept = default;
    ~TrustedPackageFile();

    TrustedPackageFile(const TrustedPackageFile&) = delete;
    TrustedPackageFile& operator=(const TrustedPackageFile&) = delete;
    TrustedPackageFile(TrustedPackageFile&& other) noexcept;
    TrustedPackageFile& operator=(TrustedPackageFile&& other) noexcept;

    HANDLE Handle() const noexcept { return handle_; }
    const PackageFileIdentity& Identity() const noexcept { return identity_; }
    const Sha256Digest& Sha256() const noexcept { return sha256_; }
    uint64_t SizeBytes() const noexcept { return sizeBytes_; }
    const std::wstring& FinalPath() const noexcept { return finalPath_; }

private:
    friend std::optional<TrustedPackageFile> OpenTrustedPackageFile(
        const std::wstring&, const std::wstring&, const PackageManifestEntry&,
        const std::vector<Thumbprint>&, const std::function<bool(const std::wstring&, const std::vector<Thumbprint>&)>&) noexcept;

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    PackageFileIdentity identity_{};
    Sha256Digest sha256_{};
    uint64_t sizeBytes_ = 0;
    std::wstring finalPath_;
};

using PackageSignatureVerifier =
    std::function<bool(const std::wstring&, const std::vector<Thumbprint>&)>;
using PackageSignerSelector = std::function<std::vector<Thumbprint>(const std::wstring&)>;

bool ValidateTrustedPackageSet(const std::wstring& packageRoot, const PackageManifest& manifest,
                              const std::vector<std::wstring>& expectedNames,
                              const PackageSignerSelector& signerSelector,
                              const PackageSignatureVerifier& verifier = {}) noexcept;

// Opens the named payload with write sharing disabled, rejects reparse points,
// resolves and confines the handle's final path to packageRoot, validates the
// manifest size/hash and verifies the signer. The returned handle is the same
// underlying file object that later copy operations consume.
std::optional<TrustedPackageFile> OpenTrustedPackageFile(
    const std::wstring& packageRoot, const std::wstring& fileName,
    const PackageManifestEntry& expected, const std::vector<Thumbprint>& approvedSigners,
    const PackageSignatureVerifier& verifier = {}) noexcept;

// Copies bytes from the already-verified source handle into a newly-created
// destination. CREATE_NEW prevents a preplanted scratch file from becoming a
// trusted staging target.
bool CopyTrustedPackageFile(TrustedPackageFile& source, const std::wstring& destinationPath) noexcept;

// Reopens a staged or installed path with reparse processing disabled and
// validates its exact manifest hash/size and approved signer.
bool VerifyPackageFileAtPath(const std::wstring& path, const PackageManifestEntry& expected,
                             const std::vector<Thumbprint>& approvedSigners,
                             const PackageSignatureVerifier& verifier = {}) noexcept;

// Promotes a verified staged file with same-volume MoveFileEx semantics only,
// rejects a reparse-point destination, and verifies the final path after the
// move. A failed post-promotion verification is reported to the caller for
// transaction rollback; no cross-volume CopyFile fallback is permitted.
bool PromoteVerifiedPackageFile(const std::wstring& stagedPath, const std::wstring& destinationPath,
                                const PackageManifestEntry& expected,
                                const std::vector<Thumbprint>& approvedSigners,
                                const PackageSignatureVerifier& verifier = {}) noexcept;

// Returns the two configured runtime signer pins as the approved package
// signer set. An unconfigured build returns an empty set and therefore fails
// closed in all package-trust operations.
std::vector<Thumbprint> ApprovedPackageSigners() noexcept;

} // namespace ffsetup
