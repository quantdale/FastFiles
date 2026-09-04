[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-ImportCertificationEvidence.ps1..." -ForegroundColor Cyan

$importer = Join-Path $RepoRoot 'verify\certification\Import-CertificationEvidence.ps1'
if (-not (Test-Path -LiteralPath $importer)) { throw "Importer not found at $importer" }

# Test 1: Valid import on valid long-run result
Write-Host "  Test 1: Valid import of long-run result..."
$validArtifact = Join-Path $RepoRoot "openspec\changes\external-certification-readiness-and-evidence-convergence\evidence\long-run-handshake-100.json"
if (Test-Path -LiteralPath $validArtifact) {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $importer `
        -EvidenceBundlePath $validArtifact `
        -CheckStalenessOnly `
        -RepoRoot $RepoRoot

    if ($LASTEXITCODE -ne 0) { throw "Importer failed on valid artifact with exit $LASTEXITCODE" }
    Write-Host "    PASS (Valid artifact verified and checked for staleness)" -ForegroundColor Green
} else {
    Write-Host "    SKIP (Artifact not found)" -ForegroundColor Yellow
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

# Test 4: Path traversal attack fails closed (Section 56)
Write-Host "  Test 4: Path traversal rejection..."
$traversalOut = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $importer `
    -EvidenceBundlePath (Join-Path $RepoRoot "openspec/lifecycle.json") `
    -TargetChange "../../etc" `
    -CheckStalenessOnly `
    -RepoRoot $RepoRoot 2>&1

if ($LASTEXITCODE -eq 0) {
    throw "Expected path traversal in TargetChange to fail closed, but exit code was 0"
}
if (($traversalOut -join ' ') -notmatch 'path traversal') {
    throw "Expected path traversal error message, got: $($traversalOut -join ' ')"
}
Write-Host "    PASS (Path traversal rejected fail-closed)" -ForegroundColor Green

# Test 5: Checksum tampering fails closed (Section 56)
Write-Host "  Test 5: Tampered artifact checksum fails closed..."
$tamperJson = [IO.Path]::GetTempFileName()
$tamperSidecar = "$tamperJson.sha256"
try {
    Set-Content -LiteralPath $tamperJson -Value '{"test": 1}' -Encoding utf8
    Set-Content -LiteralPath $tamperSidecar -Value 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 dummy.json' -Encoding utf8

    $tamperOut = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $importer `
        -EvidenceBundlePath $tamperJson `
        -CheckStalenessOnly `
        -RepoRoot $RepoRoot 2>&1

    if ($LASTEXITCODE -eq 0) {
        throw "Expected tampered checksum to fail closed, but exit code was 0"
    }
    if (($tamperOut -join ' ') -notmatch 'Integrity failure') {
        throw "Expected integrity failure message, got: $($tamperOut -join ' ')"
    }
    Write-Host "    PASS (Tampered artifact checksum rejected fail-closed)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tamperJson) { Remove-Item -LiteralPath $tamperJson -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $tamperSidecar) { Remove-Item -LiteralPath $tamperSidecar -Force -ErrorAction SilentlyContinue }
}

# Test 6: Contradictory PASS status fails closed (Section 56)
Write-Host "  Test 6: Contradictory PASS status fails closed..."
$contraJson = [IO.Path]::GetTempFileName()
try {
    $contraPayload = @{
        schemaVersion = 1
        testId = "fake_test"
        category = "long-run"
        command = "fake.exe"
        commit = "1111111111111111111111111111111111111111"
        tree = "2222222222222222222222222222222222222222"
        exactTreeBound = $true
        disposition = "PASS"
        failed = 5
    } | ConvertTo-Json
    Set-Content -LiteralPath $contraJson -Value $contraPayload -Encoding utf8

    $contraOut = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $importer `
        -EvidenceBundlePath $contraJson `
        -CheckStalenessOnly `
        -RepoRoot $RepoRoot 2>&1

    if ($LASTEXITCODE -eq 0) {
        throw "Expected contradictory PASS status to fail closed, but exit code was 0"
    }
    if (($contraOut -join ' ') -notmatch 'Contradictory evidence') {
        throw "Expected contradictory evidence error message, got: $($contraOut -join ' ')"
    }
    Write-Host "    PASS (Contradictory PASS with failed > 0 rejected fail-closed)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $contraJson) { Remove-Item -LiteralPath $contraJson -Force -ErrorAction SilentlyContinue }
}

Write-Host "All Test-ImportCertificationEvidence tests PASSED!" -ForegroundColor Green
