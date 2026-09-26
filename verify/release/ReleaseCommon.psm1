Set-StrictMode -Version Latest

if (-not ('FastFilesReleaseArtifactReadFile' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class FastFilesReleaseArtifactReadFile
{
    private const uint GenericRead = 0x80000000;
    private const uint FileShareRead = 0x00000001;
    private const uint OpenExisting = 3;
    private const uint OpenReparsePoint = 0x00200000;
    private const uint AttributeDirectory = 0x00000010;
    private const uint AttributeReparsePoint = 0x00000400;

    [StructLayout(LayoutKind.Sequential)]
    private struct FileAttributeTagInfo
    {
        public uint FileAttributes;
        public uint ReparseTag;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, EntryPoint = "CreateFileW")]
    private static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security,
        uint creation, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetFileInformationByHandleEx(SafeFileHandle handle, int infoClass,
        out FileAttributeTagInfo info, uint size);

    public static FileStream OpenRegularReadOnly(string path)
    {
        SafeFileHandle handle = CreateFile(path, GenericRead, FileShareRead, IntPtr.Zero,
            OpenExisting, OpenReparsePoint, IntPtr.Zero);
        if (handle.IsInvalid)
        {
            int error = Marshal.GetLastWin32Error();
            handle.Dispose();
            throw new Win32Exception(error);
        }

        try
        {
            FileAttributeTagInfo info;
            if (!GetFileInformationByHandleEx(handle, 9, out info,
                (uint)Marshal.SizeOf(typeof(FileAttributeTagInfo))))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            if ((info.FileAttributes & AttributeReparsePoint) != 0)
            {
                throw new IOException("Release artifact input cannot be a reparse point.");
            }
            if ((info.FileAttributes & AttributeDirectory) != 0)
            {
                throw new IOException("Release artifact input must be a regular file.");
            }
            return new FileStream(handle, FileAccess.Read, 65536, false);
        }
        catch
        {
            handle.Dispose();
            throw;
        }
    }
}
'@ -Language CSharp -ErrorAction Stop
}

$script:ReleaseArtifactDefinitions = @(
    [pscustomobject]@{ Name = 'FastFiles.exe'; PackagePath = 'FastFiles.exe'; BuildPath = 'src\ui\FastFiles.exe'; Role = 'ui'; Pin = $null },
    [pscustomobject]@{ Name = 'FastFilesEngine.exe'; PackagePath = 'FastFilesEngine.exe'; BuildPath = 'src\engine\FastFilesEngine.exe'; Role = 'engine'; Pin = 'engine' },
    [pscustomobject]@{ Name = 'FastFilesIndexSvc.exe'; PackagePath = 'FastFilesIndexSvc.exe'; BuildPath = 'src\indexsvc\FastFilesIndexSvc.exe'; Role = 'service'; Pin = 'service' },
    [pscustomobject]@{ Name = 'FastFilesSetup.exe'; PackagePath = 'FastFilesSetup.exe'; BuildPath = 'src\installer\FastFilesSetup.exe'; Role = 'installer'; Pin = $null }
)

$script:CodeSigningApplicationPolicyOid = '1.3.6.1.5.5.7.3.3'
$script:ReleaseArtifactSnapshotCapturedForTest = $null

function Normalize-Thumbprint {
    param([AllowNull()] [string] $Value)

    if ($null -eq $Value) { return $null }
    $normalized = ($Value -replace '[\s-]', '').ToUpperInvariant()
    if ($normalized.Length -eq 0) { return $null }
    return $normalized
}

function Test-ThumbprintFormat {
    param([AllowNull()] [string] $Value)

    $normalized = Normalize-Thumbprint -Value $Value
    return $null -ne $normalized -and $normalized -match '^[0-9A-F]{40}$'
}

function Test-UsableThumbprint {
    param([AllowNull()] [string] $Value)

    $normalized = Normalize-Thumbprint -Value $Value
    $formatValid = [bool](Test-ThumbprintFormat -Value $normalized)
    return [bool]($formatValid -and ($normalized -notmatch '^0+$'))
}

function Get-CertificatePolicyCore {
    [CmdletBinding()]
    param(
        [AllowNull()] $Certificate,
        [switch] $RequirePrivateKey,
        [AllowNull()] [Security.Cryptography.X509Certificates.X509Certificate2] $CustomTrustRoot
    )

    $issues = [System.Collections.Generic.List[string]]::new()
    $chainStatus = [System.Collections.Generic.List[string]]::new()
    $chainBuilt = $false
    $chainElementCount = 0
    $hasPrivateKey = $false
    $isSelfSigned = $false
    $isCurrentlyValid = $false
    $hasCodeSigningEku = $false
    $notBefore = $null
    $notAfter = $null

    if ($null -eq $Certificate) {
        [void]$issues.Add('CERTIFICATE_MISSING')
    } else {
        $certificate2 = $null
        try {
            if (-not ($Certificate -is [Security.Cryptography.X509Certificates.X509Certificate2])) {
                throw 'CERTIFICATE_TYPE_INVALID'
            }
            $certificate2 = $Certificate
            $hasPrivateKey = [bool]$certificate2.HasPrivateKey
        } catch {
            [void]$issues.Add('CERTIFICATE_TYPE_INVALID')
        }

        $subject = $null
        $issuer = $null
        try {
            $subject = [string]$Certificate.Subject
            $issuer = [string]$Certificate.Issuer
        } catch {
            [void]$issues.Add('CERTIFICATE_IDENTITY_UNREADABLE')
        }
        if ([string]::IsNullOrWhiteSpace($subject) -or [string]::IsNullOrWhiteSpace($issuer)) {
            [void]$issues.Add('CERTIFICATE_IDENTITY_MISSING')
        } elseif ($subject -ceq $issuer) {
            $isSelfSigned = $true
            [void]$issues.Add('CERTIFICATE_SELF_SIGNED')
        }

        try {
            $notBefore = ([DateTime]$Certificate.NotBefore).ToUniversalTime()
            $notAfter = ([DateTime]$Certificate.NotAfter).ToUniversalTime()
            $now = [DateTime]::UtcNow
            $isCurrentlyValid = $notBefore -le $now -and $now -le $notAfter
            if (-not $isCurrentlyValid) { [void]$issues.Add('CERTIFICATE_OUTSIDE_CURRENT_VALIDITY') }
        } catch {
            [void]$issues.Add('CERTIFICATE_VALIDITY_UNREADABLE')
        }

        if ($RequirePrivateKey -and -not $hasPrivateKey) {
            [void]$issues.Add('CERTIFICATE_PRIVATE_KEY_REQUIRED')
        }

        try {
            $ekuExtensions = @($Certificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' })
            if ($ekuExtensions.Count -eq 0) {
                [void]$issues.Add('CERTIFICATE_CODE_SIGNING_EKU_MISSING')
            } else {
                $eku = [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]$ekuExtensions[0]
                $ekuOids = @($eku.EnhancedKeyUsages | ForEach-Object { $_.Value })
                $hasCodeSigningEku = $ekuOids -contains $script:CodeSigningApplicationPolicyOid
                if (-not $hasCodeSigningEku) { [void]$issues.Add('CERTIFICATE_CODE_SIGNING_EKU_MISSING') }
            }
        } catch {
            [void]$issues.Add('CERTIFICATE_CODE_SIGNING_EKU_INVALID')
        }

        if ($null -ne $certificate2) {
            $chain = $null
            try {
                $chain = [Security.Cryptography.X509Certificates.X509Chain]::new()
                $chain.ChainPolicy.RevocationMode = [Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
                $chain.ChainPolicy.RevocationFlag = [Security.Cryptography.X509Certificates.X509RevocationFlag]::EntireChain
                $chain.ChainPolicy.VerificationFlags = [Security.Cryptography.X509Certificates.X509VerificationFlags]::NoFlag
                [void]$chain.ChainPolicy.ApplicationPolicy.Add(
                    [Security.Cryptography.Oid]::new($script:CodeSigningApplicationPolicyOid))
                if ($null -ne $CustomTrustRoot) {
                    $chain.ChainPolicy.TrustMode =
                        [Security.Cryptography.X509Certificates.X509ChainTrustMode]::CustomRootTrust
                    [void]$chain.ChainPolicy.CustomTrustStore.Add($CustomTrustRoot)
                }
                $chainBuilt = [bool]$chain.Build($certificate2)
                $chainElementCount = $chain.ChainElements.Count
                if ($chainBuilt -and $chainElementCount -lt 2) {
                    # A production leaf must have an issuer/root above it;
                    # accepting a one-element successful chain would permit a
                    # self-signed or root certificate to act as the signer.
                    $isSelfSigned = $true
                    [void]$issues.Add('CERTIFICATE_SELF_SIGNED_OR_ROOT_LEAF')
                }
                foreach ($status in @($chain.ChainStatus | Where-Object { $_.Status -ne [Security.Cryptography.X509Certificates.X509ChainStatusFlags]::NoError })) {
                    [void]$chainStatus.Add($status.Status.ToString())
                }
                if (-not $chainBuilt -or $chainElementCount -lt 2 -or $chainStatus.Count -gt 0) {
                    [void]$issues.Add('CERTIFICATE_CHAIN_UNTRUSTED_OR_INVALID')
                }
            } catch {
                [void]$issues.Add('CERTIFICATE_CHAIN_BUILD_FAILED')
            } finally {
                if ($chain) { $chain.Dispose() }
            }
        } else {
            [void]$issues.Add('CERTIFICATE_CHAIN_BUILD_FAILED')
        }
    }

    [pscustomobject]@{
        valid = $issues.Count -eq 0
        reasons = @($issues.ToArray())
        hasPrivateKey = $hasPrivateKey
        selfSigned = $isSelfSigned
        currentlyValid = $isCurrentlyValid
        notBefore = $notBefore
        notAfter = $notAfter
        codeSigningEku = $hasCodeSigningEku
        chainValid = $chainBuilt -and $chainStatus.Count -eq 0 -and $chainElementCount -ge 2
        chainStatus = @($chainStatus.ToArray())
        chainElementCount = $chainElementCount
        chainTrustMode = if ($null -ne $CustomTrustRoot) { 'CUSTOM_ROOT_TEST_ONLY' } else { 'WINDOWS_DEFAULT_TRUST_POLICY' }
        revocationPolicy = 'NOT_CHECKED_OFFLINE'
        revocationChecked = $false
        applicationPolicy = $script:CodeSigningApplicationPolicyOid
    }
}

function Get-ProductionCertificatePolicy {
    [CmdletBinding()]
    param(
        [AllowNull()] $Certificate,
        [switch] $RequirePrivateKey
    )

    return Get-CertificatePolicyCore -Certificate $Certificate -RequirePrivateKey:$RequirePrivateKey
}

# Signed-artifact verification is deliberately not a signing-credential
# preflight. WinTrust owns Authenticode validity, including the signing time
# carried by a valid RFC3161 timestamp. In particular, an expired leaf is not
# independently rejected here when WinTrust reports the timestamped artifact
# as valid. Get-CertificatePolicyCore remains the stricter policy for choosing
# a certificate that will sign a new release.
function Get-SignedArtifactVerificationPolicy {
    [CmdletBinding()]
    param(
        [AllowNull()] $Certificate,
        [Parameter(Mandatory)] [string] $AuthenticodeStatus,
        [AllowNull()] $TimestampCertificate
    )

    $issues = [System.Collections.Generic.List[string]]::new()
    $hasCodeSigningEku = $false
    $isSelfSigned = $false
    $currentlyValid = $false
    $notBefore = $null
    $notAfter = $null
    $thumbprint = $null

    if ($null -eq $Certificate) {
        [void]$issues.Add('SIGNER_CERTIFICATE_MISSING')
    } else {
        try {
            $thumbprint = Normalize-Thumbprint -Value ([string]$Certificate.Thumbprint)
            $notBefore = ([DateTime]$Certificate.NotBefore).ToUniversalTime()
            $notAfter = ([DateTime]$Certificate.NotAfter).ToUniversalTime()
            $now = [DateTime]::UtcNow
            $currentlyValid = $notBefore -le $now -and $now -le $notAfter
        } catch {
            [void]$issues.Add('SIGNER_CERTIFICATE_IDENTITY_UNREADABLE')
        }
        try {
            if ([string]::IsNullOrWhiteSpace([string]$Certificate.Subject) -or
                [string]::IsNullOrWhiteSpace([string]$Certificate.Issuer)) {
                [void]$issues.Add('SIGNER_CERTIFICATE_IDENTITY_MISSING')
            } elseif ([string]$Certificate.Subject -ceq [string]$Certificate.Issuer) {
                $isSelfSigned = $true
                [void]$issues.Add('SIGNER_CERTIFICATE_SELF_SIGNED')
            }
        } catch {
            [void]$issues.Add('SIGNER_CERTIFICATE_IDENTITY_UNREADABLE')
        }
        try {
            $ekuExtensions = @($Certificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' })
            if ($ekuExtensions.Count -gt 0) {
                $eku = [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]$ekuExtensions[0]
                $hasCodeSigningEku = @($eku.EnhancedKeyUsages | ForEach-Object { $_.Value }) -contains $script:CodeSigningApplicationPolicyOid
            }
            if (-not $hasCodeSigningEku) { [void]$issues.Add('SIGNER_CODE_SIGNING_EKU_MISSING') }
        } catch {
            [void]$issues.Add('SIGNER_CODE_SIGNING_EKU_INVALID')
        }
        if (-not (Test-ThumbprintFormat -Value $thumbprint)) {
            [void]$issues.Add('SIGNER_THUMBPRINT_INVALID')
        }
    }

    $authenticodeValid = $AuthenticodeStatus -ceq 'Valid'
    if (-not $authenticodeValid) { [void]$issues.Add('AUTHENTICODE_STATUS_NOT_VALID') }
    $timestampSigned = $null -ne $TimestampCertificate
    $timestampValid = $timestampSigned -and $authenticodeValid
    [pscustomobject]@{
        valid = $authenticodeValid -and $issues.Count -eq 0
        reasons = @($issues.ToArray())
        authenticodeStatus = $AuthenticodeStatus
        authenticodeValid = $authenticodeValid
        signerKnown = $null -ne $Certificate
        signerThumbprint = $thumbprint
        selfSigned = $isSelfSigned
        currentlyValid = $currentlyValid
        currentValidityEnforced = $false
        notBefore = $notBefore
        notAfter = $notAfter
        codeSigningEku = $hasCodeSigningEku
        chainValiditySource = 'WINDOWS_WINTRUST_AUTHENTICODE'
        timestampSigned = $timestampSigned
        timestampValid = $timestampValid
        timestampPolicy = if ($timestampValid) { 'RFC3161_AUTHENTICATED_BY_WINTRUST' } elseif ($timestampSigned) { 'WINTRUST_REJECTED' } else { 'NO_TIMESTAMP' }
        revocationPolicy = 'WINDOWS_WINTRUST_DEFAULT'
        revocationChecked = $null
    }
}

# Test-only technical seam. Production release paths intentionally call
# Get-ProductionCertificatePolicy without a custom root so Windows trust policy
# remains authoritative for real signer selection and artifact validation.
function Get-ProductionCertificatePolicyForTest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $Certificate,
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $CustomTrustRoot,
        [switch] $RequirePrivateKey
    )

    return Get-CertificatePolicyCore -Certificate $Certificate -CustomTrustRoot $CustomTrustRoot `
        -RequirePrivateKey:$RequirePrivateKey
}

function Test-ProductionCertificate {
    [CmdletBinding()]
    param(
        [AllowNull()] $Certificate,
        [switch] $RequirePrivateKey
    )

    return [bool](Get-ProductionCertificatePolicy -Certificate $Certificate -RequirePrivateKey:$RequirePrivateKey).valid
}

function Get-PinState {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [AllowNull()] [string] $Value,
        [string] $Source = 'unknown'
    )

    $normalized = Normalize-Thumbprint -Value $Value
    $state = if ($null -eq $normalized) {
        'MISSING'
    } elseif ($normalized -match '^0+$') {
        'PLACEHOLDER'
    } elseif ($normalized -notmatch '^[0-9A-F]{40}$') {
        'MALFORMED'
    } else {
        'POPULATED'
    }
    [pscustomobject]@{
        name = $Name
        state = $state
        source = $Source
        length = if ($null -eq $normalized) { 0 } else { $normalized.Length }
        normalized = $normalized
    }
}

function Get-CMakeCacheValue {
    param([Parameter(Mandatory)] [string] $CachePath, [Parameter(Mandatory)] [string] $Name)

    if (-not (Test-Path -LiteralPath $CachePath)) { return $null }
    $prefix = $Name + ':'
    $line = Get-Content -LiteralPath $CachePath | Where-Object { $_.StartsWith($prefix, [StringComparison]::Ordinal) } | Select-Object -First 1
    if ($null -eq $line) { return $null }
    $separator = $line.IndexOf('=')
    if ($separator -lt 0) { return $null }
    return $line.Substring($separator + 1).Trim()
}

function Get-ConfiguredPinStates {
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [string] $BuildRoot
    )

    if (-not $BuildRoot) { $BuildRoot = Join-Path $RepoRoot 'build\release' }
    $cachePath = [IO.Path]::GetFullPath((Join-Path $BuildRoot 'CMakeCache.txt'))
    $engine = Get-PinState -Name 'engine' -Value (Get-CMakeCacheValue -CachePath $cachePath -Name 'FASTFILES_ENGINE_SIGNATURE_THUMBPRINT') -Source $cachePath
    $service = Get-PinState -Name 'service' -Value (Get-CMakeCacheValue -CachePath $cachePath -Name 'FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT') -Source $cachePath
    [pscustomobject]@{
        engine = $engine
        service = $service
        buildRoot = [IO.Path]::GetFullPath($BuildRoot)
        cachePresent = Test-Path -LiteralPath $cachePath
    }
}

function Get-ReleaseArtifactPaths {
    param([Parameter(Mandatory)] [string] $ArtifactRoot)

    $root = [IO.Path]::GetFullPath($ArtifactRoot)
    $packageLayout = @($script:ReleaseArtifactDefinitions | ForEach-Object { Join-Path $root $_.Name })
    $buildLayout = @($script:ReleaseArtifactDefinitions | ForEach-Object { Join-Path $root $_.BuildPath })
    $layout = if (@($packageLayout | Where-Object { -not (Test-Path -LiteralPath $_) }).Count -eq 0) {
        'package'
    } elseif (@($buildLayout | Where-Object { -not (Test-Path -LiteralPath $_) }).Count -eq 0) {
        'build'
    } else {
        throw "Release artifact root is incomplete. Expected either the four package files directly under '$root' or the Release build layout under '$root'."
    }

    $index = 0
    return @($script:ReleaseArtifactDefinitions | ForEach-Object {
        $path = if ($layout -eq 'package') { Join-Path $root $_.Name } else { Join-Path $root $_.BuildPath }
        $record = [pscustomobject]@{
            order = $index
            name = $_.Name
            packagePath = $_.PackagePath
            sourcePath = [IO.Path]::GetFullPath($path)
            componentRole = $_.Role
            pin = $_.Pin
        }
        $index++
        $record
    })
}

function Resolve-ReleaseKitPackageRoot {
    param([Parameter(Mandatory)] [string] $Root)

    $fullRoot = [IO.Path]::GetFullPath($Root)
    $rootItem = Get-Item -LiteralPath $fullRoot -ErrorAction Stop
    if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "KIT_PACKAGE_ROOT_INVALID: $fullRoot"
    }
    $kitManifestPath = Join-Path $fullRoot 'kit-manifest.json'
    $directManifestPath = Join-Path $fullRoot 'FastFiles.package.manifest'
    if (Test-Path -LiteralPath $kitManifestPath -PathType Leaf) {
        try {
            $kitManifest = Get-Content -LiteralPath $kitManifestPath -Raw | ConvertFrom-Json
        } catch {
            throw "KIT_MANIFEST_INVALID: $kitManifestPath"
        }
        if ([int]$kitManifest.schemaVersion -ne 1 -or
            [string]$kitManifest.kind -cne 'fastfiles-clean-host-kit' -or
            [string]$kitManifest.packageRoot -cne 'package') {
            throw "KIT_PACKAGE_ROOT_INVALID: $kitManifestPath"
        }
        $nestedRoot = Join-Path $fullRoot 'package'
        $nestedItem = if (Test-Path -LiteralPath $nestedRoot -PathType Container) { Get-Item -LiteralPath $nestedRoot -ErrorAction Stop } else { $null }
        if ($null -eq $nestedItem -or ($nestedItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            -not (Test-Path -LiteralPath (Join-Path $nestedRoot 'FastFiles.package.manifest') -PathType Leaf)) {
            throw "KIT_PACKAGE_ROOT_MISSING: $nestedRoot"
        }
        if (Test-Path -LiteralPath $directManifestPath -PathType Leaf) {
            throw "KIT_PACKAGE_ROOT_AMBIGUOUS: both kit and direct package manifests exist under '$fullRoot'."
        }
        return [IO.Path]::GetFullPath($nestedRoot)
    }
    if (Test-Path -LiteralPath $directManifestPath -PathType Leaf) {
        return $fullRoot
    }
    throw "PACKAGE_ROOT_NOT_FOUND: expected a direct package manifest or a generated kit under '$fullRoot'."
}

function Get-PeArchitecture {
    param([Parameter(Mandatory)] [string] $Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $stream = $null
    $reader = $null
    try {
        $stream = [IO.File]::OpenRead($fullPath)
        if ($stream.Length -lt 0x40) { throw "PE image is too small: $fullPath" }
        $reader = [IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Missing DOS header: $fullPath" }
        $stream.Seek(0x3c, [IO.SeekOrigin]::Begin) | Out-Null
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset -gt $stream.Length - 6) { throw "Invalid PE header offset: $fullPath" }
        $stream.Seek($peOffset, [IO.SeekOrigin]::Begin) | Out-Null
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Missing PE signature: $fullPath" }
        $machine = $reader.ReadUInt16()
        $architecture = switch ($machine) {
            0x8664 { 'x64' }
            0xAA64 { 'arm64' }
            0x014c { 'x86' }
            default { 'unknown' }
        }
        [pscustomobject]@{ architecture = $architecture; machine = ('0x{0:X4}' -f $machine) }
    } finally {
        if ($reader) { $reader.Dispose() }
        elseif ($stream) { $stream.Dispose() }
    }
}

function Get-SignatureRecord {
    param([Parameter(Mandatory)] [string] $Path)

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    $signer = $signature.SignerCertificate
    $timestamp = $signature.TimeStamperCertificate
    $isSigned = $signature.Status -ne 'NotSigned' -and $null -ne $signer
    $artifactPolicy = if ($signer) {
        Get-SignedArtifactVerificationPolicy -Certificate $signer -AuthenticodeStatus ([string]$signature.Status) `
            -TimestampCertificate $timestamp
    } else {
        $null
    }
    [pscustomobject]@{
        signed = [bool]$isSigned
        valid = [bool]($artifactPolicy -and $artifactPolicy.valid)
        status = [string]$signature.Status
        signerSubject = if ($signer) { [string]$signer.Subject } else { $null }
        leafThumbprint = if ($signer) { Normalize-Thumbprint -Value $signer.Thumbprint } else { $null }
        certificatePolicy = if ($artifactPolicy) { $artifactPolicy } else { $null }
        verificationPolicy = 'AUTHENTICODE_WINTRUST_TIMESTAMP_AWARE'
        timestampSigned = $null -ne $timestamp
        timestampValid = if ($timestamp) { [bool]($signature.Status -eq 'Valid') } else { $null }
        timestampStatus = if ($null -eq $timestamp) { 'MISSING' } elseif ($signature.Status -eq 'Valid') { 'VALID' } else { 'INVALID' }
        timestampSubject = if ($timestamp) { [string]$timestamp.Subject } else { $null }
        timestampThumbprint = if ($timestamp) { Normalize-Thumbprint -Value $timestamp.Thumbprint } else { $null }
    }
}

function Get-FileVersionRecord {
    param([Parameter(Mandatory)] [string] $Path)

    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    $configurationMatch = [regex]::Match([string]$info.SpecialBuild, '(?:^|;)config=([^;]+)')
    [pscustomobject]@{
        productVersion = if ([string]::IsNullOrWhiteSpace($info.ProductVersion)) { $null } else { $info.ProductVersion }
        fileVersion = if ([string]::IsNullOrWhiteSpace($info.FileVersion)) { $null } else { $info.FileVersion }
        productName = if ([string]::IsNullOrWhiteSpace($info.ProductName)) { $null } else { $info.ProductName }
        companyName = if ([string]::IsNullOrWhiteSpace($info.CompanyName)) { $null } else { $info.CompanyName }
        fileDescription = if ([string]::IsNullOrWhiteSpace($info.FileDescription)) { $null } else { $info.FileDescription }
        internalName = if ([string]::IsNullOrWhiteSpace($info.InternalName)) { $null } else { $info.InternalName }
        originalFilename = if ([string]::IsNullOrWhiteSpace($info.OriginalFilename)) { $null } else { $info.OriginalFilename }
        buildIdentity = if ([string]::IsNullOrWhiteSpace($info.PrivateBuild)) { $null } else { $info.PrivateBuild }
        configuration = if ($configurationMatch.Success) { $configurationMatch.Groups[1].Value.Trim() } else { $null }
    }
}

function Get-ExpectedArtifactBuildConfiguration {
    param([Parameter(Mandatory)] [string] $Configuration)

    switch ($Configuration) {
        'release-test' { return 'RelWithDebInfo' }
        'Release' { return 'Release' }
        'Debug' { return 'Debug' }
        default { return $null }
    }
}

function Test-StrictProductVersion {
    param([AllowNull()] [string] $Value)

    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        return $false
    }
    foreach ($part in @($Matches[1], $Matches[2], $Matches[3])) {
        if ([uint64]$part -gt 65535) { return $false }
    }
    return $true
}

function Compare-StrictProductVersion {
    param([Parameter(Mandatory)] [string] $Left, [Parameter(Mandatory)] [string] $Right)

    if (-not (Test-StrictProductVersion $Left) -or -not (Test-StrictProductVersion $Right)) {
        throw 'PRODUCT_VERSION_INVALID'
    }
    $leftParts = @($Left -split '\.' | ForEach-Object { [uint64]$_ })
    $rightParts = @($Right -split '\.' | ForEach-Object { [uint64]$_ })
    for ($index = 0; $index -lt 3; $index++) {
        if ($leftParts[$index] -lt $rightParts[$index]) { return -1 }
        if ($leftParts[$index] -gt $rightParts[$index]) { return 1 }
    }
    return 0
}

function Get-RepositoryProductVersion {
    param([Parameter(Mandatory)] [string] $RepoRoot)

    $path = Join-Path ([IO.Path]::GetFullPath($RepoRoot)) 'VERSION'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "PRODUCT_VERSION_SOURCE_MISSING: $path"
    }
    $version = ([IO.File]::ReadAllText($path)).Trim()
    if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw "PRODUCT_VERSION_SOURCE_INVALID: $path"
    }
    return $version
}

function Test-ExecutableVersionMetadata {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $ExpectedProductVersion,
        [Parameter(Mandatory)] [string] $ExpectedFileVersion,
        [Parameter(Mandatory)] [string] $ExpectedOriginalFilename,
        [string] $ExpectedBuildIdentity
    )

    $issues = [System.Collections.Generic.List[string]]::new()
    $metadata = $null
    try {
        $fullPath = [IO.Path]::GetFullPath($Path)
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            $issues.Add('MISSING')
        } else {
            $metadata = Get-FileVersionRecord -Path $fullPath
            if ($metadata.productVersion -cne $ExpectedProductVersion) { $issues.Add('PRODUCT_VERSION_MISMATCH') }
            if ($metadata.fileVersion -cne $ExpectedFileVersion) { $issues.Add('FILE_VERSION_MISMATCH') }
            if ($metadata.productName -cne 'FastFiles') { $issues.Add('PRODUCT_NAME_MISMATCH') }
            if ($metadata.companyName -cne 'FastFiles') { $issues.Add('COMPANY_NAME_MISMATCH') }
            if ([string]::IsNullOrWhiteSpace($metadata.fileDescription)) { $issues.Add('FILE_DESCRIPTION_MISSING') }
            if ($metadata.internalName -ine $ExpectedOriginalFilename) { $issues.Add('INTERNAL_NAME_MISMATCH') }
            if ($metadata.originalFilename -ine $ExpectedOriginalFilename) { $issues.Add('ORIGINAL_FILENAME_MISMATCH') }
            if ($metadata.buildIdentity -notmatch '^[0-9a-fA-F]{40}$') { $issues.Add('BUILD_IDENTITY_MISSING_OR_INVALID') }
            if ($ExpectedBuildIdentity -and $metadata.buildIdentity -ine $ExpectedBuildIdentity) { $issues.Add('BUILD_IDENTITY_MISMATCH') }
        }
    } catch {
        $issues.Add('VERSION_RESOURCE_READ_ERROR')
    }
    [pscustomobject]@{
        path = [IO.Path]::GetFullPath($Path)
        filename = $ExpectedOriginalFilename
        valid = $issues.Count -eq 0
        issues = @($issues)
        metadata = $metadata
    }
}

function Test-ReleaseVersionMetadata {
    param(
        [Parameter(Mandatory)] [string] $ArtifactRoot,
        [Parameter(Mandatory)] [string] $RepoRoot,
        [string[]] $AdditionalPaths = @()
    )

    $expectedProductVersion = Get-RepositoryProductVersion -RepoRoot $RepoRoot
    $expectedFileVersion = "$expectedProductVersion.0"
    $expectedBuildIdentity = Get-RepositoryCommit -RepoRoot $RepoRoot
    # Fail closed when HEAD cannot be resolved: comparing against a null
    # expected identity would silently downgrade BUILD_IDENTITY_MISMATCH to a
    # presence-only check and let stale binaries qualify.
    if (-not $expectedBuildIdentity -or $expectedBuildIdentity -notmatch '^[0-9a-fA-F]{40}$') {
        throw 'PROVENANCE_MISMATCH:HEAD_UNAVAILABLE: expected=40-hex repository HEAD; actual=unavailable; binary=all; check=head-binding; detail=git rev-parse HEAD failed'
    }
    $checks = [System.Collections.Generic.List[object]]::new()
    foreach ($entry in @(Get-ReleaseArtifactPaths -ArtifactRoot $ArtifactRoot)) {
        $checks.Add((Test-ExecutableVersionMetadata -Path $entry.sourcePath -ExpectedProductVersion $expectedProductVersion `
                -ExpectedFileVersion $expectedFileVersion -ExpectedOriginalFilename $entry.name -ExpectedBuildIdentity $expectedBuildIdentity))
    }
    foreach ($path in @($AdditionalPaths)) {
        if ([string]::IsNullOrWhiteSpace($path)) { continue }
        $fullPath = [IO.Path]::GetFullPath($path)
        $checks.Add((Test-ExecutableVersionMetadata -Path $fullPath -ExpectedProductVersion $expectedProductVersion `
                -ExpectedFileVersion $expectedFileVersion -ExpectedOriginalFilename ([IO.Path]::GetFileName($fullPath)) -ExpectedBuildIdentity $expectedBuildIdentity))
    }
    [pscustomobject]@{
        valid = @($checks | Where-Object { -not $_.valid }).Count -eq 0
        expectedProductVersion = $expectedProductVersion
        expectedFileVersion = $expectedFileVersion
        expectedBuildIdentity = $expectedBuildIdentity
        artifacts = @($checks)
    }
}

function Get-RepositoryCommit {
    param([Parameter(Mandatory)] [string] $RepoRoot)

    try {
        $commit = git -C $RepoRoot rev-parse HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $commit) { return ([string](@($commit) | Select-Object -First 1)).Trim() }
    } catch { }
    return $null
}

# Label the artifact root with the build configuration that produced it. The
# release-test preset and the production release preset share the same artifact
# filenames, so a default of 'Release' would silently mislabel unsigned
# validation kits as Release configuration evidence.
function Get-ReleaseBuildConfigurationLabel {
    param([Parameter(Mandatory)] [string] $ArtifactRoot)

    $normalized = $ArtifactRoot.Replace('/', '\').TrimEnd('\')
    if ($normalized -match '(?i)(?:^|[\\/])release-test(?:[\\/]|$)') { return 'release-test' }
    if ($normalized -match '(?i)(?:^|[\\/])release(?:[\\/]|$)') { return 'Release' }
    if ($normalized -match '(?i)(?:^|[\\/])debug(?:-diagnostic)?(?:[\\/]|$)') { return 'Debug' }
    if ($normalized -match '(?i)(?:^|[\\/])asan(?:[\\/]|$)') { return 'asan' }
    if ($normalized -match '(?i)(?:^|[\\/])analyze(?:[\\/]|$)') { return 'analyze' }
    return 'Release'
}

function Get-ReleaseArtifactManifest {
    param(
        [Parameter(Mandatory)] [string] $ArtifactRoot,
        [string] $RepoRoot,
        [string] $Configuration = $null
    )

    if ([string]::IsNullOrWhiteSpace($Configuration)) {
        $Configuration = Get-ReleaseBuildConfigurationLabel -ArtifactRoot $ArtifactRoot
    }

    $paths = Get-ReleaseArtifactPaths -ArtifactRoot $ArtifactRoot
    $expectedBuildConfiguration = Get-ExpectedArtifactBuildConfiguration -Configuration $Configuration
    $records = [System.Collections.Generic.List[object]]::new()
    foreach ($artifactPath in @($paths | Sort-Object order)) {
        $snapshotPath = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-artifact-' + [guid]::NewGuid().ToString('N') + '.exe')
        $sourceStream = $null
        $snapshotWriter = $null
        $snapshotGuard = $null
        try {
            # Open the selected executable once without write/delete sharing and
            # reject a final-component reparse point. Every exported fact below
            # is collected from the same guarded snapshot, including Authenticode.
            $sourceStream = [FastFilesReleaseArtifactReadFile]::OpenRegularReadOnly([IO.Path]::GetFullPath($artifactPath.sourcePath))
            $snapshotWriter = [IO.File]::Open($snapshotPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            $sourceStream.CopyTo($snapshotWriter)
            $snapshotWriter.Flush($true)
            $snapshotWriter.Dispose()
            $snapshotWriter = $null
            $sourceStream.Dispose()
            $sourceStream = $null

            $snapshotGuard = [FastFilesReleaseArtifactReadFile]::OpenRegularReadOnly($snapshotPath)
            if ($script:ReleaseArtifactSnapshotCapturedForTest -is [scriptblock]) {
                & $script:ReleaseArtifactSnapshotCapturedForTest $artifactPath.sourcePath $snapshotPath
            }

            $architecture = Get-PeArchitecture -Path $snapshotPath
            $signature = Get-SignatureRecord -Path $snapshotPath
            $version = Get-FileVersionRecord -Path $snapshotPath
            if ($expectedBuildConfiguration -and $version.configuration -cne $expectedBuildConfiguration) {
                throw "RELEASE_ARTIFACT_CONFIGURATION_MISMATCH: $($artifactPath.name) embeds config='$($version.configuration)'; expected '$expectedBuildConfiguration' for manifest configuration '$Configuration'."
            }
            $records.Add([pscustomobject]@{
                path = $artifactPath.packagePath.Replace('\', '/')
                filename = $artifactPath.name
                size = $snapshotGuard.Length
                sha256 = (Get-FileHash -LiteralPath $snapshotPath -Algorithm SHA256).Hash.ToUpperInvariant()
                architecture = $architecture.architecture
                peMachine = $architecture.machine
                productVersion = $version.productVersion
                fileVersion = $version.fileVersion
                productName = $version.productName
                companyName = $version.companyName
                fileDescription = $version.fileDescription
                internalName = $version.internalName
                originalFilename = $version.originalFilename
                buildIdentity = $version.buildIdentity
                configuration = $version.configuration
                componentRole = $artifactPath.componentRole
                signature = $signature
            })
        } finally {
            if ($snapshotGuard) { $snapshotGuard.Dispose() }
            if ($snapshotWriter) { $snapshotWriter.Dispose() }
            if ($sourceStream) { $sourceStream.Dispose() }
            if (Test-Path -LiteralPath $snapshotPath -PathType Leaf) {
                Remove-Item -LiteralPath $snapshotPath -Force -ErrorAction SilentlyContinue
            }
        }
    }
    [pscustomobject]@{
        schemaVersion = 2
        kind = 'fastfiles-release-artifact-manifest'
        commit = if ($RepoRoot) { Get-RepositoryCommit -RepoRoot $RepoRoot } else { $null }
        configuration = $Configuration
        productName = 'FastFiles'
        expectedArchitecture = 'x64'
        expectedComponentInventory = @($records | ForEach-Object { $_.filename })
        artifacts = $records
    }
}

function Get-ReleaseArchiveInventory {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $Root)

    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $rootItem = Get-Item -LiteralPath $rootFull -Force -ErrorAction Stop
    if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "ARCHIVE_INVENTORY_ROOT_INVALID: $rootFull"
    }
    $files = @(Get-ChildItem -LiteralPath $rootFull -Recurse -File -Force -ErrorAction Stop)
    $inventory = [System.Collections.Generic.List[object]]::new()
    foreach ($file in $files) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "ARCHIVE_INVENTORY_REPARSE_FILE: $($file.FullName)"
        }
        $relativePath = $file.FullName.Substring($rootFull.Length).TrimStart('\').Replace('\', '/')
        $stream = [FastFilesReleaseArtifactReadFile]::OpenRegularReadOnly($file.FullName)
        try {
            $length = $stream.Length
            $stream.Position = 0
            $sha256 = [Security.Cryptography.SHA256]::Create()
            try { $hash = [Convert]::ToHexString($sha256.ComputeHash($stream)) }
            finally { $sha256.Dispose() }
        } finally {
            $stream.Dispose()
        }
        $inventory.Add([pscustomobject]@{ path = $relativePath; length = $length; sha256 = $hash })
    }
    return @($inventory | Sort-Object { [string]$_.path })
}

function Test-ReleaseCandidateSnapshotContents {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string[]] $ExpectedPaths,
        [Parameter(Mandatory)] [string] $ExpectedCommit,
        [Parameter(Mandatory)] [string] $ExpectedSqliteVersion
    )

    $issues = [System.Collections.Generic.List[string]]::new()
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $rootItem = Get-Item -LiteralPath $rootFull -Force -ErrorAction Stop
    if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "CANDIDATE_SNAPSHOT_ROOT_INVALID: $rootFull"
    }

    $expected = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($path in $ExpectedPaths) {
        if ([string]::IsNullOrWhiteSpace($path) -or $path.Contains('\') -or $path.StartsWith('/') -or
            @($path.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0 -or $path -match '^[A-Za-z]:') {
            [void]$issues.Add('CANDIDATE_SNAPSHOT_EXPECTED_PATH_INVALID')
            continue
        }
        if (-not $expected.Add($path)) { [void]$issues.Add("CANDIDATE_SNAPSHOT_EXPECTED_PATH_DUPLICATE:$path") }
    }

    $observed = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($file in @(Get-ChildItem -LiteralPath $rootFull -Recurse -File -Force -ErrorAction Stop)) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            [void]$issues.Add('CANDIDATE_SNAPSHOT_REPARSE_FILE')
            continue
        }
        $path = $file.FullName.Substring($rootFull.Length).TrimStart('\').Replace('\', '/')
        if (-not $observed.Add($path)) { [void]$issues.Add("CANDIDATE_SNAPSHOT_OBSERVED_PATH_DUPLICATE:$path") }
        elseif (-not $expected.Contains($path)) { [void]$issues.Add("CANDIDATE_SNAPSHOT_UNEXPECTED_PATH:$path") }
    }
    foreach ($path in $expected) {
        if (-not $observed.Contains($path)) { [void]$issues.Add("CANDIDATE_SNAPSHOT_REQUIRED_PATH_MISSING:$path") }
    }

    $dependencyPath = Join-Path $rootFull 'dependency-inventory.json'
    if (Test-Path -LiteralPath $dependencyPath -PathType Leaf) {
        try {
            $dependency = Get-Content -LiteralPath $dependencyPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
            $components = @($dependency.components)
            if ([int]$dependency.schemaVersion -ne 1 -or
                [string]$dependency.kind -cne 'fastfiles-dependency-inventory' -or
                [string]$dependency.format -cne 'internal-structured-inventory' -or
                [string]$dependency.commit -cne $ExpectedCommit -or
                [string]$dependency.note -cne 'This is a lightweight dependency inventory, not a claim of a complete SPDX or CycloneDX SBOM.' -or
                $components.Count -ne 1 -or
                [string]$components[0].name -cne 'SQLite' -or
                [string]$components[0].version -cne $ExpectedSqliteVersion -or
                [string]$components[0].scope -cne 'statically linked build/runtime dependency' -or
                [string]$components[0].source -cne 'third_party/sqlite') {
                [void]$issues.Add('CANDIDATE_SNAPSHOT_DEPENDENCY_INVENTORY_MISMATCH')
            }
        } catch {
            [void]$issues.Add('CANDIDATE_SNAPSHOT_DEPENDENCY_INVENTORY_INVALID')
        }
    } else {
        [void]$issues.Add('CANDIDATE_SNAPSHOT_DEPENDENCY_INVENTORY_MISSING')
    }

    [pscustomobject]@{
        valid = $issues.Count -eq 0
        entryCount = $observed.Count
        expectedPaths = @($expected)
        observedPaths = @($observed)
        issues = @($issues.ToArray())
    }
}

function Test-ReleaseKitManifestProjection {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] $ExpectedManifest
    )

    $issues = [System.Collections.Generic.List[string]]::new()
    try {
        $actualManifest = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        $actualJson = ConvertTo-Json -InputObject $actualManifest -Depth 24 -Compress
        $expectedJson = ConvertTo-Json -InputObject $ExpectedManifest -Depth 24 -Compress
        if ($actualJson -cne $expectedJson) { [void]$issues.Add('CANDIDATE_KIT_MANIFEST_PROJECTION_MISMATCH') }
    } catch {
        [void]$issues.Add('CANDIDATE_KIT_MANIFEST_PROJECTION_INVALID')
    }
    [pscustomobject]@{ valid = $issues.Count -eq 0; issues = @($issues.ToArray()) }
}

function Test-ReleaseJsonProjection {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] $ExpectedValue
    )

    $issues = [System.Collections.Generic.List[string]]::new()
    try {
        $actualValue = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        $actualJson = ConvertTo-Json -InputObject $actualValue -Depth 24 -Compress
        $expectedJson = ConvertTo-Json -InputObject $ExpectedValue -Depth 24 -Compress
        if ($actualJson -cne $expectedJson) { [void]$issues.Add('RELEASE_JSON_PROJECTION_MISMATCH') }
    } catch {
        [void]$issues.Add('RELEASE_JSON_PROJECTION_INVALID')
    }
    [pscustomobject]@{ valid = $issues.Count -eq 0; issues = @($issues.ToArray()) }
}

function Test-ReleaseCandidateOutputContents {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [object[]] $ExpectedEntries,
        [Parameter(Mandatory)] [string[]] $AdditionalPaths
    )

    $issues = [System.Collections.Generic.List[string]]::new()
    $guards = [System.Collections.Generic.List[IO.FileStream]]::new()
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    try {
        $rootItem = Get-Item -LiteralPath $rootFull -Force -ErrorAction Stop
        if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "CANDIDATE_OUTPUT_ROOT_INVALID: $rootFull"
        }
        $expectedByPath = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
        foreach ($entry in $ExpectedEntries) {
            $path = [string]$entry.path
            if ([string]::IsNullOrWhiteSpace($path) -or $path.Contains('\') -or $path.StartsWith('/') -or
                @($path.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0 -or
                $path -match '^[A-Za-z]:') {
                [void]$issues.Add('CANDIDATE_OUTPUT_EXPECTED_PATH_INVALID')
                continue
            }
            if ($expectedByPath.ContainsKey($path)) {
                [void]$issues.Add("CANDIDATE_OUTPUT_EXPECTED_PATH_DUPLICATE:$path")
                continue
            }
            if ([string]$entry.sha256 -notmatch '^[0-9A-Fa-f]{64}$' -or [long]$entry.length -lt 0) {
                [void]$issues.Add("CANDIDATE_OUTPUT_EXPECTED_RECORD_INVALID:$path")
                continue
            }
            $expectedByPath.Add($path, $entry)
        }
        $additional = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($path in $AdditionalPaths) {
            if ([string]::IsNullOrWhiteSpace($path) -or $path.Contains('\') -or $path.StartsWith('/') -or
                @($path.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0 -or
                $path -match '^[A-Za-z]:') {
                [void]$issues.Add('CANDIDATE_OUTPUT_ADDITIONAL_PATH_INVALID')
                continue
            }
            if ($expectedByPath.ContainsKey($path) -or -not $additional.Add($path)) {
                [void]$issues.Add("CANDIDATE_OUTPUT_ADDITIONAL_PATH_DUPLICATE:$path")
            }
        }

        $seenExpected = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        $seenAdditional = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($file in @(Get-ChildItem -LiteralPath $rootFull -Recurse -File -Force -ErrorAction Stop)) {
            if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                [void]$issues.Add('CANDIDATE_OUTPUT_REPARSE_FILE')
                continue
            }
            $path = $file.FullName.Substring($rootFull.Length).TrimStart('\').Replace('\', '/')
            if ($expectedByPath.ContainsKey($path)) {
                if (-not $seenExpected.Add($path)) { [void]$issues.Add("CANDIDATE_OUTPUT_DUPLICATE_PATH:$path"); continue }
                $stream = [FastFilesReleaseArtifactReadFile]::OpenRegularReadOnly($file.FullName)
                $guards.Add($stream)
                $entry = $expectedByPath[$path]
                if ($stream.Length -ne [long]$entry.length) {
                    [void]$issues.Add("CANDIDATE_OUTPUT_LENGTH_MISMATCH:$path")
                    continue
                }
                $stream.Position = 0
                $hash = [Security.Cryptography.SHA256]::Create()
                try { $actualHash = [Convert]::ToHexString($hash.ComputeHash($stream)) }
                finally { $hash.Dispose() }
                if ($actualHash -ine [string]$entry.sha256) { [void]$issues.Add("CANDIDATE_OUTPUT_HASH_MISMATCH:$path") }
            } elseif ($additional.Contains($path)) {
                if (-not $seenAdditional.Add($path)) { [void]$issues.Add("CANDIDATE_OUTPUT_DUPLICATE_PATH:$path"); continue }
                $guards.Add([FastFilesReleaseArtifactReadFile]::OpenRegularReadOnly($file.FullName))
            } else {
                [void]$issues.Add("CANDIDATE_OUTPUT_UNEXPECTED_PATH:$path")
            }
        }
        foreach ($path in $expectedByPath.Keys) {
            if (-not $seenExpected.Contains($path)) { [void]$issues.Add("CANDIDATE_OUTPUT_REQUIRED_PATH_MISSING:$path") }
        }
        foreach ($path in $additional) {
            if (-not $seenAdditional.Contains($path)) { [void]$issues.Add("CANDIDATE_OUTPUT_REQUIRED_PATH_MISSING:$path") }
        }
        return [pscustomobject]@{
            valid = $issues.Count -eq 0
            issues = @($issues.ToArray())
            guards = $guards
        }
    } catch {
        foreach ($guard in $guards) { if ($guard) { $guard.Dispose() } }
        throw
    }
}

function Test-ReleaseCandidateChecksums {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $ChecksumsPath,
        [Parameter(Mandatory)] [string] $MarkerPath
    )

    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $checksumsFull = [IO.Path]::GetFullPath($ChecksumsPath)
    $markerFull = [IO.Path]::GetFullPath($MarkerPath)
    $lines = @(Get-ChildItem -LiteralPath $rootFull -Recurse -File -Force -ErrorAction Stop | Where-Object {
            [IO.Path]::GetFullPath($_.FullName) -ine $checksumsFull -and
            [IO.Path]::GetFullPath($_.FullName) -ine $markerFull
        } | ForEach-Object {
            $relative = $_.FullName.Substring($rootFull.Length).TrimStart('\').Replace('\', '/')
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
            "$hash  $relative"
        } | Sort-Object)
    $expectedText = ($lines -join "`n") + "`n"
    $actualText = [IO.File]::ReadAllText($checksumsFull)
    [pscustomobject]@{ valid = $actualText -ceq $expectedText; issue = if ($actualText -ceq $expectedText) { $null } else { 'CANDIDATE_CHECKSUMS_MISMATCH' } }
}

function Test-ReleaseArchiveContents {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ArchivePath,
        [Parameter(Mandatory)] [object[]] $ExpectedEntries
    )

    $issues = [System.Collections.Generic.List[string]]::new()
    $expectedByPath = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($expected in $ExpectedEntries) {
        $path = [string]$expected.path
        if ([string]::IsNullOrWhiteSpace($path) -or $path.Contains('\') -or $path.StartsWith('/') -or
            @($path.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0 -or
            $path -match '^[A-Za-z]:') {
            [void]$issues.Add('ARCHIVE_EXPECTED_PATH_INVALID')
            continue
        }
        if ($expectedByPath.ContainsKey($path)) {
            [void]$issues.Add('ARCHIVE_EXPECTED_DUPLICATE_PATH')
            continue
        }
        if ([string]$expected.sha256 -notmatch '^[0-9A-Fa-f]{64}$' -or [long]$expected.length -lt 0) {
            [void]$issues.Add('ARCHIVE_EXPECTED_RECORD_INVALID')
            continue
        }
        $expectedByPath.Add($path, $expected)
    }

    $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $stream = $null
    $archive = $null
    try {
        $stream = [FastFilesReleaseArtifactReadFile]::OpenRegularReadOnly([IO.Path]::GetFullPath($ArchivePath))
        $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Read, $true)
        foreach ($entry in $archive.Entries) {
            $path = [string]$entry.FullName
            if ([string]::IsNullOrWhiteSpace($path) -or $path.Contains('\') -or $path.StartsWith('/') -or
                @($path.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0 -or
                $path -match '^[A-Za-z]:') {
                [void]$issues.Add('ARCHIVE_ENTRY_PATH_INVALID')
                continue
            }
            if (-not $seen.Add($path)) {
                [void]$issues.Add("ARCHIVE_ENTRY_DUPLICATE:$path")
                continue
            }
            if (-not $expectedByPath.ContainsKey($path)) {
                [void]$issues.Add("ARCHIVE_ENTRY_UNEXPECTED:$path")
                continue
            }
            $expected = $expectedByPath[$path]
            if ($entry.Length -ne [long]$expected.length) {
                [void]$issues.Add("ARCHIVE_ENTRY_LENGTH_MISMATCH:$path")
                continue
            }
            $entryStream = $null
            $sha256 = $null
            try {
                $entryStream = $entry.Open()
                $sha = [Security.Cryptography.SHA256]::Create()
                try { $sha256 = [Convert]::ToHexString($sha.ComputeHash($entryStream)) }
                finally { $sha.Dispose() }
            } finally {
                if ($entryStream) { $entryStream.Dispose() }
            }
            if ($sha256 -ine [string]$expected.sha256) {
                [void]$issues.Add("ARCHIVE_ENTRY_HASH_MISMATCH:$path")
            }
        }
    } catch {
        [void]$issues.Add('ARCHIVE_READ_ERROR')
    } finally {
        if ($archive) { $archive.Dispose() }
        if ($stream) { $stream.Dispose() }
    }
    foreach ($path in $expectedByPath.Keys) {
        if (-not $seen.Contains($path)) { [void]$issues.Add("ARCHIVE_ENTRY_MISSING:$path") }
    }
    $uniqueIssues = @($issues | Sort-Object -Unique)
    [pscustomobject]@{
        valid = $uniqueIssues.Count -eq 0
        issues = $uniqueIssues
        entryCount = $seen.Count
    }
}

function Write-PackageManifest {
    param([Parameter(Mandatory)] $Manifest, [Parameter(Mandatory)] [string] $Path)

    $artifacts = @($Manifest.artifacts | Sort-Object filename)
    $requiredNames = @('FastFiles.exe', 'FastFilesEngine.exe', 'FastFilesIndexSvc.exe', 'FastFilesSetup.exe')
    if ($artifacts.Count -ne $requiredNames.Count -or
        (@($artifacts.filename | Where-Object { $_ -notin $requiredNames }).Count -gt 0) -or
        (@($requiredNames | Where-Object { $_ -notin @($artifacts.filename) }).Count -gt 0)) {
        throw 'PACKAGE_MANIFEST_INVENTORY_INVALID'
    }
    $versions = @($artifacts | Select-Object -ExpandProperty productVersion -Unique)
    $builds = @($artifacts | Select-Object -ExpandProperty buildIdentity -Unique)
    $products = @($artifacts | Select-Object -ExpandProperty productName -Unique)
    $architectures = @($artifacts | Select-Object -ExpandProperty architecture -Unique)
    if ($versions.Count -ne 1 -or $builds.Count -ne 1 -or $products.Count -ne 1 -or $architectures.Count -ne 1 -or
        $products[0] -cne 'FastFiles' -or $architectures[0] -cne 'x64' -or
        -not (Test-StrictProductVersion ([string]$versions[0])) -or
        [string]$builds[0] -notmatch '^[0-9a-fA-F]{40}$') {
        throw 'PACKAGE_MANIFEST_RELEASE_IDENTITY_INVALID'
    }
    $version = [string]$versions[0]
    $build = ([string]$builds[0]).ToLowerInvariant()
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('FASTFILES_PACKAGE_MANIFEST_V2')
    $lines.Add('schema=2')
    $lines.Add('productName=FastFiles')
    $lines.Add(('productVersion={0}' -f $version))
    $lines.Add(('buildIdentity={0}' -f $build))
    $lines.Add('architecture=x64')
    foreach ($artifact in $artifacts) {
        $fields = @($artifact.filename, $artifact.componentRole, $artifact.originalFilename, $artifact.internalName,
            $artifact.productName, $artifact.productVersion, $artifact.buildIdentity, $artifact.architecture,
            [uint64]$artifact.size, ([string]$artifact.sha256).ToUpperInvariant())
        if ($fields[0..7] | Where-Object { [string]::IsNullOrWhiteSpace([string]$_) -or ([string]$_).Contains('|') -or ([string]$_) -match '[\r\n]' } |
            Select-Object -First 1) {
            throw "PACKAGE_MANIFEST_ARTIFACT_INVALID: $($artifact.filename)"
        }
        if ([string]$artifact.componentRole -notin @('ui', 'engine', 'service', 'installer') -or
            [string]$artifact.productName -cne 'FastFiles' -or [string]$artifact.productVersion -cne $version -or
            [string]$artifact.buildIdentity -ine $build -or [string]$artifact.architecture -cne 'x64' -or
            [string]$artifact.internalName -ine [string]$artifact.originalFilename -or
            ([string]$artifact.sha256 -notmatch '^[0-9A-Fa-f]{64}$')) {
            throw "PACKAGE_MANIFEST_ARTIFACT_INVALID: $($artifact.filename)"
        }
        $lines.Add(('file={0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}|{9}' -f $fields))
    }
    $parent = Split-Path -Parent ([IO.Path]::GetFullPath($Path))
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $encoding = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($Path), (($lines -join "`n") + "`n"), $encoding)
}

function Test-CertificateChainAtTime {
    param(
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $Certificate,
        [Parameter(Mandatory)] [DateTime] $VerificationTime,
        [Parameter(Mandatory)] [string] $ApplicationPolicyOid,
        [AllowNull()] [Security.Cryptography.X509Certificates.X509Certificate2] $CustomTrustRoot,
        [Security.Cryptography.X509Certificates.X509Certificate2Collection] $ExtraStore
    )

    $chain = $null
    try {
        $chain = [Security.Cryptography.X509Certificates.X509Chain]::new()
        $chain.ChainPolicy.RevocationMode = [Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
        $chain.ChainPolicy.RevocationFlag = [Security.Cryptography.X509Certificates.X509RevocationFlag]::EntireChain
        $chain.ChainPolicy.VerificationFlags = [Security.Cryptography.X509Certificates.X509VerificationFlags]::NoFlag
        $chain.ChainPolicy.VerificationTimeIgnored = $false
        $chain.ChainPolicy.VerificationTime = $VerificationTime.ToUniversalTime()
        $chain.ChainPolicy.DisableCertificateDownloads = $true
        [void]$chain.ChainPolicy.ApplicationPolicy.Add([Security.Cryptography.Oid]::new($ApplicationPolicyOid))
        foreach ($extra in @($ExtraStore)) {
            if ($null -ne $extra) { [void]$chain.ChainPolicy.ExtraStore.Add($extra) }
        }
        if ($null -ne $CustomTrustRoot) {
            $chain.ChainPolicy.TrustMode = [Security.Cryptography.X509Certificates.X509ChainTrustMode]::CustomRootTrust
            [void]$chain.ChainPolicy.CustomTrustStore.Add($CustomTrustRoot)
        }
        $built = [bool]$chain.Build($Certificate)
        $statuses = @($chain.ChainStatus | Where-Object {
                $_.Status -ne [Security.Cryptography.X509Certificates.X509ChainStatusFlags]::NoError
            } | ForEach-Object { $_.Status.ToString() })
        [pscustomobject]@{
            valid = $built -and $chain.ChainElements.Count -ge 2 -and $statuses.Count -eq 0
            built = $built
            elementCount = $chain.ChainElements.Count
            statuses = $statuses
            verificationTime = $VerificationTime.ToUniversalTime()
            applicationPolicy = $ApplicationPolicyOid
            trustMode = if ($null -ne $CustomTrustRoot) { 'CUSTOM_ROOT_TEST_ONLY' } else { 'WINDOWS_DEFAULT_TRUST_POLICY' }
            revocationPolicy = 'NOT_CHECKED_OFFLINE'
        }
    } catch {
        [pscustomobject]@{
            valid = $false
            built = $false
            elementCount = 0
            statuses = @('CHAIN_BUILD_FAILED')
            verificationTime = $VerificationTime.ToUniversalTime()
            applicationPolicy = $ApplicationPolicyOid
            trustMode = if ($null -ne $CustomTrustRoot) { 'CUSTOM_ROOT_TEST_ONLY' } else { 'WINDOWS_DEFAULT_TRUST_POLICY' }
            revocationPolicy = 'NOT_CHECKED_OFFLINE'
            error = $_.Exception.Message
        }
    } finally {
        if ($chain) { $chain.Dispose() }
    }
}

function Test-PackageManifestSignatureCore {
    param(
        [Parameter(Mandatory)] [string] $ManifestPath,
        [Parameter(Mandatory)] [string] $SignaturePath,
        [string[]] $AllowedSignerThumbprints = @(),
        [AllowNull()] [Security.Cryptography.X509Certificates.X509Certificate2] $CustomTrustRoot
    )

    try {
        $manifestFullPath = [IO.Path]::GetFullPath($ManifestPath)
        $signatureFullPath = [IO.Path]::GetFullPath($SignaturePath)
        if (-not (Test-Path -LiteralPath $manifestFullPath -PathType Leaf)) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_MISSING'; signerThumbprint = $null }
        }
        if (-not (Test-Path -LiteralPath $signatureFullPath -PathType Leaf)) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_SIGNATURE_MISSING'; signerThumbprint = $null }
        }
        try { Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop } catch { }
        $content = [IO.File]::ReadAllBytes($manifestFullPath)
        $signature = [IO.File]::ReadAllBytes($signatureFullPath)
        if ($content.Length -eq 0 -or $signature.Length -eq 0) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_SIGNATURE_EMPTY'; signerThumbprint = $null }
        }
        $contentInfo = [Security.Cryptography.Pkcs.ContentInfo]::new($content)
        $cms = [Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
        $cms.Decode($signature)
        if ($cms.SignerInfos.Count -ne 1) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_SIGNATURE_SIGNER_COUNT_INVALID'; signerThumbprint = $null }
        }
        $signerInfo = $cms.SignerInfos[0]
        $certificate = $signerInfo.Certificate
        if (-not $certificate) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_SIGNATURE_CERTIFICATE_MISSING'; signerThumbprint = $null }
        }
        $cms.CheckSignature($true)

        $timestampAttributes = @($signerInfo.UnsignedAttributes | Where-Object {
                $_.Oid.Value -eq '1.3.6.1.4.1.311.3.3.1'
            })
        if ($timestampAttributes.Count -ne 1 -or $timestampAttributes[0].Values.Count -ne 1) {
            return [pscustomobject]@{
                valid = $false
                reason = 'PACKAGE_MANIFEST_TIMESTAMP_REQUIRED'
                signerThumbprint = Normalize-Thumbprint -Value $certificate.Thumbprint
                timestampSigned = $false
                timestampValid = $false
            }
        }

        $timestampBytes = [byte[]]$timestampAttributes[0].Values[0].RawData
        $timestampToken = $null
        $timestampConsumed = 0
        $timestampDecoded = [Security.Cryptography.Pkcs.Rfc3161TimestampToken]::TryDecode(
            [ReadOnlyMemory[byte]]::new($timestampBytes), [ref]$timestampToken, [ref]$timestampConsumed)
        [Security.Cryptography.X509Certificates.X509Certificate2]$timestampSigner = $null
        $timestampAuthentic = $false
        if ($timestampDecoded -and $timestampConsumed -eq $timestampBytes.Length -and $null -ne $timestampToken) {
            $timestampAuthentic = [bool]$timestampToken.VerifySignatureForSignerInfo(
                $signerInfo, [ref]$timestampSigner, $cms.Certificates)
        }
        if (-not $timestampAuthentic -or $null -eq $timestampSigner) {
            return [pscustomobject]@{
                valid = $false
                reason = 'PACKAGE_MANIFEST_TIMESTAMP_INVALID'
                signerThumbprint = Normalize-Thumbprint -Value $certificate.Thumbprint
                timestampSigned = $true
                timestampValid = $false
            }
        }

        $timestampUtc = $timestampToken.TokenInfo.Timestamp.UtcDateTime
        $chainExtraStore = [Security.Cryptography.X509Certificates.X509Certificate2Collection]::new()
        foreach ($embedded in @($cms.Certificates)) { [void]$chainExtraStore.Add($embedded) }
        try {
            $timestampCms = [Security.Cryptography.Pkcs.SignedCms]::new()
            $timestampCms.Decode($timestampBytes)
            foreach ($embedded in @($timestampCms.Certificates)) { [void]$chainExtraStore.Add($embedded) }
        } catch {
            return [pscustomobject]@{
                valid = $false
                reason = 'PACKAGE_MANIFEST_TIMESTAMP_INVALID'
                signerThumbprint = Normalize-Thumbprint -Value $certificate.Thumbprint
                timestampSigned = $true
                timestampValid = $false
                timestampAuthentic = $false
            }
        }
        [void]$chainExtraStore.Add($timestampSigner)
        $signerChain = Test-CertificateChainAtTime -Certificate $certificate -VerificationTime $timestampUtc `
            -ApplicationPolicyOid $script:CodeSigningApplicationPolicyOid -CustomTrustRoot $CustomTrustRoot `
            -ExtraStore $chainExtraStore
        if (-not $signerChain.valid) {
            return [pscustomobject]@{
                valid = $false
                reason = 'PACKAGE_MANIFEST_SIGNATURE_CERTIFICATE_CHAIN_INVALID'
                signerThumbprint = Normalize-Thumbprint -Value $certificate.Thumbprint
                timestampSigned = $true
                timestampValid = $true
                timestampAuthentic = $true
                timestampUtc = $timestampUtc
                signerChain = $signerChain
            }
        }
        $timestampChain = Test-CertificateChainAtTime -Certificate $timestampSigner -VerificationTime $timestampUtc `
            -ApplicationPolicyOid '1.3.6.1.5.5.7.3.8' -CustomTrustRoot $CustomTrustRoot `
            -ExtraStore $chainExtraStore
        if (-not $timestampChain.valid) {
            return [pscustomobject]@{
                valid = $false
                reason = 'PACKAGE_MANIFEST_TIMESTAMP_CHAIN_INVALID'
                signerThumbprint = Normalize-Thumbprint -Value $certificate.Thumbprint
                timestampSigned = $true
                timestampValid = $false
                timestampAuthentic = $true
                timestampUtc = $timestampUtc
                timestampSignerThumbprint = Normalize-Thumbprint -Value $timestampSigner.Thumbprint
                timestampChain = $timestampChain
            }
        }

        $thumbprint = Normalize-Thumbprint -Value $certificate.Thumbprint
        $allowed = @($AllowedSignerThumbprints | ForEach-Object { Normalize-Thumbprint -Value $_ } |
            Where-Object { Test-UsableThumbprint $_ })
        if ($allowed.Count -eq 0 -or $thumbprint -notin $allowed) {
            return [pscustomobject]@{
                valid = $false
                reason = 'PACKAGE_MANIFEST_SIGNATURE_WRONG_SIGNER'
                signerThumbprint = $thumbprint
                timestampSigned = $true
                timestampValid = $true
                timestampAuthentic = $true
                timestampUtc = $timestampUtc
                timestampSignerThumbprint = Normalize-Thumbprint -Value $timestampSigner.Thumbprint
            }
        }
        [pscustomobject]@{
            valid = $true
            reason = $null
            signerThumbprint = $thumbprint
            timestampSigned = $true
            timestampValid = $true
            timestampAuthentic = $true
            timestampUtc = $timestampUtc
            timestampSignerThumbprint = Normalize-Thumbprint -Value $timestampSigner.Thumbprint
            signerChain = $signerChain
            timestampChain = $timestampChain
            revocationPolicy = 'NOT_CHECKED_OFFLINE'
            verificationPolicy = 'DETACHED_CMS_RFC3161_TIMESTAMP_AWARE'
        }
    } catch {
        [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_SIGNATURE_INVALID'; signerThumbprint = $null; error = $_.Exception.Message }
    }
}

function Test-PackageManifestSignature {
    param(
        [Parameter(Mandatory)] [string] $ManifestPath,
        [Parameter(Mandatory)] [string] $SignaturePath,
        [string[]] $AllowedSignerThumbprints = @()
    )

    return Test-PackageManifestSignatureCore -ManifestPath $ManifestPath -SignaturePath $SignaturePath `
        -AllowedSignerThumbprints $AllowedSignerThumbprints
}

# Test-only technical seam. Production package validation intentionally calls
# Test-PackageManifestSignature without a custom root, preserving Windows trust
# policy for signed release packages.
function Test-PackageManifestSignatureForTest {
    param(
        [Parameter(Mandatory)] [string] $ManifestPath,
        [Parameter(Mandatory)] [string] $SignaturePath,
        [string[]] $AllowedSignerThumbprints = @(),
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $CustomTrustRoot
    )

    return Test-PackageManifestSignatureCore -ManifestPath $ManifestPath -SignaturePath $SignaturePath `
        -AllowedSignerThumbprints $AllowedSignerThumbprints -CustomTrustRoot $CustomTrustRoot
}

function Test-PackageManifest {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] $ArtifactManifest,
        [string] $PackageRoot,
        [string] $ManifestSignaturePath,
        [string[]] $AllowedSignerThumbprints = @(),
        [switch] $AllowUnsignedPreparation
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    try {
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_MISSING'; entries = @() }
        }
        $lines = [IO.File]::ReadAllLines($fullPath, [Text.UTF8Encoding]::new($false))
        if ($lines.Count -lt 7 -or $lines[0] -cne 'FASTFILES_PACKAGE_MANIFEST_V2' -or $lines[1] -cne 'schema=2') {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_HEADER_INVALID'; entries = @() }
        }
        if ($lines[2] -cne 'productName=FastFiles' -or $lines[3] -notmatch '^productVersion=(.+)$') {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_VERSION_INVALID'; entries = @() }
        }
        $productVersion = [string]($lines[3].Substring(15))
        if ($lines[4] -notmatch '^buildIdentity=([0-9A-Fa-f]{40})$' -or $lines[5] -cne 'architecture=x64' -or
            -not (Test-StrictProductVersion $productVersion)) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_VERSION_INVALID'; entries = @() }
        }
        $buildIdentity = [string]($lines[4].Substring(14)).ToLowerInvariant()
        $expected = @($ArtifactManifest.artifacts | Sort-Object filename)
        $expectedVersions = @($expected | Select-Object -ExpandProperty productVersion -Unique)
        $expectedBuilds = @($expected | Select-Object -ExpandProperty buildIdentity -Unique)
        if ($expected.Count -ne 4 -or $expectedVersions.Count -ne 1 -or $expectedBuilds.Count -ne 1 -or
            $productVersion -cne [string]$expectedVersions[0] -or $buildIdentity -cne ([string]$expectedBuilds[0]).ToLowerInvariant()) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_VERSION_MISMATCH'; entries = @() }
        }

        $entries = [System.Collections.Generic.List[object]]::new()
        $seen = @{}
        for ($index = 6; $index -lt $lines.Count; $index++) {
            if ($lines[$index] -notmatch '^file=([^|]+)\|([^|]+)\|([^|]+)\|([^|]+)\|([^|]+)\|([^|]+)\|([^|]+)\|([^|]+)\|([0-9]+)\|([0-9A-Fa-f]{64})$') {
                return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_FILE_ROW_INVALID'; entries = @($entries) }
            }
            $name = $Matches[1]
            if ($seen.ContainsKey($name)) {
                return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_DUPLICATE_FILE'; entries = @($entries) }
            }
            $seen[$name] = $true
            $entries.Add([pscustomobject]@{
                    filename = $name
                    componentRole = $Matches[2]
                    originalFilename = $Matches[3]
                    internalName = $Matches[4]
                    productName = $Matches[5]
                    productVersion = $Matches[6]
                    buildIdentity = $Matches[7].ToLowerInvariant()
                    architecture = $Matches[8]
                    size = [uint64]$Matches[9]
                    sha256 = $Matches[10].ToUpperInvariant()
                })
        }

        if ($entries.Count -ne $expected.Count -or $entries.Count -eq 0) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_INVENTORY_MISMATCH'; entries = @($entries) }
        }
        for ($index = 0; $index -lt $expected.Count; $index++) {
            if ($entries[$index].filename -cne [string]$expected[$index].filename -or
                $entries[$index].componentRole -cne [string]$expected[$index].componentRole -or
                $entries[$index].originalFilename -ine [string]$expected[$index].originalFilename -or
                $entries[$index].internalName -ine [string]$expected[$index].internalName -or
                $entries[$index].productName -cne 'FastFiles' -or
                $entries[$index].productVersion -cne $productVersion -or
                $entries[$index].buildIdentity -cne $buildIdentity -or
                $entries[$index].architecture -cne 'x64' -or
                $entries[$index].size -ne [uint64]$expected[$index].size -or
                $entries[$index].sha256 -cne ([string]$expected[$index].sha256).ToUpperInvariant()) {
                return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_HASH_MISMATCH'; entries = @($entries) }
            }
            if ($PackageRoot) {
                $artifactPath = Join-Path ([IO.Path]::GetFullPath($PackageRoot)) $entries[$index].filename
                if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
                    return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_ARTIFACT_MISSING'; entries = @($entries) }
                }
                $artifact = Get-Item -LiteralPath $artifactPath
                $actualHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToUpperInvariant()
                if ([uint64]$artifact.Length -ne $entries[$index].size -or $actualHash -cne $entries[$index].sha256) {
                    return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_ARTIFACT_HASH_MISMATCH'; entries = @($entries) }
                }
            }
        }
        $signaturePath = if ($ManifestSignaturePath) { [IO.Path]::GetFullPath($ManifestSignaturePath) } else {
            Join-Path (Split-Path -Parent $fullPath) 'FastFiles.package.manifest.p7s'
        }
        $signatureCheck = if ($AllowUnsignedPreparation -and -not (Test-Path -LiteralPath $signaturePath -PathType Leaf)) {
            [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_SIGNATURE_NOT_PRESENT_FOR_PREPARATION'; signerThumbprint = $null }
        } else {
            $pins = if ($AllowedSignerThumbprints.Count -gt 0) { $AllowedSignerThumbprints } else {
                @($ArtifactManifest.artifacts | ForEach-Object { $_.signature.leafThumbprint } | Where-Object { Test-UsableThumbprint $_ } | Select-Object -Unique)
            }
            Test-PackageManifestSignature -ManifestPath $fullPath -SignaturePath $signaturePath -AllowedSignerThumbprints $pins
        }
        if (-not $AllowUnsignedPreparation -and -not $signatureCheck.valid) {
            return [pscustomobject]@{ valid = $false; productionReady = $false; reason = $signatureCheck.reason; entries = @($entries); signature = $signatureCheck }
        }
        if ($PackageRoot) {
            $root = [IO.Path]::GetFullPath($PackageRoot)
            if ([IO.Path]::GetFullPath((Split-Path -Parent $fullPath)) -ine $root) {
                return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_ROOT_MISMATCH'; entries = @($entries); signature = $signatureCheck }
            }
            $rootItem = Get-Item -LiteralPath $root -ErrorAction Stop
            if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_REPARSE_ROOT'; entries = @($entries); signature = $signatureCheck }
            }
            foreach ($entry in $entries) {
                $artifactPath = Join-Path $root $entry.filename
                if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
                    return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_ARTIFACT_MISSING'; entries = @($entries); signature = $signatureCheck }
                }
                $artifact = Get-Item -LiteralPath $artifactPath
                if (($artifact.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                    return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_REPARSE_MEMBER'; entries = @($entries); signature = $signatureCheck }
                }
                $actualHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToUpperInvariant()
                if ([uint64]$artifact.Length -ne $entry.size -or $actualHash -cne $entry.sha256) {
                    return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_ARTIFACT_HASH_MISMATCH'; entries = @($entries); signature = $signatureCheck }
                }
            }
            $allowedNames = @($expected.filename) + @('FastFiles.package.manifest')
            if (Test-Path -LiteralPath $signaturePath -PathType Leaf) {
                $allowedNames += 'FastFiles.package.manifest.p7s'
            }
            $rootChildren = @(Get-ChildItem -LiteralPath $root -Force)
            $unexpectedMembers = @($rootChildren | Where-Object {
                    $_.PSIsContainer -or ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or ($_.Name -notin $allowedNames)
                })
            $payloadMembers = @($rootChildren | Where-Object { $_.Name -in @($expected.filename) })
            if ($unexpectedMembers.Count -gt 0 -or $payloadMembers.Count -ne $expected.Count) {
                return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_UNEXPECTED_MEMBER'; entries = @($entries); signature = $signatureCheck }
            }
        }
        [pscustomobject]@{ valid = $true; productionReady = [bool]$signatureCheck.valid; reason = $null; productVersion = $productVersion; buildIdentity = $buildIdentity; entries = @($entries); signature = $signatureCheck }
    } catch {
        [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_PARSE_ERROR'; entries = @(); error = $_.Exception.Message }
    }
}

function Get-SafePinState {
    param([Parameter(Mandatory)] $PinState)

    $source = [string]$PinState.source
    $safeSource = if ([string]::IsNullOrWhiteSpace($source)) {
        $null
    } elseif ([IO.Path]::IsPathRooted($source) -or $source.Contains('\') -or $source.Contains('/')) {
        'path-redacted'
    } else {
        $source
    }
    [pscustomobject]@{
        name = $PinState.name
        state = $PinState.state
        source = $safeSource
        length = $PinState.length
    }
}

function Get-ReleaseReadinessStatus {
    param(
        [Parameter(Mandatory)] [string] $ArtifactStatus,
        [Parameter(Mandatory)] [string] $SigningStatus,
        [Parameter(Mandatory)] [string] $PinsStatus,
        [Parameter(Mandatory)] [bool] $CleanHost,
        [Parameter(Mandatory)] [bool] $ArchitectureCompatible,
        [Parameter(Mandatory)] [bool] $Elevated
    )

    if ($ArtifactStatus -eq 'FAIL') { return 'FAIL' }
    if ($ArtifactStatus -eq 'PASS' -and $SigningStatus -eq 'PASS' -and
        $PinsStatus -eq 'PASS' -and $CleanHost -and
        $ArchitectureCompatible -and $Elevated) {
        return 'PASS'
    }
    return 'BLOCKED_EXTERNAL'
}

function Get-ReleaseSignatureMatrix {
    param(
        [Parameter(Mandatory)] $Manifest,
        [Parameter(Mandatory)] $PinStates,
        [AllowNull()] [string] $ExpectedSignerThumbprint
    )

    $expected = Normalize-Thumbprint -Value $ExpectedSignerThumbprint
    $expectedUsable = Test-UsableThumbprint -Value $expected
    $rows = @($Manifest.artifacts | ForEach-Object {
        $pinState = if ($_.filename -eq 'FastFilesEngine.exe') { $PinStates.engine } elseif ($_.filename -eq 'FastFilesIndexSvc.exe') { $PinStates.service } else { $null }
        $expectedPinned = if ($expected) { [bool]($_.signature.leafThumbprint -and $_.signature.leafThumbprint -eq $expected) } else { $null }
        $reason = $null
        $result = 'PASS'
        if (-not $_.signature.signed) {
            $result = 'UNSIGNED — PRODUCTION SIGNING REQUIRED'
        } elseif (-not $_.signature.valid) {
            $result = 'SIGNATURE_INVALID'
        } elseif (-not $_.signature.timestampSigned) {
            $result = 'MISSING_TIMESTAMP'
        } elseif (-not $_.signature.timestampValid) {
            $result = 'TIMESTAMP_INVALID'
        } elseif ($ExpectedSignerThumbprint -and -not $expectedUsable) {
            $result = 'MALFORMED_EXPECTED_SIGNER'
        } elseif (-not $expected) {
            $result = 'BLOCKED_EXTERNAL — EXPECTED_SIGNER_NOT_CONFIGURED'
        } elseif (-not $expectedPinned) {
            $result = 'WRONG_SIGNER'
        } elseif ($pinState -and $pinState.state -eq 'PLACEHOLDER') {
            $result = 'PLACEHOLDER_PIN'
        } elseif ($pinState -and $pinState.state -eq 'MALFORMED') {
            $result = 'MALFORMED_PIN'
        } elseif ($pinState -and $pinState.state -eq 'MISSING') {
            $result = 'MISSING_PIN'
        } elseif ($pinState -and $pinState.normalized -ne $_.signature.leafThumbprint) {
            $result = 'PIN_MISMATCH'
        }
        [pscustomobject]@{
            artifact = $_.filename
            signed = $_.signature.signed
            signatureValid = $_.signature.valid
            signerSubject = $_.signature.signerSubject
            leafThumbprint = $_.signature.leafThumbprint
            expectedPinned = $expectedPinned
            timestampSigned = $_.signature.timestampSigned
            timestampValid = $_.signature.timestampValid
            timestampStatus = if ($_.signature.timestampSigned -and $_.signature.timestampValid) { 'VALID' } elseif ($_.signature.timestampSigned) { 'INVALID' } else { 'MISSING' }
            result = $result
            pinState = if ($pinState) { Get-SafePinState -PinState $pinState } else { $null }
        }
    })
    return $rows
}

function Get-FastFilesProcessRecords {
    $names = @('FastFiles.exe', 'FastFilesEngine.exe', 'FastFilesIndexSvc.exe', 'FastFilesSetup.exe')
    if (-not $IsWindows) { return @() }
    $records = @()
    foreach ($process in @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object { $names -contains $_.Name })) {
        $records += [pscustomobject]@{
            name = $process.Name
            processId = [int]$process.ProcessId
            parentProcessId = [int]$process.ParentProcessId
            path = $process.ExecutablePath
            pathKnown = $null -ne $process.ExecutablePath
        }
    }
    return $records
}

function Get-CleanHostPreflight {
    $service = if ($IsWindows) { Get-CimInstance Win32_Service -Filter "Name='FastFilesIndexSvc'" -ErrorAction SilentlyContinue } else { $null }
    $task = if ($IsWindows) { Get-ScheduledTask -TaskPath '\FastFiles\' -TaskName 'FastFilesEngine' -ErrorAction SilentlyContinue } else { $null }
    $group = if ($IsWindows) { Get-LocalGroup -Name 'FastFilesUsers' -ErrorAction SilentlyContinue } else { $null }
    $programFiles = if ($env:ProgramFiles) { Join-Path $env:ProgramFiles 'FastFiles' } else { $null }
    $programData = if ($env:ProgramData) { Join-Path $env:ProgramData 'FastFiles' } else { $null }
    $localData = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'FastFiles' } else { $null }
    $registry = if ($IsWindows) { Test-Path -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FastFiles' } else { $false }
    $processes = @(Get-FastFilesProcessRecords)
    $resources = [ordered]@{
        service = [pscustomobject]@{ present = $null -ne $service; state = if ($service) { $service.State } else { $null }; startName = if ($service) { $service.StartName } else { $null }; path = if ($service) { $service.PathName } else { $null } }
        processes = $processes
        scheduledTask = [pscustomobject]@{ present = $null -ne $task; path = '\FastFiles\FastFilesEngine'; action = if ($task) { @($task.Actions.Execute) } else { @() } }
        clientGroup = [pscustomobject]@{ present = $null -ne $group; name = 'FastFilesUsers' }
        installDirectory = [pscustomobject]@{ present = if ($programFiles) { Test-Path -LiteralPath $programFiles } else { $false }; path = $programFiles }
        programData = [pscustomobject]@{ present = if ($programData) { Test-Path -LiteralPath $programData } else { $false }; path = $programData }
        userData = [pscustomobject]@{ present = if ($localData) { Test-Path -LiteralPath $localData } else { $false }; path = $localData }
        uninstallRegistry = [pscustomobject]@{ present = $registry; path = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FastFiles' }
    }
    $contamination = @()
    foreach ($entry in $resources.GetEnumerator()) {
        if ($entry.Key -eq 'processes') {
            if (@($entry.Value).Count -gt 0) { $contamination += 'processes' }
        } elseif ($entry.Value.present) {
            $contamination += $entry.Key
        }
    }
    $os = if ($IsWindows) { Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue } else { $null }
    [pscustomobject]@{
        schemaVersion = 1
        checkedAtUtc = [DateTime]::UtcNow.ToString('o')
        os = [pscustomobject]@{ caption = if ($os) { $os.Caption } else { $null }; version = if ($os) { $os.Version } else { $null }; build = if ($os) { $os.BuildNumber } else { $null }; architecture = $env:PROCESSOR_ARCHITECTURE }
        elevated = if ($IsWindows) { ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) } else { $false }
        resources = $resources
        contamination = @($contamination | Sort-Object -Unique)
        clean = @($contamination).Count -eq 0
        expectedArchitecture = 'x64'
        architectureCompatible = $env:PROCESSOR_ARCHITECTURE -in @('AMD64', 'ARM64')
        trustStore = [pscustomobject]@{ checked = $false; policy = 'The kit does not install or modify trust-store entries; production certificate provenance is validated from artifact signatures.' }
    }
}

function Write-ReleaseJson {
    param([Parameter(Mandatory)] $Value, [Parameter(Mandatory)] [string] $Path)

    $parent = Split-Path -Parent ([IO.Path]::GetFullPath($Path))
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $Value | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding utf8NoBOM
}

Export-ModuleMember -Function @(
    'Normalize-Thumbprint',
    'Test-ThumbprintFormat',
    'Test-UsableThumbprint',
    'Get-ProductionCertificatePolicy',
    'Get-SignedArtifactVerificationPolicy',
    'Test-ProductionCertificate',
    'Get-PinState',
    'Get-ConfiguredPinStates',
    'Get-ReleaseArtifactPaths',
    'Resolve-ReleaseKitPackageRoot',
    'Get-PeArchitecture',
    'Get-SignatureRecord',
    'Get-FileVersionRecord',
    'Test-StrictProductVersion',
    'Compare-StrictProductVersion',
    'Get-RepositoryProductVersion',
    'Test-ExecutableVersionMetadata',
    'Test-ReleaseVersionMetadata',
    'Get-ReleaseBuildConfigurationLabel',
    'Get-ReleaseArtifactManifest',
    'Get-ReleaseArchiveInventory',
    'Test-ReleaseCandidateSnapshotContents',
    'Test-ReleaseKitManifestProjection',
    'Test-ReleaseJsonProjection',
    'Test-ReleaseCandidateOutputContents',
    'Test-ReleaseCandidateChecksums',
    'Test-ReleaseArchiveContents',
    'Write-PackageManifest',
    'Test-PackageManifestSignature',
    'Test-PackageManifest',
    'Get-SafePinState',
    'Get-ReleaseReadinessStatus',
    'Get-ReleaseSignatureMatrix',
    'Get-CleanHostPreflight',
    'Write-ReleaseJson'
)
