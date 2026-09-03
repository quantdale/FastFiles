[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-ImportCertificationEvidence.ps1..." -ForegroundColor Cyan

$importer = Join-Path $RepoRoot 'verify\certification\Import-CertificationEvidence.ps1'
if (-not (Test-Path -LiteralPath $importer)) { throw "Importer not found at $importer" }

# Test 1: Valid import on generated soak result
Write-Host "  Test 1: Valid import of soak result..."
$soakArtifact = Join-Path $RepoRoot "openspec\changes\external-certification-readiness-and-evidence-convergence\evidence\resource-soak-summary.json"
if (Test-Path -LiteralPath $soakArtifact) {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $importer `
        -EvidenceBundlePath $soakArtifact `
        -CheckStalenessOnly `
        -RepoRoot $RepoRoot

    if ($LASTEXITCODE -ne 0) { throw "Importer failed on valid artifact with exit $LASTEXITCODE" }
    Write-Host "    PASS (Valid artifact verified and checked for staleness)" -ForegroundColor Green
} else {
    Write-Host "    SKIP (Soak artifact not found)" -ForegroundColor Yellow
}

# Test 2: Secret leakage fails closed
Write-Host "  Test 2: Secret scanning rejects credential leak..."
$leakJson = [IO.Path]::GetTempFileName()
try {
    $leakPayload = @{
        schemaVersion = 1
        lane = "external-provider"
        status = "PASS"
        commit = "1111111111111111111111111111111111111111"
        tree = "2222222222222222222222222222222222222222"
        exactTreeBound = $true
        secret = "-----BEGIN RSA PRIVATE KEY----- abc -----"
        disposition = "PASS"
    } | ConvertTo-Json
    Set-Content -LiteralPath $leakJson -Value $leakPayload -Encoding utf8

    $leakOut = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $importer `
        -EvidenceBundlePath $leakJson `
        -CheckStalenessOnly `
        -RepoRoot $RepoRoot 2>&1

    if ($LASTEXITCODE -eq 0) {
        throw "Expected secret leak artifact to be rejected, but importer passed"
    }
    if (($leakOut -join ' ') -notmatch 'Security rejection') {
        throw "Expected security rejection message, got: $($leakOut -join ' ')"
    }
    Write-Host "    PASS (Secret pattern rejected fail-closed)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $leakJson) { Remove-Item -LiteralPath $leakJson -Force -ErrorAction SilentlyContinue }
}

# Test 3: All-zero SHA fails closed
Write-Host "  Test 3: Zero-SHA rejection..."
$zeroJson = [IO.Path]::GetTempFileName()
try {
    $zeroPayload = @{
        schemaVersion = 1
        lane = "production-signing"
        status = "PASS"
        commit = "0000000000000000000000000000000000000000"
        tree = "0000000000000000000000000000000000000000"
        exactTreeBound = $true
        disposition = "PASS"
    } | ConvertTo-Json
    Set-Content -LiteralPath $zeroJson -Value $zeroPayload -Encoding utf8

    $zeroOut = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $importer `
        -EvidenceBundlePath $zeroJson `
        -CheckStalenessOnly `
        -RepoRoot $RepoRoot 2>&1

    if ($LASTEXITCODE -eq 0) {
        throw "Expected zero-SHA artifact to be rejected, but importer passed"
    }
    if (($zeroOut -join ' ') -notmatch 'all-zero') {
        throw "Expected zero-SHA error message, got: $($zeroOut -join ' ')"
    }
    Write-Host "    PASS (All-zero SHA rejected fail-closed)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $zeroJson) { Remove-Item -LiteralPath $zeroJson -Force -ErrorAction SilentlyContinue }
}

Write-Host "All Test-ImportCertificationEvidence tests PASSED!" -ForegroundColor Green
