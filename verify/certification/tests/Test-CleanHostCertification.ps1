[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-CleanHostCertification.ps1..." -ForegroundColor Cyan

$runner = Join-Path $RepoRoot 'verify\certification\Invoke-CleanHostCertification.ps1'
$schemaPath = Join-Path $RepoRoot 'verify\certification\schemas\certification-result.schema.json'

if (-not (Test-Path -LiteralPath $runner)) { throw "Runner not found at $runner" }
if (-not (Test-Path -LiteralPath $schemaPath)) { throw "Schema not found at $schemaPath" }

# Test 1: Audit-only mode execution produces valid schema-conforming artifact
Write-Host "  Test 1: Audit-only execution produces schema-compliant JSON..."
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
    if ($json.lane -ne 'clean-host') { throw "Expected lane clean-host, got $($json.lane)" }
    if ($json.checks.Count -lt 3) { throw "Expected at least 3 checks, got $($json.checks.Count)" }

    Write-Host "    PASS (Checks: $($json.checks.Count), Status: $($json.status))" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson -Force -ErrorAction SilentlyContinue }
}

# Test 2: Missing candidate package when InstallCandidate requested fails closed
Write-Host "  Test 2: Install candidate without package fails closed..."
$tempJson2 = [IO.Path]::GetTempFileName()
try {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -InstallCandidate `
        -OutputPath $tempJson2 `
        -RepoRoot $RepoRoot

    $json2 = Get-Content -LiteralPath $tempJson2 -Raw | ConvertFrom-Json
    if ($json2.disposition -eq 'PASS' -and -not $json2.environment.elevated) {
        throw "Unprivileged install request must not return PASS disposition"
    }
    Write-Host "    PASS (Fail-closed on invalid install request)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson2) { Remove-Item -LiteralPath $tempJson2 -Force -ErrorAction SilentlyContinue }
}

Write-Host "All Test-CleanHostCertification tests PASSED!" -ForegroundColor Green
