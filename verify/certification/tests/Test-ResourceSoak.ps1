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
$runnerSoakText = Get-Content -LiteralPath $runner -Raw
foreach ($required in @('$handleSettled', '$handleHalfDrift', '$handleFastOk', 'second-half drift')) {
    if ($runnerSoakText -notmatch [regex]::Escape($required)) { throw "Runner missing settled-step handle gate element: $required" }
}
$checkStability = {
    param([double[]] $samples, [double] $duration)
    $halfCount = [Math]::Max(1, [int]($samples.Count / 2))
    $firstHalf = @($samples[0..($halfCount - 1)] | Sort-Object)
    $secondHalf = @($samples[($samples.Count - $halfCount)..($samples.Count - 1)] | Sort-Object)
    $firstMed = $firstHalf[[Math]::Floor(($firstHalf.Count - 1) * 0.50)]
    $secondMed = $secondHalf[[Math]::Floor(($secondHalf.Count - 1) * 0.50)]
    $baseline = $samples[0]
    $final = $samples[$samples.Count - 1]
    $slope = [Math]::Round(($final - $baseline) / $duration, 4)
    $handleGrowth = $final - $baseline
    $handleHalfDrift = $secondMed - $firstMed
    $handleFastOk = ($handleGrowth -le 150 -and $slope -le 0.5)
    $handleSettled = ($handleHalfDrift -le 25)
    return ($handleFastOk -or $handleSettled)
}
$runawaySamples = [double[]]@(100.0, 150.0, 250.0, 400.0, 600.0) # Handle runaway (+500 handles over 10s)
$isStable = & $checkStability -samples $runawaySamples -duration 10.0
if ($isStable) {
    throw "Expected runaway handle profile to fail stability check, but it passed!"
}
# NV-011: one-time first-open cache step (+536) then flat across 45
# open/close cycles must settle to PASS, not trip the slope gate.
$stepFlat = [double[]]@(1180.0, 1180.0, 1756.0, 1758.0, 1756.0, 1758.0, 1758.0, 1756.0, 1758.0, 1756.0)
$stepStable = & $checkStability -samples $stepFlat -duration 600.0
if (-not $stepStable) {
    throw "Expected bounded step-then-flat handle profile to settle PASS, but it failed!"
}
# Late-onset growth after a flat first half must still fail.
$lateOnset = [double[]]@(500.0, 501.0, 500.0, 502.0, 501.0, 560.0, 620.0, 680.0, 740.0, 800.0)
$lateStable = & $checkStability -samples $lateOnset -duration 600.0
if ($lateStable) {
    throw "Expected late-onset handle growth to fail stability check, but it passed!"
}
Write-Host "    PASS (Runaway fails closed; bounded step settles PASS; late-onset fails)" -ForegroundColor Green

# Test 5: Separate Private-Bytes Gate (stable working set + leaking private bytes => FAIL)
Write-Host "  Test 5: Private-bytes-only leak fails the separate gate (no working-set reuse)..."
$runnerText = Get-Content -LiteralPath $runner -Raw
# The gate must exist as its own verdict with all three documented signals.
foreach ($required in @('$privateBytesStable', '$pbSlopeOk', '$pbGrowthOk', '$pbDriftOk', '$pbHalfDrift', 'private-bytes instability')) {
    if ($runnerText -notmatch [regex]::Escape($required)) { throw "Runner missing separate private-bytes gate element: $required" }
}
# The working-set verdict must not decide private bytes: the PB row and the
# composed verdict must reference the separate gate, not the WS verdict.
if ($runnerText -notmatch '\$wsStable -and \$privateBytesStable') { throw "resourceStabilityPass must compose the separate private-bytes verdict alongside the working-set verdict" }
if ($runnerText -match '\|\s\*\*Private Bytes\*\*.*\$wsStable') { throw "Private Bytes markdown row must not reuse the working-set verdict" }
# Numeric simulation with the runner-documented thresholds (slope <= 150 KB/s,
# growth <= 50 MB, second-half drift <= 25 MB) over Compute-MetricStats shape.
$getSimStats = {
    param([double[]] $samples, [double] $duration)
    $sorted = @($samples | Sort-Object)
    $medianIdx = [Math]::Floor(($sorted.Count - 1) * 0.50)
    $median = $sorted[[int]$medianIdx]
    $halfCount = [Math]::Max(1, [int]($samples.Count / 2))
    $firstHalf = @($samples[0..($halfCount - 1)] | Sort-Object)
    $secondHalf = @($samples[($samples.Count - $halfCount)..($samples.Count - 1)] | Sort-Object)
    $firstMed = $firstHalf[[Math]::Floor(($firstHalf.Count - 1) * 0.50)]
    $secondMed = $secondHalf[[Math]::Floor(($secondHalf.Count - 1) * 0.50)]
    return [pscustomobject]@{
        baseline = $samples[0]
        final = $samples[$samples.Count - 1]
        slope = [Math]::Round(($samples[$samples.Count - 1] - $samples[0]) / $duration, 4)
        firstHalfMedian = $firstMed
        secondHalfMedian = $secondMed
    }
}
$wsFlat = [double[]]@(104857600, 104960000, 104752000, 105062400, 104857600, 104960000, 104752000, 105062400, 104857600, 104960000)
$wsStats = & $getSimStats -samples $wsFlat -duration 60.0
$wsVerdict = ($wsStats.slope -le 153600)
if (-not $wsVerdict) { throw "Simulation setup broken: flat working-set profile should pass the WS verdict (slope=$($wsStats.slope))" }
# Leaking private bytes: +20 MB over 60 s (slope ~349 KB/s > 150 KB/s ceiling).
$pbLeak = [double[]]@(83886080, 86222848, 88559616, 90896384, 93233152, 95569920, 97906688, 100243456, 102580224, 104857600)
$pbStats = & $getSimStats -samples $pbLeak -duration 60.0
$pbSlopeCheck = ($pbStats.slope -le 153600)
$pbGrowthCheck = (($pbStats.final - $pbStats.baseline) -le 52428800)
$pbDriftCheck = (($pbStats.secondHalfMedian - $pbStats.firstHalfMedian) -le 26214400)
$pbVerdict = ($pbSlopeCheck -and $pbGrowthCheck -and $pbDriftCheck)
if ($pbVerdict) { throw "Expected leaking private-bytes profile to fail the separate gate, but it passed (slope=$($pbStats.slope))" }
$composedStability = ($true -and $true -and $true -and $wsVerdict -and $pbVerdict)
if ($composedStability) { throw "Stable working set + leaking private bytes must compose to resourceStability FAIL" }
# Positive control: flat private bytes pass all three sub-conditions.
$pbFlat = [double[]]@(83886080, 83918848, 83853312, 83951616, 83886080, 83918848, 83853312, 83951616, 83886080, 83918848)
$pbFlatStats = & $getSimStats -samples $pbFlat -duration 60.0
$pbFlatVerdict = (($pbFlatStats.slope -le 153600) -and (($pbFlatStats.final - $pbFlatStats.baseline) -le 52428800) -and (($pbFlatStats.secondHalfMedian - $pbFlatStats.firstHalfMedian) -le 26214400))
if (-not $pbFlatVerdict) { throw "Flat private-bytes profile should pass the separate gate (slope=$($pbFlatStats.slope))" }
Write-Host "    PASS (WS verdict PASS + PB verdict FAIL composes to resourceStability FAIL; flat PB passes)" -ForegroundColor Green

# Test 6: Close-Without-Verification Is Rejected (no closed=PASS from a sent keystroke)
Write-Host "  Test 6: Every transition close is verified by disappearance/restoration..."
$unverifiedClosed = [regex]::Matches($runnerText, "\['closed'\]\s*=\s*'PASS'")
if ($unverifiedClosed.Count -gt 0) { throw "Found $($unverifiedClosed.Count) unconditional closed=PASS assignment(s); every close must be verified" }
foreach ($proof in @('Wait-UiaElementGone', 'close not verified', 'still present after', 'Search.Results absent', 'Pane.1 absence', 'single-pane restoration not verified', 'settings dialog close not verified')) {
    if ($runnerText -notmatch [regex]::Escape($proof)) { throw "Runner missing verified-close evidence: $proof" }
}
# Each of the four activity branches must gate success on its verified close.
foreach ($branch in @("'command-palette'", "'dual-pane'", "'search'", "'settings'")) {
    if ($runnerText -notmatch [regex]::Escape($branch)) { throw "Runner missing activity branch: $branch" }
}
if ($runnerText -notmatch '\$actSuccess = \(\$palOpened -and \$palGone\)') { throw "command-palette success must require both verified open and verified close" }
if ($runnerText -notmatch '\$actSuccess = \(\$dualCmdOk -and \$enabledOk -and \$restoredOk\)') { throw "dual-pane success must require command, enable postcondition, and verified restoration" }
if ($runnerText -notmatch '\$actSuccess = \(\$searchOpened -and \$filtersFound -and \$contentsOk -and \$closeOk\)') { throw "search success must require open, Filters disclosure, contents, and verified close" }
if ($runnerText -notmatch '\$actSuccess = \(\$settingsOpened -and \$settingsClosed\)') { throw "settings success must require verified open and verified close" }
Write-Host "    PASS (No unconditional closed=PASS; all four branches gate on verified disappearance/restoration)" -ForegroundColor Green

# Test 7: Cleanup Object Present and Clean on a Real Idle Run (mode explicit)
Write-Host "  Test 7: Idle run emits explicit mode plus clean cleanup census..."
$test7Json = [IO.Path]::GetTempFileName()
try {
    # NV-011: WarmupSeconds 2 (was 0) -- the runner's baseline phase is
    # defined as STEADY_STATE, so measuring it with zero warmup samples
    # launch-settling allocator noise instead (WS slope ~190 KB/s over a
    # 3-sample window vs ~8 KB/s after 2 s warmup on the same binary).
    # Duration 4 s keeps this a fast contract test; every assertion below
    # is unchanged.
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -DurationSeconds 4 `
        -SampleIntervalSeconds 1 `
        -WarmupSeconds 2 `
        -IdleOnly `
        -OutputPath $test7Json `
        -RepoRoot $RepoRoot

    if ($LASTEXITCODE -ne 0) { throw "Test 7 runner failed with exit code $LASTEXITCODE" }
    $json7 = Get-Content -LiteralPath $test7Json -Raw | ConvertFrom-Json
    if ($json7.mode -ne 'idle-only') { throw "Expected mode=idle-only, got $($json7.mode)" }
    if ($json7.stabilityLegs.functionalActivity -ne 'SKIPPED') { throw "Expected functionalActivity=SKIPPED in idle-only mode, got $($json7.stabilityLegs.functionalActivity)" }
    if ($null -eq $json7.cleanup) { throw "Result JSON must include a cleanup object" }
    if ($json7.cleanup.status -ne 'PASS') { throw "Expected cleanup.status=PASS, got $($json7.cleanup.status)" }
    if ($json7.cleanup.pass -ne $true) { throw "Expected cleanup.pass=true" }
    if ($json7.cleanup.sandboxRemoved -ne $true) { throw "Expected cleanup.sandboxRemoved=true" }
    if ($json7.cleanup.orphanCount -ne 0) { throw "Expected cleanup.orphanCount=0, got $($json7.cleanup.orphanCount)" }
    if ($json7.stabilityLegs.cleanup -ne 'PASS') { throw "Expected stability leg cleanup=PASS, got $($json7.stabilityLegs.cleanup)" }
    if ($json7.disposition -ne 'PASS') { throw "Expected disposition PASS for a clean idle run, got $($json7.disposition)" }
    # Schema still validates with the new mode/cleanup fields.
    $pyCmd7 = "import json, jsonschema; s = json.load(open(r'$schemaPath')); d = json.load(open(r'$test7Json')); jsonschema.validate(instance=d, schema=s); print('OK')"
    $val7 = & (Get-Command python).Source -c $pyCmd7 2>&1
    if ($val7 -notmatch 'OK') { throw "Schema validation failed with new fields: $val7" }
    Write-Host "    PASS (mode=idle-only, SKIPPED leg, cleanup census clean, schema valid)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $test7Json) { Remove-Item -LiteralPath $test7Json -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$test7Json.sha256") { Remove-Item -LiteralPath "$test7Json.sha256" -Force -ErrorAction SilentlyContinue }
}
Write-Host "All Test-ResourceSoak tests PASSED!" -ForegroundColor Green
