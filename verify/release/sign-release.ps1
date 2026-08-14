[CmdletBinding(DefaultParameterSetName = 'Root')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Files')]
    [ValidateNotNullOrEmpty()] [string[]] $Artifacts,
    [Parameter(Mandatory, ParameterSetName = 'Root')]
    [string] $ArtifactRoot,
    [Parameter(Mandatory, ParameterSetName = 'ManifestOnly')]
    [switch] $ManifestOnly,
    [ValidateSet('CertificateStore', 'Pfx')]
    [string] $Provider,
    [string] $CertificateThumbprint,
    [ValidateSet('CurrentUser', 'LocalMachine')]
    [string] $StoreLocation = 'CurrentUser',
    [string] $PfxPath,
    [SecureString] $PfxPassword,
    [string] $PfxPasswordEnvironmentVariable = 'FASTFILES_SIGNING_PFX_PASSWORD',
    [string] $SigntoolPath = 'signtool.exe',
    [string] $TimestampUrl,
    [string] $ExpectedSignerThumbprint,
    [string] $PackageManifestPath,
    [string] $ManifestSignatureOutputPath,
    [switch] $DryRun,
    [ValidateSet('human', 'json')]
    [string] $OutputFormat = 'human'
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking

$providerThumbprint = $null
$pfxSecret = $null
$certificateForManifest = $null
$signingStarted = $false
$records = @()

function ConvertTo-PlainSecret {
    param([Parameter(Mandatory)] [SecureString] $SecureValue)
    $handle = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureValue)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($handle) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($handle) }
}

function Resolve-Signtool {
    param([Parameter(Mandatory)] [string] $RequestedPath)
    if (Test-Path -LiteralPath $RequestedPath -PathType Leaf) { return [IO.Path]::GetFullPath($RequestedPath) }
    $command = Get-Command $RequestedPath -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "SIGNTOOL_REQUIRED: signtool.exe was not found. Supply -SigntoolPath with the VS/Windows SDK tool."
}

function Assert-AuthorizedCertificate {
    param([Parameter(Mandatory)] $Certificate, [Parameter(Mandatory)] [string] $Source)
    if (-not $Certificate) { throw "AUTHORIZED_SIGNER_REQUIRED: certificate could not be loaded from $Source." }
    $policy = Get-ProductionCertificatePolicy -Certificate $Certificate -RequirePrivateKey
    if (-not $policy.valid) {
        $reasons = @($policy.reasons) -join ', '
        throw "DEVELOPMENT_CERTIFICATE_NOT_ALLOWED: certificate from $Source failed technical production code-signing policy ($reasons). Operator authorization/provenance remains a separate release input."
    }
}

function Assert-ExpectedSignerThumbprint {
    param([Parameter(Mandatory)] [string] $ProviderThumbprint)

    $expected = if ($ExpectedSignerThumbprint) { Normalize-Thumbprint -Value $ExpectedSignerThumbprint } else {
        $ProviderThumbprint
    }
    if (-not (Test-UsableThumbprint -Value $expected)) {
        throw 'MALFORMED_EXPECTED_SIGNER: expected signer thumbprint must be a non-placeholder 40-hex value.'
    }
    if ($ExpectedSignerThumbprint -and $expected -cne $ProviderThumbprint) {
        throw "EXPECTED_SIGNER_MISMATCH: selected certificate thumbprint $ProviderThumbprint does not match the expected signer $expected."
    }
    return $expected
}

function Assert-ProductionTimestampConfiguration {
    if ([string]::IsNullOrWhiteSpace($TimestampUrl)) {
        throw 'PRODUCTION_TIMESTAMP_REQUIRED: real release signing requires an explicitly configured RFC3161 timestamp service (-TimestampUrl).'
    }
    try {
        $uri = [Uri]::new($TimestampUrl)
        if (-not $uri.IsAbsoluteUri -or $uri.Scheme -notin @('http', 'https')) {
            throw 'invalid scheme'
        }
    } catch {
        throw 'PRODUCTION_TIMESTAMP_REQUIRED: -TimestampUrl must be an absolute http or https RFC3161 service URL.'
    }
}

function Invoke-SigntoolSign {
    param([Parameter(Mandatory)] [string] $ToolPath, [Parameter(Mandatory)] [string[]] $Arguments)
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $ToolPath
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw 'SIGNING_FAILED: signtool did not start.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    if ($process.ExitCode -ne 0) {
        # Do not include stdout/stderr: signtool output can echo provider details or
        # command-line material. The exit code is sufficient for the release report.
        throw "SIGNING_FAILED: signtool exited with code $($process.ExitCode)."
    }
}

function Write-DetachedPackageManifestSignature {
    param(
        [Parameter(Mandatory)] [string] $ManifestPath,
        [Parameter(Mandatory)] [string] $OutputPath,
        [Parameter(Mandatory)] $Certificate
    )

    try { Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop } catch { }
    $content = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($ManifestPath))
    if ($content.Length -eq 0) { throw 'PACKAGE_MANIFEST_SIGNATURE_INPUT_EMPTY' }
    if (-not $Certificate.HasPrivateKey) { throw 'PACKAGE_MANIFEST_SIGNATURE_PRIVATE_KEY_REQUIRED' }
    $contentInfo = [Security.Cryptography.Pkcs.ContentInfo]::new($content)
    $cms = [Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
    $signer = [Security.Cryptography.Pkcs.CmsSigner]::new(
        [Security.Cryptography.Pkcs.SubjectIdentifierType]::IssuerAndSerialNumber, $Certificate)
    $signer.IncludeOption = [Security.Cryptography.X509Certificates.X509IncludeOption]::EndCertOnly
    $cms.ComputeSignature($signer)
    $parent = Split-Path -Parent ([IO.Path]::GetFullPath($OutputPath))
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    [IO.File]::WriteAllBytes([IO.Path]::GetFullPath($OutputPath), $cms.Encode())
}

try {
    if ([string]::IsNullOrWhiteSpace($Provider)) { throw 'AUTHORIZED_SIGNER_REQUIRED: select CertificateStore or Pfx and supply its external signer configuration.' }
    $isManifestOnly = $PSCmdlet.ParameterSetName -eq 'ManifestOnly'
    if (($PackageManifestPath -or $ManifestSignatureOutputPath) -and -not $isManifestOnly) {
        throw 'PACKAGE_MANIFEST_SIGNATURE_REQUIRES_MANIFEST_ONLY: sign final PE artifacts first, generate the final manifest, then use -ManifestOnly so manifest bytes cannot become stale.'
    }
    if ($isManifestOnly -and (-not $PackageManifestPath -or -not $ManifestSignatureOutputPath)) {
        throw 'PACKAGE_MANIFEST_SIGNATURE_PATHS_REQUIRED: -ManifestOnly requires both -PackageManifestPath and -ManifestSignatureOutputPath.'
    }
    if ($isManifestOnly -and -not (Test-Path -LiteralPath $PackageManifestPath -PathType Leaf)) {
        throw "PACKAGE_MANIFEST_REQUIRED: $PackageManifestPath"
    }
    $paths = if ($isManifestOnly) {
        @()
    } elseif ($PSCmdlet.ParameterSetName -eq 'Root') {
        Get-ReleaseArtifactPaths -ArtifactRoot $ArtifactRoot | ForEach-Object { $_.sourcePath }
    } else { @($Artifacts | ForEach-Object { [IO.Path]::GetFullPath($_) }) }
    if (-not $isManifestOnly) {
        if (@($paths).Count -eq 0) { throw 'RELEASE_ARTIFACTS_REQUIRED: no artifacts were supplied.' }
        foreach ($path in $paths) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "RELEASE_ARTIFACT_REQUIRED: missing artifact '$path'." }
            $architecture = Get-PeArchitecture -Path $path
            if ($architecture.architecture -ne 'x64') { throw "WRONG_ARCHITECTURE: '$path' is $($architecture.architecture), expected x64." }
        }
    }

    if (-not $DryRun) { Assert-ProductionTimestampConfiguration }

    $storeSwitch = @()
    if ($Provider -eq 'CertificateStore') {
        $providerThumbprint = Normalize-Thumbprint -Value $CertificateThumbprint
        if (-not (Test-ThumbprintFormat -Value $providerThumbprint)) { throw 'AUTHORIZED_SIGNER_REQUIRED: CertificateStore requires a valid 40-hex -CertificateThumbprint.' }
        $expected = Assert-ExpectedSignerThumbprint -ProviderThumbprint $providerThumbprint
        $certificatePath = "Cert:\$StoreLocation\My\$providerThumbprint"
        $certificate = Get-Item -LiteralPath $certificatePath -ErrorAction SilentlyContinue
        if (-not $certificate -or -not $certificate.HasPrivateKey) { throw "AUTHORIZED_SIGNER_REQUIRED: certificate with private key was not found at $certificatePath." }
        Assert-AuthorizedCertificate -Certificate $certificate -Source $certificatePath
        $certificateForManifest = $certificate
        if ($StoreLocation -eq 'LocalMachine') { $storeSwitch = @('/sm') }
    } else {
        if (-not $PfxPath -or -not (Test-Path -LiteralPath $PfxPath -PathType Leaf)) { throw 'AUTHORIZED_SIGNER_REQUIRED: Pfx requires an externally supplied -PfxPath.' }
        if (-not $PfxPassword) {
            $environmentSecret = [Environment]::GetEnvironmentVariable($PfxPasswordEnvironmentVariable)
            if ([string]::IsNullOrEmpty($environmentSecret)) { throw "AUTHORIZED_SIGNER_REQUIRED: supply -PfxPassword or the '$PfxPasswordEnvironmentVariable' environment variable." }
            $PfxPassword = ConvertTo-SecureString -String $environmentSecret -AsPlainText -Force
        }
        $pfxCertificate = Get-PfxCertificate -LiteralPath ([IO.Path]::GetFullPath($PfxPath)) -Password $PfxPassword -NoPromptForPassword -ErrorAction Stop
        $providerThumbprint = Normalize-Thumbprint -Value $pfxCertificate.Thumbprint
        if (-not (Test-ThumbprintFormat -Value $providerThumbprint)) { throw 'AUTHORIZED_SIGNER_REQUIRED: supplied PFX leaf thumbprint is malformed.' }
        $expected = Assert-ExpectedSignerThumbprint -ProviderThumbprint $providerThumbprint
        Assert-AuthorizedCertificate -Certificate $pfxCertificate -Source ([IO.Path]::GetFullPath($PfxPath))
        $pfxSecret = ConvertTo-PlainSecret -SecureValue $PfxPassword
        $certificateForManifest = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
            [IO.Path]::GetFullPath($PfxPath), $pfxSecret,
            [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet)
    }
    $tool = if ($DryRun -and $isManifestOnly) { $null } else { Resolve-Signtool -RequestedPath $SigntoolPath }
    $preflight = @($paths | ForEach-Object { [pscustomobject]@{ artifact = $_; architecture = (Get-PeArchitecture -Path $_).architecture; present = $true } })
    if ($DryRun) {
        $dry = [pscustomobject]@{ status = 'DRY_RUN'; provider = $Provider; manifestOnly = $isManifestOnly; artifacts = $preflight; expectedSignerThumbprint = $expected; signtool = $tool; timestampUrlConfigured = -not [string]::IsNullOrWhiteSpace($TimestampUrl); packageManifest = $PackageManifestPath; signingPerformed = $false }
        if ($OutputFormat -eq 'json') { $dry | ConvertTo-Json -Depth 8 } else { Write-Host "DRY RUN: $(@($paths).Count) x64 artifact(s) ready for external signer '$Provider'; manifestOnly=$isManifestOnly; no files changed." }
        exit 0
    }

    if ($isManifestOnly) {
        $signingStarted = $true
        Write-DetachedPackageManifestSignature -ManifestPath $PackageManifestPath -OutputPath $ManifestSignatureOutputPath -Certificate $certificateForManifest
        $manifestHashBeforeTimestamp = (Get-FileHash -LiteralPath $PackageManifestPath -Algorithm SHA256).Hash
        Invoke-SigntoolSign -ToolPath $tool -Arguments @('timestamp', '/p7', '/tr', $TimestampUrl, '/td', 'SHA256', [IO.Path]::GetFullPath($ManifestSignatureOutputPath))
        $manifestHashAfterTimestamp = (Get-FileHash -LiteralPath $PackageManifestPath -Algorithm SHA256).Hash
        if ($manifestHashBeforeTimestamp -cne $manifestHashAfterTimestamp) {
            throw 'PACKAGE_MANIFEST_MUTATED_DURING_TIMESTAMP: timestamping changed the detached manifest content.'
        }
        $manifestSignatureCheck = Test-PackageManifestSignature -ManifestPath $PackageManifestPath -SignaturePath $ManifestSignatureOutputPath -AllowedSignerThumbprints @($expected)
        if (-not $manifestSignatureCheck.valid) { throw "PACKAGE_MANIFEST_SIGNATURE_VERIFICATION_FAILED: $($manifestSignatureCheck.reason)" }
        $manifestRecord = [pscustomobject]@{ manifest = [IO.Path]::GetFullPath($PackageManifestPath); signature = [IO.Path]::GetFullPath($ManifestSignatureOutputPath); timestampUrlConfigured = $true; verification = $manifestSignatureCheck }
        $result = [pscustomobject]@{ status = 'PASS'; provider = $Provider; expectedSignerThumbprint = $expected; timestampUrlConfigured = $true; artifacts = @(); packageManifestSignature = $manifestRecord; signingPerformed = $false; manifestSigningPerformed = $true }
        if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 10 } else { Write-Host "PASS: signed and verified package manifest with signer $expected." }
        exit 0
    }

    foreach ($path in $paths) {
        $arguments = @('sign', '/fd', 'SHA256') + $storeSwitch
        if ($Provider -eq 'CertificateStore') {
            $arguments += @('/sha1', $providerThumbprint, '/a')
        } else {
            $arguments += @('/f', [IO.Path]::GetFullPath($PfxPath), '/p', $pfxSecret, '/sha1', $providerThumbprint)
        }
        if ($TimestampUrl) { $arguments += @('/tr', $TimestampUrl, '/td', 'SHA256') }
        $arguments += $path
        $signingStarted = $true
        Invoke-SigntoolSign -ToolPath $tool -Arguments $arguments
        $verified = Get-SignatureRecord -Path $path
        $match = $verified.signed -and $verified.valid -and $verified.leafThumbprint -eq $expected -and
            $verified.timestampSigned -and $verified.timestampValid
        $records += [pscustomobject]@{ artifact = $path; signed = $verified.signed; signatureValid = $verified.valid; leafThumbprint = $verified.leafThumbprint; signerMatchesExpected = ($verified.leafThumbprint -eq $expected); timestampSigned = $verified.timestampSigned; timestampValid = $verified.timestampValid; status = if ($match) { 'PASS' } else { 'FAIL' } }
        if (-not $verified.timestampSigned) { throw "POST_SIGN_TIMESTAMP_MISSING: production artifact '$path' has no RFC3161 timestamp." }
        if (-not $verified.timestampValid) { throw "POST_SIGN_TIMESTAMP_INVALID: production artifact '$path' does not have a valid RFC3161 timestamp." }
        if (-not $match) { throw "POST_SIGN_VERIFICATION_FAILED: signer verification failed for '$path'." }
    }
    $result = [pscustomobject]@{ status = 'PASS'; provider = $Provider; expectedSignerThumbprint = $expected; timestampUrlConfigured = $true; artifacts = $records; packageManifestSignature = $null; signingPerformed = $true; manifestSigningPerformed = $false }
    if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 10 } else { Write-Host "PASS: signed and verified $(@($records).Count) artifact(s) with signer $expected." }
    exit 0
} catch {
    $message = $_.Exception.Message
    $failure = [pscustomobject]@{ status = 'FAIL'; error = $message; signingPerformed = $signingStarted; partialSigningPossible = $signingStarted; verifiedArtifacts = $records; note = 'No credentials or private material were written to the repository; a partial signing failure must be reviewed before reuse.' }
    if ($OutputFormat -eq 'json') { $failure | ConvertTo-Json -Depth 8 } else { Write-Error $message }
    exit 1
} finally {
    if ($pfxSecret) { $pfxSecret = $null }
}
