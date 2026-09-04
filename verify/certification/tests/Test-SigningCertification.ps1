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

function Assert-ResultSchema {
    param([string] $JsonPath)
    $pyCmd = "import json, jsonschema; s = json.load(open(r'$schemaPath')); d = json.load(open(r'$JsonPath')); jsonschema.validate(instance=d, schema=s); print('OK')"
    $val = & (Get-Command python).Source -c $pyCmd 2>&1
    if ($val -notmatch 'OK') { throw "Schema validation failed: $val" }
}

function Get-Check {
    param($Json, [string] $Id)
    return @($Json.checks | Where-Object { $_.checkId -eq $Id }) | Select-Object -First 1
}

# Test 1: Missing candidate returns REQUIRED-BUT-UNAVAILABLE and valid schema JSON (fail-closed).
Write-Host "  Test 1: Missing candidate returns REQUIRED-BUT-UNAVAILABLE..."
$tempJson = [IO.Path]::GetTempFileName()
try {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -OutputPath $tempJson `
        -RepoRoot $RepoRoot

    if ($LASTEXITCODE -ne 0) { throw "Runner failed with exit code $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $tempJson)) { throw "Output JSON was not created" }

    Assert-ResultSchema -JsonPath $tempJson

    $json = Get-Content -LiteralPath $tempJson -Raw | ConvertFrom-Json
    if ($json.lane -ne 'production-signing') { throw "Expected lane production-signing, got $($json.lane)" }
    if ($json.status -ne 'REQUIRED-BUT-UNAVAILABLE') { throw "Expected REQUIRED-BUT-UNAVAILABLE, got $($json.status)" }
    if ($json.disposition -ne 'BLOCKED_EXTERNAL') { throw "Expected disposition BLOCKED_EXTERNAL, got $($json.disposition)" }
    if ($null -eq $json.cleanup) { throw "Result JSON MUST include a cleanup object" }
    if ($json.cleanup.status -ne 'PASS') { throw "Audit-only run must report clean cleanup, got $($json.cleanup.status)" }
    if ($json.capabilities -contains 'production-signing') { throw "production-signing must NOT be claimed without a verifying candidate" }
    $ident = Get-Check -Json $json -Id 'candidate-identity'
    if ($ident.status -ne 'REQUIRED-BUT-UNAVAILABLE') { throw "candidate-identity must be REQUIRED-BUT-UNAVAILABLE, got $($ident.status)" }

    Write-Host "    PASS (Correctly classified REQUIRED-BUT-UNAVAILABLE / BLOCKED_EXTERNAL)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson.sha256") { Remove-Item -LiteralPath "$tempJson.sha256" -Force -ErrorAction SilentlyContinue }
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

        Assert-ResultSchema -JsonPath $tempJson2
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
        if (Test-Path -LiteralPath "$tempJson2.sha256") { Remove-Item -LiteralPath "$tempJson2.sha256" -Force -ErrorAction SilentlyContinue }
    }
} else {
    Write-Host "    SKIPPED (Unsigned test candidate not found at $unsignedTarget; covered synthetically in Test 3)" -ForegroundColor Yellow
}

# Test 3: Synthetic unsigned artifact is recognized and rejected; wrong pin => FAIL.
# Nothing is signed; the fixture hash must be identical before and after the run.
Write-Host "  Test 3: Synthetic unsigned artifact rejected; wrong pin fails..."
$workDir = Join-Path ([IO.Path]::GetTempPath()) ("ff-signing-test-" + [Guid]::NewGuid().ToString("N"))
$tempJson3 = [IO.Path]::GetTempFileName()
try {
    New-Item -ItemType Directory -Path $workDir -Force | Out-Null
    $unsignedFx = Join-Path $workDir "unsigned-fixture.ps1"
    Set-Content -LiteralPath $unsignedFx -Value "Write-Output 'FastFiles synthetic unsigned fixture'" -Encoding ascii -NoNewline
    $preBytes = [IO.File]::ReadAllBytes($unsignedFx)
    $preHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($preBytes)).Replace('-', '').ToLowerInvariant()

    $sigFx = Get-AuthenticodeSignature -FilePath $unsignedFx -ErrorAction SilentlyContinue
    if ($sigFx.Status -ne 'NotSigned') { throw "Synthetic fixture must be recognized as NotSigned, got $($sigFx.Status)" }

    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -CandidatePath $unsignedFx `
        -SignerThumbprint "A1B2C3D4E5F6A1B2C3D4E5F6A1B2C3D4E5F6A1B2" `
        -ExpectedSubject "Contoso Production Signing" `
        -OutputPath $tempJson3 `
        -RepoRoot $RepoRoot

    Assert-ResultSchema -JsonPath $tempJson3
    $json3 = Get-Content -LiteralPath $tempJson3 -Raw | ConvertFrom-Json
    if ($json3.status -ne 'FAIL') { throw "Unsigned artifact with wrong pin must yield FAIL, got $($json3.status)" }
    if ($json3.disposition -ne 'FAIL') { throw "Expected disposition FAIL, got $($json3.disposition)" }
    $ident3 = Get-Check -Json $json3 -Id 'candidate-identity'
    if ($ident3.status -ne 'PASS') { throw "candidate-identity must be PASS for a supplied file, got $($ident3.status)" }
    if ($json3.candidateHash -ne $preHash) { throw "candidateHash mismatch: expected $preHash, got $($json3.candidateHash)" }
    $auth3 = Get-Check -Json $json3 -Id 'authenticode-signature'
    if ($auth3.status -ne 'FAIL') { throw "authenticode-signature must be FAIL for unsigned input, got $($auth3.status)" }
    $pin3 = Get-Check -Json $json3 -Id 'signer-pin-match'
    if ($pin3.status -ne 'FAIL') { throw "Negative pin test: wrong pin must yield FAIL, got $($pin3.status)" }
    $subj3 = Get-Check -Json $json3 -Id 'signer-subject-match'
    if ($subj3.status -ne 'FAIL') { throw "signer-subject-match must be FAIL for unsigned input with expected subject, got $($subj3.status)" }
    if ($json3.capabilities -contains 'production-signing') { throw "production-signing must NOT be claimed for an unsigned candidate" }
    if ($null -eq $json3.cleanup -or $json3.cleanup.status -ne 'PASS') { throw "Expected clean cleanup object" }
    $postBytes = [IO.File]::ReadAllBytes($unsignedFx)
    $postHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($postBytes)).Replace('-', '').ToLowerInvariant()
    if ($postHash -ne $preHash) { throw "Runner must not modify the candidate (audit-only violated)" }

    Write-Host "    PASS (Unsigned artifact rejected; wrong pin FAILs; fixture untouched)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $workDir) { Remove-Item -LiteralPath $workDir -Force -Recurse -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $tempJson3) { Remove-Item -LiteralPath $tempJson3 -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson3.sha256") { Remove-Item -LiteralPath "$tempJson3.sha256" -Force -ErrorAction SilentlyContinue }
}

Write-Host "All Test-SigningCertification tests PASSED!" -ForegroundColor Green
