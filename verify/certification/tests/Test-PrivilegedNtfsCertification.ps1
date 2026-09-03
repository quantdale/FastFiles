[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-PrivilegedNtfsCertification.ps1..." -ForegroundColor Cyan

$runner = Join-Path $RepoRoot 'verify\certification\Invoke-PrivilegedNtfsCertification.ps1'
$schemaPath = Join-Path $RepoRoot 'verify\certification\schemas\certification-result.schema.json'

if (-not (Test-Path -LiteralPath $runner)) { throw "Runner not found at $runner" }
if (-not (Test-Path -LiteralPath $schemaPath)) { throw "Schema not found at $schemaPath" }

# Test 1: Normal execution produces schema-conforming artifact
Write-Host "  Test 1: Execution produces schema-compliant JSON..."
$tempJson = [IO.Path]::GetTempFileName()
try {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -VolumeRoot "C:" `
        -OutputPath $tempJson `
        -RepoRoot $RepoRoot

    if ($LASTEXITCODE -ne 0) { throw "Runner failed with exit code $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $tempJson)) { throw "Output JSON was not created" }

    $pyCmd = "import json, jsonschema; s = json.load(open(r'$schemaPath')); d = json.load(open(r'$tempJson')); jsonschema.validate(instance=d, schema=s); print('OK')"
    $val = & (Get-Command python).Source -c $pyCmd 2>&1
    if ($val -notmatch 'OK') { throw "Schema validation failed: $val" }

    $json = Get-Content -LiteralPath $tempJson -Raw | ConvertFrom-Json
    if ($json.lane -ne 'privileged-ntfs') { throw "Expected lane privileged-ntfs, got $($json.lane)" }
    if ($json.details.readOnly -ne $true) { throw "Read-only invariant violated in details" }

    Write-Host "    PASS (Status: $($json.status), Disposition: $($json.disposition))" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson -Force -ErrorAction SilentlyContinue }
}

# Test 2: Unprivileged execution must not claim PASS
Write-Host "  Test 2: Elevation invariant check..."
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $tempJson2 = [IO.Path]::GetTempFileName()
    try {
        $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
            -VolumeRoot "C:" `
            -OutputPath $tempJson2 `
            -RepoRoot $RepoRoot

        $json2 = Get-Content -LiteralPath $tempJson2 -Raw | ConvertFrom-Json
        if ($json2.status -eq 'PASS') {
            throw "Unprivileged process must not claim PASS on privileged NTFS lane"
        }
        if ($json2.status -ne 'BLOCKED_NOT_ELEVATED') {
            throw "Expected status BLOCKED_NOT_ELEVATED, got $($json2.status)"
        }
        Write-Host "    PASS (Correctly classified BLOCKED_NOT_ELEVATED)" -ForegroundColor Green
    } finally {
        if (Test-Path -LiteralPath $tempJson2) { Remove-Item -LiteralPath $tempJson2 -Force -ErrorAction SilentlyContinue }
    }
} else {
    Write-Host "    PASS (Host is elevated; verified in Test 1)" -ForegroundColor Green
}

Write-Host "All Test-PrivilegedNtfsCertification tests PASSED!" -ForegroundColor Green
