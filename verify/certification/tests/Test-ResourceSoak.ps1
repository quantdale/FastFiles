[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-ResourceSoak.ps1..." -ForegroundColor Cyan

$runner = Join-Path $RepoRoot 'verify\certification\Invoke-ResourceSoak.ps1'
$schemaPath = Join-Path $RepoRoot 'verify\certification\schemas\resource-soak-result.schema.json'

if (-not (Test-Path -LiteralPath $runner)) { throw "Runner not found at $runner" }
if (-not (Test-Path -LiteralPath $schemaPath)) { throw "Schema not found at $schemaPath" }

# Test 1: Successful short soak execution emits valid schema-conforming artifact
Write-Host "  Test 1: Valid soak execution produces schema-compliant JSON..."
$tempJson = [IO.Path]::GetTempFileName()
try {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -DurationSeconds 4 `
        -SampleIntervalSeconds 1 `
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
    if ($json.sampleCount -lt 1) { throw "Expected at least 1 sample, got $($json.sampleCount)" }
    if ($null -eq $json.metrics.workingSetBytes.baseline) { throw "Missing workingSetBytes baseline" }
    if ($null -eq $json.metrics.handles.baseline) { throw "Missing handles baseline" }

    # Verify process terminated cleanly
    $pidToTest = $json.fastFilesPid
    $alive = Get-Process -Id $pidToTest -ErrorAction SilentlyContinue
    if ($null -ne $alive) { throw "Process $pidToTest was still running after soak completion!" }

    Write-Host "    PASS (PID $pidToTest terminated cleanly, metrics valid)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson -Force -ErrorAction SilentlyContinue }
}

# Test 2: Exact-tree requirement rejects dirty worktree
Write-Host "  Test 2: Exact-tree requirement rejects dirty worktree..."
$dirtyOutput = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
    -DurationSeconds 2 `
    -SampleIntervalSeconds 1 `
    -RequireExactTree `
    -RepoRoot $RepoRoot 2>&1

if ($LASTEXITCODE -eq 0) {
    throw "Expected -RequireExactTree on dirty tree to fail, but exit code was 0"
}
if (($dirtyOutput -join ' ') -notmatch 'Exact-tree qualification required') {
    throw "Expected error message about exact-tree qualification, got: $($dirtyOutput -join ' ')"
}
Write-Host "    PASS" -ForegroundColor Green

Write-Host "All Test-ResourceSoak tests PASSED!" -ForegroundColor Green
