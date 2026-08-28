Set-StrictMode -Version Latest

$script:ReleaseArtifactDefinitions = @(
    [pscustomobject]@{ Name = 'FastFiles.exe'; PackagePath = 'FastFiles.exe'; BuildPath = 'src\ui\FastFiles.exe'; Role = 'ui'; Pin = $null },
    [pscustomobject]@{ Name = 'FastFilesEngine.exe'; PackagePath = 'FastFilesEngine.exe'; BuildPath = 'src\engine\FastFilesEngine.exe'; Role = 'engine'; Pin = 'engine' },
    [pscustomobject]@{ Name = 'FastFilesIndexSvc.exe'; PackagePath = 'FastFilesIndexSvc.exe'; BuildPath = 'src\indexsvc\FastFilesIndexSvc.exe'; Role = 'service'; Pin = 'service' },
    [pscustomobject]@{ Name = 'FastFilesSetup.exe'; PackagePath = 'FastFilesSetup.exe'; BuildPath = 'src\installer\FastFilesSetup.exe'; Role = 'installer'; Pin = $null }
)

$script:CodeSigningApplicationPolicyOid = '1.3.6.1.5.5.7.3.3'

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
    [pscustomobject]@{
        productVersion = if ([string]::IsNullOrWhiteSpace($info.ProductVersion)) { $null } else { $info.ProductVersion }
        fileVersion = if ([string]::IsNullOrWhiteSpace($info.FileVersion)) { $null } else { $info.FileVersion }
        productName = if ([string]::IsNullOrWhiteSpace($info.ProductName)) { $null } else { $info.ProductName }
        companyName = if ([string]::IsNullOrWhiteSpace($info.CompanyName)) { $null } else { $info.CompanyName }
        fileDescription = if ([string]::IsNullOrWhiteSpace($info.FileDescription)) { $null } else { $info.FileDescription }
        internalName = if ([string]::IsNullOrWhiteSpace($info.InternalName)) { $null } else { $info.InternalName }
        originalFilename = if ([string]::IsNullOrWhiteSpace($info.OriginalFilename)) { $null } else { $info.OriginalFilename }
        buildIdentity = if ([string]::IsNullOrWhiteSpace($info.PrivateBuild)) { $null } else { $info.PrivateBuild }
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
    if ($expectedBuildIdentity -and $expectedBuildIdentity -notmatch '^[0-9a-fA-F]{40}$') { throw 'BUILD_IDENTITY_INVALID' }
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

function Get-ReleaseArtifactManifest {
    param(
        [Parameter(Mandatory)] [string] $ArtifactRoot,
        [string] $RepoRoot,
        [string] $Configuration = 'Release'
    )

    $paths = Get-ReleaseArtifactPaths -ArtifactRoot $ArtifactRoot
    $records = @($paths | Sort-Object order | ForEach-Object {
        $architecture = Get-PeArchitecture -Path $_.sourcePath
        $signature = Get-SignatureRecord -Path $_.sourcePath
        $version = Get-FileVersionRecord -Path $_.sourcePath
        [pscustomobject]@{
            path = $_.packagePath.Replace('\', '/')
            filename = $_.name
            size = (Get-Item -LiteralPath $_.sourcePath).Length
            sha256 = (Get-FileHash -LiteralPath $_.sourcePath -Algorithm SHA256).Hash.ToUpperInvariant()
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
            componentRole = $_.componentRole
            signature = $signature
        }
    })
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
    'Get-ReleaseArtifactManifest',
    'Write-PackageManifest',
    'Test-PackageManifestSignature',
    'Test-PackageManifest',
    'Get-SafePinState',
    'Get-ReleaseReadinessStatus',
    'Get-ReleaseSignatureMatrix',
    'Get-CleanHostPreflight',
    'Write-ReleaseJson'
)
