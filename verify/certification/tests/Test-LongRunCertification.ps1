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

# Test 4: UIA rapid-input semantic assertions present (no PASS without real execution proof)
Write-Host "  Test 4: UIA rapid-input runner asserts pane/filter/close postconditions..."
$uiaRunner = Join-Path $RepoRoot 'verify\certification\Invoke-UiaLongRunCertification.ps1'
if (-not (Test-Path -LiteralPath $uiaRunner)) { throw "UIA runner not found at $uiaRunner" }
$uiaText = Get-Content -LiteralPath $uiaRunner -Raw
# Dual-pane toggle must assert both directions with explicit pane identities.
foreach ($required in @('Pane.1', 'pane count=2', 'Pane.1 absent', 'pane count=1', 'Pane.0 present')) {
    if ($uiaText -notmatch [regex]::Escape($required)) { throw "UIA runner missing dual-pane semantic assertion: $required" }
}
# Search/Filters must assert open, disclosure, contents, and verified close.
foreach ($required in @('Search.Results', "'Filters' disclosure", "'Clear history'", "'Ascending'", 'close not verified', 'Search.Results absent')) {
    if ($uiaText -notmatch [regex]::Escape($required)) { throw "UIA runner missing search/filters semantic assertion: $required" }
}
# Palette close must be proved by disappearance, never by a sent Escape.
if ($uiaText -notmatch 'Wait-UiaElementGone') { throw "UIA runner must verify close via Wait-UiaElementGone" }
if ($uiaText -notmatch 'still present after Escape') { throw "UIA runner must fail the iteration when the palette is still present after Escape" }
$bareClosed = [regex]::Matches($uiaText, "\['closed'\]\s*=\s*'PASS'")
if ($bareClosed.Count -gt 0) { throw "Found $($bareClosed.Count) unconditional closed=PASS assignment(s) in UIA runner" }
Write-Host "    PASS (Pane.1/Filters-disclosure/verified-close assertions present; no bare closed=PASS)" -ForegroundColor Green

# Test 5: UIA artifact carries ledger, phases, assertions, cleanup, and orphan census
Write-Host "  Test 5: UIA result contract carries execution-proof artifact fields..."
foreach ($required in @('iterationLedger', 'phaseDurations', 'assertionCounts', 'semanticPostconditions', 'cleanup', 'orphanPids', 'orphanCount', 'sandboxRemoved', 'totalIterations')) {
    $hit = ($uiaText -match [regex]::Escape($required)) -or ((Join-Path $RepoRoot 'verify\certification\Invoke-ResourceSoak.ps1') | ForEach-Object { (Get-Content -LiteralPath $_ -Raw) -match [regex]::Escape($required) })
    if (-not $hit) { throw "Neither runner emits artifact field: $required" }
}
foreach ($required in @('iterationLedger', 'phaseDurations', 'assertionCounts', 'semanticPostconditions', 'cleanup = $cleanupResult')) {
    if ($uiaText -notmatch [regex]::Escape($required)) { throw "UIA runner result missing artifact field: $required" }
}
# Cleanup failure must force FAIL, never hide behind iteration counts.
if ($uiaText -notmatch 'Cleanup gate') { throw "UIA runner missing cleanup gate forcing FAIL on cleanup failure" }
if ($uiaText -notmatch '\$cleanupResult\.pass -ne \$true') { throw "UIA runner cleanup gate must key off the cleanup census" }
Write-Host "    PASS (Ledger, phases, assertion counts, postconditions, cleanup, and FAIL-gate present)" -ForegroundColor Green

# Test 6: Live UIA rapid-input run proves execution or reports BLOCKED_ENVIRONMENT with cleanup
Write-Host "  Test 6: Live rapid-input run (1 iteration) yields proof fields or clean BLOCKED_ENVIRONMENT..."
$test6Json = [IO.Path]::GetTempFileName()
try {
    $null = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $uiaRunner `
        -Scenario rapid-input `
        -Iterations 1 `
        -Preset debug `
        -OutputPath $test6Json `
        -RepoRoot $RepoRoot

    if (-not (Test-Path -LiteralPath $test6Json)) { throw "UIA runner produced no output JSON" }
    $json6 = Get-Content -LiteralPath $test6Json -Raw | ConvertFrom-Json
    if ($null -eq $json6.cleanup) { throw "UIA result JSON must include a cleanup object in every mode" }
    if ($json6.disposition -eq 'BLOCKED_ENVIRONMENT') {
        Write-Host "    PASS (Non-interactive host cleanly classified BLOCKED_ENVIRONMENT with cleanup object)" -ForegroundColor Green
    } else {
        if ($json6.totalIterations -ne 1) { throw "Expected totalIterations=1, got $($json6.totalIterations)" }
        if ($null -eq $json6.iterationLedger -or $json6.iterationLedger.Count -lt 1) { throw "Expected a per-iteration ledger entry" }
        if ($json6.assertionCounts.total -le 0) { throw "Expected counted semantic assertions, got $($json6.assertionCounts.total)" }
        if ($json6.completedIterations -lt 1) { throw "Expected at least 1 completed iteration" }
        Write-Host "    PASS (Live run: $($json6.passed) passed / $($json6.failed) failed, $($json6.assertionCounts.total) assertions, cleanup=$($json6.cleanup.status))" -ForegroundColor Green
    }
} finally {
    if (Test-Path -LiteralPath $test6Json) { Remove-Item -LiteralPath $test6Json -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$test6Json.sha256") { Remove-Item -LiteralPath "$test6Json.sha256" -Force -ErrorAction SilentlyContinue }
}


# Test 7: visual-matrix scenario proves details-pane and storage-parent-percent
Write-Host "  Test 7: visual-matrix asserts reservation dynamics and treemap percent ground truth..."
$uiaModule = Join-Path $RepoRoot 'verify\capabilities\windows-ui-automation-validation\UiAutomationValidation.psm1'
$modText = Get-Content -LiteralPath $uiaModule -Raw
if ($modText -notmatch 'function Invoke-UiaVisualMatrix') { throw "UiAutomationValidation.psm1 must define Invoke-UiaVisualMatrix" }
# Treemap percent ground truth: FullDescription reads, top-level sum, exact values, drill-down.
foreach ($required in @('% of parent:', 'percent-sum-top', 'percent-values-top', 'percent-sum-drilled', 'drill-scope', 'Get-UiaFullDescriptionRaw')) {
    if ($modText -notmatch [regex]::Escape($required)) { throw "visual-matrix missing treemap ground-truth element: $required" }
}
# Details-pane reservation: dispatch + monotonic release + exact restore (+ wide magnitude).
foreach ($required in @('details-toggle-dispatch', 'details-monotonic', 'details-restore', 'details-fullcard-wide', 'view.toggle-details-pane')) {
    if ($modText -notmatch [regex]::Escape($required)) { throw "visual-matrix missing details-pane assertion: $required" }
}
# Runner wiring: scenario accepted, dispatched, and capability-advertised.
foreach ($required in @("'visual-matrix'", 'Invoke-UiaVisualMatrix -Exe', "'details-pane'", "'storage-parent-percent'")) {
    if ($uiaText -notmatch [regex]::Escape($required)) { throw "UIA runner missing visual-matrix wiring: $required" }
}
Write-Host "    PASS (visual-matrix function, percent/drill/details assertions, and runner wiring present)" -ForegroundColor Green

# Test 8: Live visual-matrix run proves execution or reports clean BLOCKED_ENVIRONMENT
Write-Host "  Test 8: Live visual-matrix run (1 iteration) yields proof fields or clean BLOCKED_ENVIRONMENT..."
$test8Json = [IO.Path]::GetTempFileName()
try {
    $null = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $uiaRunner `
        -Scenario visual-matrix `
        -Iterations 1 `
        -Preset debug `
        -OutputPath $test8Json `
        -RepoRoot $RepoRoot
    if (-not (Test-Path -LiteralPath $test8Json)) { throw "UIA runner produced no output JSON" }
    $json8 = Get-Content -LiteralPath $test8Json -Raw | ConvertFrom-Json
    if ($null -eq $json8.cleanup) { throw "UIA result JSON must include a cleanup object in every mode" }
    if ($null -eq $json8.displayMatrix) { throw "UIA result JSON must include the display matrix" }
    if ($json8.disposition -eq 'BLOCKED_ENVIRONMENT') {
        if ($json8.capabilities -contains 'storage-parent-percent') { throw "BLOCKED run must not advertise storage-parent-percent" }
        if ($json8.capabilities -contains 'details-pane') { throw "BLOCKED run with zero passed iterations must not advertise details-pane" }
        Write-Host "    PASS (Host cleanly classified BLOCKED_ENVIRONMENT with cleanup and matrix; unproven caps withheld)" -ForegroundColor Green
    } else {
        if ($json8.capabilities -notcontains 'storage-parent-percent') { throw "visual-matrix PASS must advertise storage-parent-percent, got: $($json8.capabilities -join ',')" }
        if ($json8.capabilities -notcontains 'details-pane') { throw "visual-matrix PASS must advertise details-pane, got: $($json8.capabilities -join ',')" }
        if ($null -eq $json8.iterationLedger -or $json8.iterationLedger.Count -lt 1) { throw "Expected a per-iteration ledger entry" }
        Write-Host "    PASS (Live run: $($json8.passed) passed / $($json8.failed) failed, caps=$($json8.capabilities -join ','))" -ForegroundColor Green
    }
} finally {
    if (Test-Path -LiteralPath $test8Json) { Remove-Item -LiteralPath $test8Json -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$test8Json.sha256") { Remove-Item -LiteralPath "$test8Json.sha256" -Force -ErrorAction SilentlyContinue }
}

Write-Host "All Test-LongRunCertification tests PASSED!" -ForegroundColor Green
