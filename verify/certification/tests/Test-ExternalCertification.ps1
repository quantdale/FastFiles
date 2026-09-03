[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-ExternalCertification.ps1..." -ForegroundColor Cyan

$runner = Join-Path $RepoRoot 'verify\certification\Invoke-ExternalCertification.ps1'
$schemaPath = Join-Path $RepoRoot 'verify\certification\schemas\certification-result.schema.json'

if (-not (Test-Path -LiteralPath $runner)) { throw "Runner not found at $runner" }
if (-not (Test-Path -LiteralPath $schemaPath)) { throw "Schema not found at $schemaPath" }

# Test 1: Orchestrator execution in audit mode produces valid schema-compliant artifact
Write-Host "  Test 1: Orchestrator audit execution..."
$tempJson = [IO.Path]::GetTempFileName()
try {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -OutputPath $tempJson `
        -RepoRoot $RepoRoot

    if ($LASTEXITCODE -ne 0) { throw "Runner failed with exit code $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $tempJson)) { throw "Output JSON was not created" }

    $pyCmd = "import json, jsonschema; s = json.load(open(r'$schemaPath')); d = json.load(open(r'$tempJson')); jsonschema.validate(instance=d, schema=s); print('OK')"
    $val = & (Get-Command python).Source -c $pyCmd 2>&1
    if ($val -notmatch 'OK') { throw "Schema validation failed: $val" }

    $json = Get-Content -LiteralPath $tempJson -Raw | ConvertFrom-Json
    if ($json.lane -ne 'orchestrator') { throw "Expected lane orchestrator, got $($json.lane)" }
    if ($json.disposition -eq 'PASS') {
        throw "Invariant failure: Orchestrator must not report PASS when external lanes are unexecuted / blocked (no success by skip)"
    }

    # Verify capability matrix
    $matrix = $json.details.capabilityMatrix
    $expectedCaps = @('administrativeElevation', 'supportedWindowsVersion', 'ntfsVolumeAvailable', 'interactiveDesktop', 'productionCandidateSupplied', 'providerConfigured')
    foreach ($cap in $expectedCaps) {
        if ($null -eq $matrix.$cap) {
            throw "Missing capability in matrix: $cap"
        }
    }

    # Verify all 4 lanes dispatched
    $lanes = $json.details.laneResults
    $expectedLanes = @('clean-host', 'privileged-ntfs', 'production-signing', 'external-provider')
    foreach ($l in $expectedLanes) {
        if ($null -eq $lanes.$l) {
            throw "Missing lane result: $l"
        }
    }

    Write-Host "    PASS (Status: $($json.status), Disposition: $($json.disposition), Lanes: 4/4 dispatched)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson -Force -ErrorAction SilentlyContinue }
}

Write-Host "All Test-ExternalCertification tests PASSED!" -ForegroundColor Green
