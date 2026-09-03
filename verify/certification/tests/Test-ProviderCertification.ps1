[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-ProviderCertification.ps1..." -ForegroundColor Cyan

$runner = Join-Path $RepoRoot 'verify\certification\Invoke-ProviderCertification.ps1'
$schemaPath = Join-Path $RepoRoot 'verify\certification\schemas\certification-result.schema.json'

if (-not (Test-Path -LiteralPath $runner)) { throw "Runner not found at $runner" }
if (-not (Test-Path -LiteralPath $schemaPath)) { throw "Schema not found at $schemaPath" }

# Test 1: Unconfigured provider returns REQUIRED-BUT-UNAVAILABLE and valid schema JSON
Write-Host "  Test 1: Unconfigured provider returns REQUIRED-BUT-UNAVAILABLE..."
$tempJson = [IO.Path]::GetTempFileName()
try {
    # Ensure env vars are temporarily unset for deterministic testing
    $oldKey = $env:FASTFILES_VISION_PROVIDER_KEY
    $oldEndpoint = $env:FASTFILES_VISION_PROVIDER_ENDPOINT
    $env:FASTFILES_VISION_PROVIDER_KEY = $null
    $env:FASTFILES_VISION_PROVIDER_ENDPOINT = $null

    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -Provider real `
        -OutputPath $tempJson `
        -RepoRoot $RepoRoot

    if ($LASTEXITCODE -ne 0) { throw "Runner failed with exit code $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $tempJson)) { throw "Output JSON was not created" }

    $pyCmd = "import json, jsonschema; s = json.load(open(r'$schemaPath')); d = json.load(open(r'$tempJson')); jsonschema.validate(instance=d, schema=s); print('OK')"
    $val = & (Get-Command python).Source -c $pyCmd 2>&1
    if ($val -notmatch 'OK') { throw "Schema validation failed: $val" }

    $json = Get-Content -LiteralPath $tempJson -Raw | ConvertFrom-Json
    if ($json.lane -ne 'external-provider') { throw "Expected lane external-provider, got $($json.lane)" }
    if ($json.status -ne 'REQUIRED-BUT-UNAVAILABLE') { throw "Expected REQUIRED-BUT-UNAVAILABLE, got $($json.status)" }
    if ($json.disposition -ne 'BLOCKED_EXTERNAL') { throw "Expected disposition BLOCKED_EXTERNAL, got $($json.disposition)" }

    Write-Host "    PASS (Correctly classified REQUIRED-BUT-UNAVAILABLE / BLOCKED_EXTERNAL)" -ForegroundColor Green
} finally {
    $env:FASTFILES_VISION_PROVIDER_KEY = $oldKey
    $env:FASTFILES_VISION_PROVIDER_ENDPOINT = $oldEndpoint
    if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson -Force -ErrorAction SilentlyContinue }
}

# Test 2: Fake provider invocation fails closed (cannot claim certification)
Write-Host "  Test 2: Fake provider invocation fails closed..."
$tempJson2 = [IO.Path]::GetTempFileName()
try {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -Provider fake `
        -OutputPath $tempJson2 `
        -RepoRoot $RepoRoot

    $json2 = Get-Content -LiteralPath $tempJson2 -Raw | ConvertFrom-Json
    if ($json2.status -ne 'FAIL') {
        throw "Expected fake provider to fail certification with status FAIL, got $($json2.status)"
    }
    if ($json2.disposition -ne 'FAIL') {
        throw "Expected disposition FAIL, got $($json2.disposition)"
    }
    Write-Host "    PASS (FakeCritic correctly rejected with status FAIL)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson2) { Remove-Item -LiteralPath $tempJson2 -Force -ErrorAction SilentlyContinue }
}

Write-Host "All Test-ProviderCertification tests PASSED!" -ForegroundColor Green
