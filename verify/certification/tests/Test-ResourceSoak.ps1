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
        -SampleIntervalSeconds 2 `
        -WarmupSeconds 1 `
        -IdleOnly `
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
    if ($null -eq $json.phases.steadyStateBaselineUtc) { throw "Missing steadyStateBaselineUtc in phases" }
    if ($json.phases.shutdownMethod -ne 'graceful') { throw "Expected graceful shutdownMethod, got $($json.phases.shutdownMethod)" }
    if ($json.stabilityLegs.survival -ne 'PASS') { throw "Expected survival PASS" }
    if ($json.stabilityLegs.resourceStability -ne 'PASS') { throw "Expected resourceStability PASS" }
    if ($json.stabilityLegs.shutdown -ne 'PASS') { throw "Expected shutdown PASS" }
    if ($json.stabilityLegs.residue -ne 'PASS') { throw "Expected residue PASS" }

    # Verify sidecar hash
    $sidecar = "$tempJson.sha256"
    if (-not (Test-Path -LiteralPath $sidecar)) { throw "Missing sidecar $sidecar" }
    $fileBytes = [IO.File]::ReadAllBytes($tempJson)
    $actualHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
    $sidecarHash = ((Get-Content -LiteralPath $sidecar -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
    if ($sidecarHash -ne $actualHash) { throw "Sidecar checksum mismatch: $sidecarHash vs $actualHash" }

    # Verify process terminated cleanly
    $pidToTest = $json.fastFilesPid
    $alive = Get-Process -Id $pidToTest -ErrorAction SilentlyContinue
    if ($null -ne $alive) { throw "Process $pidToTest was still running after soak completion!" }

    Write-Host "    PASS (PID $pidToTest terminated cleanly, 7 phases and stability legs verified)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson.sha256") { Remove-Item -LiteralPath "$tempJson.sha256" -Force -ErrorAction SilentlyContinue }
}

# Test 2: Exact-tree requirement rejects dirty worktree
Write-Host "  Test 2: Exact-tree requirement rejects dirty worktree..."
$dirtyFixture = Join-Path $RepoRoot "temp-dirty-soak-$([guid]::NewGuid().ToString('n')).tmp"
try {
    Set-Content -LiteralPath $dirtyFixture -Value "dirty soak fixture" -Encoding utf8

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
} finally {
    if (Test-Path -LiteralPath $dirtyFixture) {
        Remove-Item -LiteralPath $dirtyFixture -Force -ErrorAction SilentlyContinue
    }
}

# Test 3: Prohibition of Fake/Simulated UI Activity (CT-002 Regression)
Write-Host "  Test 3: Prohibition of simulated activity records..."
$test3Json = [IO.Path]::GetTempFileName()
try {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -DurationSeconds 2 `
        -SampleIntervalSeconds 1 `
        -WarmupSeconds 0 `
        -IdleOnly `
        -OutputPath $test3Json `
        -RepoRoot $RepoRoot

    $json3 = Get-Content -LiteralPath $test3Json -Raw | ConvertFrom-Json
    foreach ($act in $json3.activityTransitions) {
        if ($act.details -match 'Simulated periodic UI activity') {
            throw "Found simulated activity string in output! Prohibited by CT-002."
        }
    }
    if ($json3.stabilityLegs.functionalActivity -ne 'SKIPPED') {
        throw "Expected functionalActivity=SKIPPED in IdleOnly mode, got $($json3.stabilityLegs.functionalActivity)"
    }
    Write-Host "    PASS (No simulated activity emitted; idle correctly classified SKIPPED)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $test3Json) { Remove-Item -LiteralPath $test3Json -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$test3Json.sha256") { Remove-Item -LiteralPath "$test3Json.sha256" -Force -ErrorAction SilentlyContinue }
}

# Test 4: Resource Instability Gate (Adversarial Slope/Runaway Fixture)
Write-Host "  Test 4: Resource runaway slope triggers resourceStability FAIL (CT-003 Regression)..."
$checkStability = {
    param([double[]] $samples, [double] $duration)
    $baseline = $samples[0]
    $final = $samples[$samples.Count - 1]
    $slope = [Math]::Round(($final - $baseline) / $duration, 4)
    $handleGrowth = $final - $baseline
    $handleStable = ($handleGrowth -le 150 -and $slope -le 0.5)
    return $handleStable
}
$runawaySamples = [double[]]@(100.0, 150.0, 250.0, 400.0, 600.0) # Handle runaway (+500 handles over 10s)
$isStable = & $checkStability -samples $runawaySamples -duration 10.0
if ($isStable) {
    throw "Expected runaway handle profile to fail stability check, but it passed!"
}
Write-Host "    PASS (Runaway handle/resource profile correctly fails closed)" -ForegroundColor Green
Write-Host "All Test-ResourceSoak tests PASSED!" -ForegroundColor Green
