[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-LongRunCertification.ps1..." -ForegroundColor Cyan

$runner = Join-Path $RepoRoot 'verify\certification\Invoke-LongRunCertification.ps1'
$schemaPath = Join-Path $RepoRoot 'verify\certification\schemas\long-run-result.schema.json'

if (-not (Test-Path -LiteralPath $runner)) { throw "Runner not found at $runner" }
if (-not (Test-Path -LiteralPath $schemaPath)) { throw "Schema not found at $schemaPath" }

# Test 1: Successful execution emits valid schema-conforming artifact
Write-Host "  Test 1: Valid execution produces schema-compliant JSON..."
$tempJson = [IO.Path]::GetTempFileName()
try {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -TestTarget ffaddress_bar_focus_tests `
        -Iterations 2 `
        -Category uia `
        -OutputPath $tempJson `
        -RepoRoot $RepoRoot

    if ($LASTEXITCODE -ne 0) { throw "Test 1 runner failed with exit code $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $tempJson)) { throw "Output JSON was not created" }

    # Validate schema via python
    $pyCmd = "import json, jsonschema; s = json.load(open(r'$schemaPath')); d = json.load(open(r'$tempJson')); jsonschema.validate(instance=d, schema=s); print('OK')"
    $val = & (Get-Command python).Source -c $pyCmd 2>&1
    if ($val -notmatch 'OK') { throw "Schema validation failed: $val" }

    $json = Get-Content -LiteralPath $tempJson -Raw | ConvertFrom-Json
    if ($json.disposition -ne 'PASS') { throw "Expected PASS, got $($json.disposition)" }
    if ($json.passed -ne 2) { throw "Expected 2 passed, got $($json.passed)" }
    if ($json.failed -ne 0) { throw "Expected 0 failed, got $($json.failed)" }

    # Verify sidecar hash matches actual file hash
    $sidecar = "$tempJson.sha256"
    if (-not (Test-Path -LiteralPath $sidecar)) { throw "Expected sidecar checksum file $sidecar was not created" }
    $sidecarText = (Get-Content -LiteralPath $sidecar -Raw).Trim()
    $sidecarHash = ($sidecarText -split '\s+')[0].ToLowerInvariant()
    $fileBytes = [IO.File]::ReadAllBytes($tempJson)
    $actualHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
    if ($sidecarHash -ne $actualHash) { throw "Sidecar hash ($sidecarHash) does not match actual file bytes ($actualHash)" }

    # Verify path sanitization: command should be relative or forward-slash normalized
    if ($json.command -match '^[a-zA-Z]:\\') {
        throw "Command path should be repository-relative or normalized, got absolute Windows path: $($json.command)"
    }

    Write-Host "    PASS" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson.sha256") { Remove-Item -LiteralPath "$tempJson.sha256" -Force -ErrorAction SilentlyContinue }
}

# Test 2: Exact-tree enforcement rejects dirty trees when required
Write-Host "  Test 2: Exact-tree requirement rejects dirty worktree..."
$dirtyFixture = Join-Path $RepoRoot "temp-dirty-fixture-$([guid]::NewGuid().ToString('n')).tmp"
try {
    # Ensure working tree is dirty for this test fixture
    Set-Content -LiteralPath $dirtyFixture -Value "dirty tree test fixture" -Encoding utf8

    $dirtyOutput = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -TestTarget ffaddress_bar_focus_tests `
        -Iterations 1 `
        -Category uia `
        -RequireExactTree `
        -RepoRoot $RepoRoot 2>&1

    if ($LASTEXITCODE -eq 0) {
        throw "Expected -RequireExactTree on dirty tree to fail, but exit code was 0"
    }
    if (($dirtyOutput -join ' ') -notmatch 'Exact-tree qualification required') {
        throw "Expected error message about exact-tree qualification, got: $($dirtyOutput -join ' ')"
    }
    Write-Host "    PASS" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $dirtyFixture) {
        Remove-Item -LiteralPath $dirtyFixture -Force -ErrorAction SilentlyContinue
    }
}

# Test 3: Missing executable / invalid target fails closed (CT-001 regression)
Write-Host "  Test 3: Non-existent target fails closed as INVALID_TARGET..."
$missingOutput = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
    -TestTarget non_existent_target_12345 `
    -Iterations 1 `
    -Category uia `
    -RepoRoot $RepoRoot 2>&1

if ($LASTEXITCODE -eq 0) {
    throw "Expected non-existent target to fail, but exit code was 0"
}
if (($missingOutput -join ' ') -notmatch 'INVALID_TARGET') {
    throw "Expected INVALID_TARGET in error message, got: $($missingOutput -join ' ')"
}
Write-Host "    PASS (Zero-test CTest target correctly rejected with INVALID_TARGET)" -ForegroundColor Green

Write-Host "All Test-LongRunCertification tests PASSED!" -ForegroundColor Green
