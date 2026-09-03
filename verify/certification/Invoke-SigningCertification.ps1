[CmdletBinding()]
param(
    [string] $CandidatePath,

    [string] $SignerThumbprint,

    [string] $ExpectedSubject,

    [string] $OutputPath,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

$startUtc = [DateTime]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

# 1. Capture Git Provenance
$headSha = (& git -C $RepoRoot rev-parse HEAD 2>&1).ToString().Trim()
$treeSha = (& git -C $RepoRoot write-tree 2>&1).ToString().Trim()
$porcelain = @(& git -C $RepoRoot status --porcelain 2>&1 | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne '' })
$isDirty = ($porcelain.Count -gt 0)

if ($RequireExactTree -and $isDirty) {
    throw "Exact-tree qualification required, but repository working tree is dirty ($($porcelain.Count) changes)."
}

$exactTreeBound = (-not $isDirty)

# 2. Output Setup
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\signing"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "signing-certification-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

$checks = [System.Collections.Generic.List[object]]::new()

function Add-CheckResult {
    param([string] $Id, [string] $Status, [string] $Message, $DetailObj = $null)
    $script:checks.Add([pscustomobject]@{
        checkId = $Id
        status = $Status
        message = $Message
        details = $DetailObj
    })
    $color = $(if ($Status -eq 'PASS') { 'Green' } elseif ($Status -match 'BLOCKED' -or $Status -match 'REQUIRED') { 'Yellow' } else { 'Red' })
    Write-Host "  [$Status] ${Id}: $Message" -ForegroundColor $color
}

Write-Host "Starting Production Signing Certification Package:"
Write-Host "  Candidate: $(if ($CandidatePath) { $CandidatePath } else { '<not supplied>' })"
Write-Host "  Thumbprint: $(if ($SignerThumbprint) { $SignerThumbprint } else { '<not supplied>' })"
Write-Host "  Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"

# 3. Verify Invariant: Unsigned Candidate Rejection
$unsignedTarget = Join-Path $RepoRoot "build\debug\src\ui\FastFiles.exe"
if (Test-Path -LiteralPath $unsignedTarget) {
    $sig = Get-AuthenticodeSignature -FilePath $unsignedTarget -ErrorAction SilentlyContinue
    if ($sig.Status -eq 'NotSigned') {
        Add-CheckResult -Id "unsigned-candidate-rejection" -Status "PASS" -Message "Unsigned development binary is correctly recognized as NotSigned"
    } else {
        Add-CheckResult -Id "unsigned-candidate-rejection" -Status "FAIL" -Message "Unexpected signature status on unsigned build: $($sig.Status)"
    }
} else {
    Add-CheckResult -Id "unsigned-candidate-rejection" -Status "SKIPPED" -Message "Unsigned test candidate not found at $unsignedTarget"
}

# 4. Verify Signed Candidate (if provided)
$candidateHash = $null
if ($CandidatePath -and (Test-Path -LiteralPath $CandidatePath -PathType Leaf)) {
    $sha256 = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($CandidatePath)
    $candidateHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    $stream.Close()

    $sig = Get-AuthenticodeSignature -FilePath $CandidatePath
    if ($sig.Status -eq 'Valid') {
        Add-CheckResult -Id "authenticode-signature" -Status "PASS" -Message "Authenticode signature is cryptographically Valid"
    } else {
        Add-CheckResult -Id "authenticode-signature" -Status "FAIL" -Message "Authenticode signature status: $($sig.Status) - $($sig.StatusMessage)"
    }

    $actualThumbprint = if ($sig.SignerCertificate) { $sig.SignerCertificate.Thumbprint.ToUpperInvariant() } else { $null }
    if ($SignerThumbprint) {
        $expectedNorm = $SignerThumbprint.Replace(':', '').Replace(' ', '').ToUpperInvariant()
        if ($actualThumbprint -eq $expectedNorm) {
            Add-CheckResult -Id "signer-pin-match" -Status "PASS" -Message "Signer thumbprint matches expected production pin: $actualThumbprint"
        } else {
            Add-CheckResult -Id "signer-pin-match" -Status "FAIL" -Message "Thumbprint mismatch: expected $expectedNorm, actual $actualThumbprint"
        }
    } else {
        Add-CheckResult -Id "signer-pin-match" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Expected signer thumbprint pin was not supplied"
    }

    if ($sig.TimeStamperCertificate) {
        Add-CheckResult -Id "rfc3161-timestamp" -Status "PASS" -Message "RFC3161 timestamp is present"
    } else {
        Add-CheckResult -Id "rfc3161-timestamp" -Status "FAIL" -Message "RFC3161 timestamp is missing from signature"
    }
} else {
    Add-CheckResult -Id "production-signed-candidate" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Production signed candidate binary was not supplied (fail-closed: no test credentials fabricated)"
}

$stopwatch.Stop()
$endUtc = [DateTime]::UtcNow

$hasFail = @($checks | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0
$hasRequired = @($checks | Where-Object { $_.status -eq 'REQUIRED-BUT-UNAVAILABLE' }).Count -gt 0

$laneStatus = $(if ($hasFail) { 'FAIL' }
                elseif ($hasRequired) { 'REQUIRED-BUT-UNAVAILABLE' }
                else { 'PASS' })

$disposition = $(if ($laneStatus -eq 'PASS') { 'PASS' }
                 elseif ($laneStatus -eq 'REQUIRED-BUT-UNAVAILABLE') { 'BLOCKED_EXTERNAL' }
                 else { 'FAIL' })

$resultObject = [ordered]@{
    schemaVersion = 1
    lane = "production-signing"
    status = $laneStatus
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    candidatePath = $CandidatePath
    candidateHash = $candidateHash
    environment = [ordered]@{
        os = [Environment]::OSVersion.ToString()
        architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        computer = "<local-host>"
        powershell = $PSVersionTable.PSVersion.ToString()
        elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    }
    startUtc = $startUtc.ToString('o')
    endUtc = $endUtc.ToString('o')
    durationMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 2)
    checks = @($checks)
    failureReason = $(if ($hasFail) { "Signing verification checks failed" } else { $null })
    details = [ordered]@{
        candidateSupplied = (-not [string]::IsNullOrEmpty($CandidatePath))
        signerThumbprintSupplied = (-not [string]::IsNullOrEmpty($SignerThumbprint))
    }
    disposition = $disposition
    artifactHashes = [ordered]@{}
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$artifactHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
$resultObject.artifactHashes[([IO.Path]::GetFileName($OutputPath))] = $artifactHash

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

Write-Host "Signing certification finished with status: $laneStatus (disposition: $disposition)"
Write-Host "Artifact written: $OutputPath"

return [pscustomobject]$resultObject
