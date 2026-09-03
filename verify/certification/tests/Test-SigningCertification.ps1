[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-SigningCertification.ps1..." -ForegroundColor Cyan

$runner = Join-Path $RepoRoot 'verify\certification\Invoke-SigningCertification.ps1'
$schemaPath = Join-Path $RepoRoot 'verify\certification\schemas\certification-result.schema.json'

if (-not (Test-Path -LiteralPath $runner)) { throw "Runner not found at $runner" }
if (-not (Test-Path -LiteralPath $schemaPath)) { throw "Schema not found at $schemaPath" }

# Test 1: Missing candidate returns REQUIRED-BUT-UNAVAILABLE and valid schema JSON
Write-Host "  Test 1: Missing candidate returns REQUIRED-BUT-UNAVAILABLE..."
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
    if ($json.lane -ne 'production-signing') { throw "Expected lane production-signing, got $($json.lane)" }
    if ($json.status -ne 'REQUIRED-BUT-UNAVAILABLE') { throw "Expected REQUIRED-BUT-UNAVAILABLE, got $($json.status)" }
    if ($json.disposition -ne 'BLOCKED_EXTERNAL') { throw "Expected disposition BLOCKED_EXTERNAL, got $($json.disposition)" }

    Write-Host "    PASS (Correctly classified REQUIRED-BUT-UNAVAILABLE / BLOCKED_EXTERNAL)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson -Force -ErrorAction SilentlyContinue }
}

# Test 2: Unsigned candidate with expected thumbprint fails Authenticode signature check
Write-Host "  Test 2: Unsigned binary fails Authenticode validation..."
$unsignedTarget = Join-Path $RepoRoot "build\debug\src\ui\FastFiles.exe"
if (Test-Path -LiteralPath $unsignedTarget) {
    $tempJson2 = [IO.Path]::GetTempFileName()
    try {
        $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
            -CandidatePath $unsignedTarget `
            -SignerThumbprint "A1B2C3D4E5F6A1B2C3D4E5F6A1B2C3D4E5F6A1B2" `
            -OutputPath $tempJson2 `
            -RepoRoot $RepoRoot

        $json2 = Get-Content -LiteralPath $tempJson2 -Raw | ConvertFrom-Json
        if ($json2.status -ne 'FAIL') {
            throw "Expected unsigned binary to fail signing certification, got $($json2.status)"
        }
        if ($json2.disposition -ne 'FAIL') {
            throw "Expected disposition FAIL, got $($json2.disposition)"
        }
        Write-Host "    PASS (Correctly rejected unsigned candidate as FAIL)" -ForegroundColor Green
    } finally {
        if (Test-Path -LiteralPath $tempJson2) { Remove-Item -LiteralPath $tempJson2 -Force -ErrorAction SilentlyContinue }
    }
}

Write-Host "All Test-SigningCertification tests PASSED!" -ForegroundColor Green
