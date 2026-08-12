[CmdletBinding(DefaultParameterSetName = 'Root')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Files')]
    [ValidateNotNullOrEmpty()] [string[]] $Artifacts,
    [Parameter(Mandatory, ParameterSetName = 'Root')]
    [string] $ArtifactRoot,
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
    [switch] $DryRun,
    [ValidateSet('human', 'json')]
    [string] $OutputFormat = 'human'
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking

$providerThumbprint = $null
$pfxSecret = $null
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
    if (-not (Test-ProductionCertificate -Certificate $Certificate)) {
        throw "DEVELOPMENT_CERTIFICATE_NOT_ALLOWED: self-signed certificate supplied from $Source; an authorized production code-signing chain is required."
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

try {
    if ([string]::IsNullOrWhiteSpace($Provider)) { throw 'AUTHORIZED_SIGNER_REQUIRED: select CertificateStore or Pfx and supply its external signer configuration.' }
    $paths = if ($PSCmdlet.ParameterSetName -eq 'Root') {
        Get-ReleaseArtifactPaths -ArtifactRoot $ArtifactRoot | ForEach-Object { $_.sourcePath }
    } else { @($Artifacts | ForEach-Object { [IO.Path]::GetFullPath($_) }) }
    if (@($paths).Count -eq 0) { throw 'RELEASE_ARTIFACTS_REQUIRED: no artifacts were supplied.' }
    foreach ($path in $paths) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "RELEASE_ARTIFACT_REQUIRED: missing artifact '$path'." }
        $architecture = Get-PeArchitecture -Path $path
        if ($architecture.architecture -ne 'x64') { throw "WRONG_ARCHITECTURE: '$path' is $($architecture.architecture), expected x64." }
    }

    $storeSwitch = @()
    if ($Provider -eq 'CertificateStore') {
        $providerThumbprint = Normalize-Thumbprint -Value $CertificateThumbprint
        if (-not (Test-ThumbprintFormat -Value $providerThumbprint)) { throw 'AUTHORIZED_SIGNER_REQUIRED: CertificateStore requires a valid 40-hex -CertificateThumbprint.' }
        $certificatePath = "Cert:\$StoreLocation\My\$providerThumbprint"
        $certificate = Get-Item -LiteralPath $certificatePath -ErrorAction SilentlyContinue
        if (-not $certificate -or -not $certificate.HasPrivateKey) { throw "AUTHORIZED_SIGNER_REQUIRED: certificate with private key was not found at $certificatePath." }
        Assert-AuthorizedCertificate -Certificate $certificate -Source $certificatePath
        if ($StoreLocation -eq 'LocalMachine') { $storeSwitch = @('/sm') }
    } else {
        if (-not $PfxPath -or -not (Test-Path -LiteralPath $PfxPath -PathType Leaf)) { throw 'AUTHORIZED_SIGNER_REQUIRED: Pfx requires an externally supplied -PfxPath.' }
        if (-not $PfxPassword) {
            $environmentSecret = [Environment]::GetEnvironmentVariable($PfxPasswordEnvironmentVariable)
            if ([string]::IsNullOrEmpty($environmentSecret)) { throw "AUTHORIZED_SIGNER_REQUIRED: supply -PfxPassword or the '$PfxPasswordEnvironmentVariable' environment variable." }
            $PfxPassword = ConvertTo-SecureString -String $environmentSecret -AsPlainText -Force
        }
        $pfxCertificate = Get-PfxCertificate -FilePath $PfxPath -ErrorAction Stop
        $providerThumbprint = Normalize-Thumbprint -Value $pfxCertificate.Thumbprint
        if (-not (Test-ThumbprintFormat -Value $providerThumbprint)) { throw 'AUTHORIZED_SIGNER_REQUIRED: supplied PFX leaf thumbprint is malformed.' }
        Assert-AuthorizedCertificate -Certificate $pfxCertificate -Source ([IO.Path]::GetFullPath($PfxPath))
        $pfxSecret = ConvertTo-PlainSecret -SecureValue $PfxPassword
    }
    $expected = if ($ExpectedSignerThumbprint) { Normalize-Thumbprint -Value $ExpectedSignerThumbprint } else { $providerThumbprint }
    if (-not (Test-UsableThumbprint -Value $expected)) { throw 'MALFORMED_EXPECTED_SIGNER: expected signer thumbprint must be a non-placeholder 40-hex value.' }
    $tool = Resolve-Signtool -RequestedPath $SigntoolPath
    $preflight = @($paths | ForEach-Object { [pscustomobject]@{ artifact = $_; architecture = (Get-PeArchitecture -Path $_).architecture; present = $true } })
    if ($DryRun) {
        $dry = [pscustomobject]@{ status = 'DRY_RUN'; provider = $Provider; artifacts = $preflight; expectedSignerThumbprint = $expected; signtool = $tool; signingPerformed = $false }
        if ($OutputFormat -eq 'json') { $dry | ConvertTo-Json -Depth 8 } else { Write-Host "DRY RUN: $(@($paths).Count) x64 artifact(s) ready for external signer '$Provider'; no files changed." }
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
        $match = $verified.valid -and $verified.leafThumbprint -eq $expected
        $records += [pscustomobject]@{ artifact = $path; signed = $verified.signed; signatureValid = $verified.valid; leafThumbprint = $verified.leafThumbprint; signerMatchesExpected = $match; timestampSigned = $verified.timestampSigned; status = if ($match) { 'PASS' } else { 'FAIL' } }
        if (-not $match) { throw "POST_SIGN_VERIFICATION_FAILED: signer verification failed for '$path'." }
    }
    $result = [pscustomobject]@{ status = 'PASS'; provider = $Provider; expectedSignerThumbprint = $expected; artifacts = $records; signingPerformed = $true }
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
