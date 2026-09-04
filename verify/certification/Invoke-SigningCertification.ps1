[CmdletBinding()]
param(
    [string] $CandidatePath,

    [string] $SignerThumbprint,

    [string] $ExpectedSubject,

    [string] $OutputPath,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

# AUDIT-ONLY: this lane verifies signatures and certificate pins. It NEVER signs anything
# and NEVER fabricates test credentials. Absence of a candidate or pin is fail-closed
# (REQUIRED-BUT-UNAVAILABLE / BLOCKED_EXTERNAL), never PASS-by-absence.

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
$candidatePresent = ($CandidatePath -and (Test-Path -LiteralPath $CandidatePath -PathType Leaf))
$hashHandleClosed = $true
if ($candidatePresent) {
    # Candidate identity + hash (stream is closed in finally; nothing is modified).
    $candidateItem = Get-Item -LiteralPath $CandidatePath
    $sha256 = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($CandidatePath)
    $hashHandleClosed = $false
    try {
        $candidateHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Close()
        $hashHandleClosed = $true
    }
    Add-CheckResult -Id "candidate-identity" -Status "PASS" -Message "Candidate identity captured: $($candidateItem.Name) ($($candidateItem.Length) bytes, SHA256 $candidateHash)" -DetailObj ([ordered]@{
            fullPath = $candidateItem.FullName
            leaf = $candidateItem.Name
            bytes = $candidateItem.Length
            sha256 = $candidateHash
        })

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

    # Signer certificate identity + chain (offline revocation mode documented, not silent).
    if ($sig.SignerCertificate) {
        $chain = New-Object Security.Cryptography.X509Certificates.X509Chain
        $chain.ChainPolicy.RevocationMode = [Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
        $chainErr = $null
        $built = $false
        try {
            $built = $chain.Build($sig.SignerCertificate)
        } catch {
            $built = $false
            $chainErr = $_.Exception.Message
        }
        $chainDetail = [ordered]@{
            subject = $sig.SignerCertificate.Subject
            issuer = $sig.SignerCertificate.Issuer
            thumbprint = $sig.SignerCertificate.Thumbprint
            chainStatus = @($chain.ChainStatus | ForEach-Object { "$($_.Status): $($_.StatusInformation)".Trim() })
        }
        if ($built) {
            Add-CheckResult -Id "signer-chain" -Status "PASS" -Message "Signer certificate chain builds to a trusted root (offline revocation mode NoCheck; revocation freshness is not asserted without network)" -DetailObj $chainDetail
        } else {
            $chainMsg = "Signer certificate chain does not build to a trusted root"
            if ($chainErr) { $chainMsg += ": $chainErr" }
            Add-CheckResult -Id "signer-chain" -Status "FAIL" -Message $chainMsg -DetailObj $chainDetail
        }
    } else {
        Add-CheckResult -Id "signer-chain" -Status "FAIL" -Message "No signer certificate present; certificate identity and chain cannot be established"
    }

    # Expected subject comparison (honors the pre-existing $ExpectedSubject parameter).
    if ($ExpectedSubject) {
        $actualSubject = if ($sig.SignerCertificate) { $sig.SignerCertificate.Subject } else { '' }
        if (-not [string]::IsNullOrEmpty($actualSubject) -and ($actualSubject.IndexOf($ExpectedSubject, [StringComparison]::OrdinalIgnoreCase) -ge 0)) {
            Add-CheckResult -Id "signer-subject-match" -Status "PASS" -Message "Signer subject contains expected value: $ExpectedSubject"
        } else {
            Add-CheckResult -Id "signer-subject-match" -Status "FAIL" -Message "Subject mismatch: expected to contain '$ExpectedSubject', actual '$actualSubject'"
        }
    } else {
        Add-CheckResult -Id "signer-subject-match" -Status "SKIPPED" -Message "Expected subject was not supplied; subject comparison skipped"
    }

    if ($sig.TimeStamperCertificate) {
        Add-CheckResult -Id "rfc3161-timestamp" -Status "PASS" -Message "RFC3161 timestamp is present"
    } else {
        Add-CheckResult -Id "rfc3161-timestamp" -Status "FAIL" -Message "RFC3161 timestamp is missing from signature"
    }
} else {
    # Fail-closed: no candidate means no credentials are fabricated and no PASS is possible.
    Add-CheckResult -Id "candidate-identity" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Production signed candidate binary was not supplied (fail-closed: no test credentials fabricated)"
    Add-CheckResult -Id "production-signed-candidate" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Production signed candidate binary was not supplied (fail-closed: no test credentials fabricated)"
    Add-CheckResult -Id "signer-chain" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Signer chain cannot be evaluated without a candidate binary"
}

# 5. Cleanup record (audit-only lane opens no fixtures; the candidate hash stream above is
# closed in finally). The cleanup object is REQUIRED in every result; a dirty cleanup
# forces FAIL and is never hidden.
$cleanupFailed = (-not $hashHandleClosed)
$cleanup = [ordered]@{
    status = $(if ($cleanupFailed) { 'FAIL' } else { 'PASS' })
    handlesClosed = ([bool]$hashHandleClosed)
    message = $(if ($cleanupFailed) { "Candidate hash stream was not closed" } else { "No fixtures created; candidate hash stream closed" })
}

$stopwatch.Stop()
$endUtc = [DateTime]::UtcNow

$hasFail = @($checks | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0
$hasRequired = @($checks | Where-Object { $_.status -eq 'REQUIRED-BUT-UNAVAILABLE' }).Count -gt 0

$laneStatus = $(if ($hasFail -or $cleanupFailed) { 'FAIL' }
                elseif ($hasRequired) { 'REQUIRED-BUT-UNAVAILABLE' }
                else { 'PASS' })

$disposition = $(if ($laneStatus -eq 'PASS') { 'PASS' }
                 elseif ($laneStatus -eq 'REQUIRED-BUT-UNAVAILABLE') { 'BLOCKED_EXTERNAL' }
                 else { 'FAIL' })

# production-signing is claimed ONLY when a supplied candidate actually verifies end to end.
$authOk = (@($checks | Where-Object { $_.checkId -eq 'authenticode-signature' -and $_.status -eq 'PASS' }).Count -gt 0)
$pinOk = (@($checks | Where-Object { $_.checkId -eq 'signer-pin-match' -and $_.status -eq 'PASS' }).Count -gt 0)
$chainOk = (@($checks | Where-Object { $_.checkId -eq 'signer-chain' -and $_.status -eq 'PASS' }).Count -gt 0)
$tsOk = (@($checks | Where-Object { $_.checkId -eq 'rfc3161-timestamp' -and $_.status -eq 'PASS' }).Count -gt 0)
$capabilities = @()
if ($authOk -and $pinOk -and $chainOk -and $tsOk) { $capabilities += 'production-signing' }

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
    failureReason = $(if ($cleanupFailed) { "Signing certification cleanup verification failed" } elseif ($hasFail) { "Signing verification checks failed" } else { $null })
    details = [ordered]@{
        candidateSupplied = (-not [string]::IsNullOrEmpty($CandidatePath))
        signerThumbprintSupplied = (-not [string]::IsNullOrEmpty($SignerThumbprint))
    }
    capabilities = @($capabilities)
    cleanup = $cleanup
    disposition = $disposition
    artifactHashes = [ordered]@{}
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
Set-Content -LiteralPath "$OutputPath.sha256" -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM
Write-Host "Signing certification finished with status: $laneStatus (disposition: $disposition)"
Write-Host "Artifact written: $OutputPath"

return [pscustomobject]$resultObject
