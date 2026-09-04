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
    if ($json.lane -ne 'vision-provider' -and $json.lane -ne 'external-provider') { throw "Expected lane vision-provider or external-provider, got $($json.lane)" }
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

# Environment provider certification helpers (campaign-owned temp dirs only).
$envRunner = Join-Path $RepoRoot 'verify\certification\Invoke-EnvironmentProviderCertification.ps1'
$fixtureModule = Join-Path $RepoRoot 'verify\certification\tests\ProviderFixture.psm1'
if (-not (Test-Path -LiteralPath $envRunner)) { throw "Environment runner not found at $envRunner" }
if (-not (Test-Path -LiteralPath $fixtureModule)) { throw "Fixture module not found at $fixtureModule" }
Import-Module $fixtureModule -Force

$script:expectedPhases = @(
    'DISCOVER', 'PREFLIGHT', 'PROVISION', 'ACTIVATE', 'WAIT_READY',
    'ESTABLISH_SECURE_GUEST_CHANNEL', 'TRANSFER_PAYLOAD', 'EXECUTE_PAYLOAD',
    'COLLECT_LOGS', 'COLLECT_STRUCTURED_RESULTS', 'VERIFY_RESULT_PROVENANCE',
    'RESTORE_ROLLBACK', 'CLEANUP', 'RESIDUE_CHECK'
)

function New-EnvCertWorkDir {
    $dir = Join-Path ([IO.Path]::GetTempPath()) ('ffprovcrt-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    return $dir
}

function Invoke-EnvCertRun {
    param([hashtable] $RunnerParams)
    $outPath = Join-Path $RunnerParams['WorkDir'] 'result.json'
    $invokeArgs = @{
        OutputPath = $outPath
        RepoRoot = $RepoRoot
    }
    foreach ($key in $RunnerParams.Keys) {
        if ($key -eq 'WorkDir') { continue }
        $invokeArgs[$key] = $RunnerParams[$key]
    }
    & $envRunner @invokeArgs | Out-Null
    if (-not (Test-Path -LiteralPath $outPath)) { throw "Runner did not write $outPath" }
    if (-not (Test-Path -LiteralPath "$outPath.sha256")) { throw "Runner did not write sidecar $outPath.sha256" }
    $parsed = Get-Content -LiteralPath $outPath -Raw | ConvertFrom-Json -Depth 20
    return $parsed
}

function Get-EnvCertPhase {
    param($ResultJson, [string] $Phase)
    $found = @($ResultJson.phases | Where-Object { $_.phase -eq $Phase })
    if ($found.Count -eq 0) { throw "Phase $Phase missing from result" }
    return $found[0]
}

function Assert-EnvCertLifecycleShape {
    param($ResultJson, [string] $Label)
    if ($ResultJson.lane -ne 'environment-provider') { throw "$Label : expected lane environment-provider, got $($ResultJson.lane)" }
    if ($null -eq $ResultJson.phases -or $ResultJson.phases.Count -ne 14) {
        throw "$Label : expected exactly 14 lifecycle phases, got $(@($ResultJson.phases).Count)"
    }
    for ($i = 0; $i -lt $script:expectedPhases.Count; $i++) {
        if ($ResultJson.phases[$i].phase -ne $script:expectedPhases[$i]) {
            throw "$Label : phase $i expected $($script:expectedPhases[$i]), got $($ResultJson.phases[$i].phase)"
        }
    }
    foreach ($record in $ResultJson.phases) {
        foreach ($field in @('phase', 'status', 'startUtc', 'endUtc', 'provider', 'artifacts')) {
            if ($null -eq $record.$field) { throw "$Label : phase $($record.phase) missing field '$field'" }
        }
        if (@('PASS', 'FAIL', 'SKIPPED', 'BLOCKED') -notcontains $record.status) {
            throw "$Label : phase $($record.phase) has unexpected status '$($record.status)'"
        }
    }
    if ($null -eq $ResultJson.cleanup) { throw "$Label : missing top-level cleanup object" }
    if ($null -eq $ResultJson.cleanup.succeeded) { throw "$Label : cleanup object missing 'succeeded'" }
    if ($ResultJson.cleanup.status -ne 'PASS' -and $ResultJson.cleanup.status -ne 'FAIL') {
        throw "$Label : cleanup.status must be PASS/FAIL, got '$($ResultJson.cleanup.status)'"
    }
    if ($null -eq $ResultJson.capabilities) { throw "$Label : missing top-level capabilities array" }
    if ($null -eq $ResultJson.provenance) { throw "$Label : missing top-level provenance record" }
    foreach ($field in @('schemaVersion', 'status', 'commit', 'tree', 'environment', 'startUtc', 'endUtc', 'durationMs', 'checks', 'disposition')) {
        if ($null -eq $ResultJson.$field) { throw "$Label : missing required field '$field'" }
    }
}

# Test 3: Real Hyper-V path without a guest image is BLOCKED, never PASS.
# Preflight-only success must not yield execution credit.
Write-Host "  Test 3: Real Hyper-V path without guest image is BLOCKED..."
$workDir3 = New-EnvCertWorkDir
try {
    $oldImage = $env:FASTFILES_HYPERV_IMAGE
    $env:FASTFILES_HYPERV_IMAGE = $null
    try {
        $json3 = Invoke-EnvCertRun -RunnerParams @{
            WorkDir = $workDir3
            Provider = 'hyperv'
            SourcePath = (Join-Path $workDir3 'absent-image.vhdx')
        }
    } finally {
        $env:FASTFILES_HYPERV_IMAGE = $oldImage
    }
    Assert-EnvCertLifecycleShape -ResultJson $json3 -Label 'real-hyperv-no-image'
    if ($json3.status -ne 'REQUIRED-BUT-UNAVAILABLE' -and $json3.status -ne 'BLOCKED_NOT_ELEVATED') {
        throw "Expected REQUIRED-BUT-UNAVAILABLE or BLOCKED_NOT_ELEVATED, got $($json3.status)"
    }
    if ($json3.disposition -ne 'BLOCKED_EXTERNAL') { throw "Expected disposition BLOCKED_EXTERNAL, got $($json3.disposition)" }
    $executePhase = Get-EnvCertPhase -ResultJson $json3 -Phase 'EXECUTE_PAYLOAD'
    if ($executePhase.status -eq 'PASS') { throw "EXECUTE_PAYLOAD must not pass without real guest execution" }
    foreach ($gated in @('PROVISION', 'ACTIVATE', 'WAIT_READY', 'ESTABLISH_SECURE_GUEST_CHANNEL', 'TRANSFER_PAYLOAD', 'EXECUTE_PAYLOAD')) {
        $phase = Get-EnvCertPhase -ResultJson $json3 -Phase $gated
        if ($phase.status -ne 'BLOCKED' -and $phase.status -ne 'SKIPPED') {
            throw "Phase $gated must be BLOCKED/SKIPPED without a guest image, got $($phase.status)"
        }
        if ([string]::IsNullOrWhiteSpace($phase.reason)) { throw "Phase $gated must record a reason" }
    }
    if ($json3.provenance.verified) { throw "Provenance must not verify without execution" }
    if ($json3.capabilities -contains 'guest-execution') { throw "guest-execution must not be claimed without execution" }
    if (-not $json3.cleanup.succeeded) { throw "Cleanup must be clean when nothing was provisioned" }
    Write-Host "    PASS (No image => $($json3.status) / BLOCKED_EXTERNAL, no execution credit)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $workDir3) { Remove-Item -LiteralPath $workDir3 -Recurse -Force -ErrorAction SilentlyContinue }
}

# Test 4: Local provider really executes the probe and earns PASS with proof.
Write-Host "  Test 4: Local provider executes with verified provenance..."
$workDir4 = New-EnvCertWorkDir
try {
    $json4 = Invoke-EnvCertRun -RunnerParams @{ WorkDir = $workDir4; Provider = 'local' }
    Assert-EnvCertLifecycleShape -ResultJson $json4 -Label 'real-local'
    if ($json4.status -ne 'PASS') { throw "Expected local PASS, got $($json4.status)" }
    if ($json4.disposition -ne 'PASS') { throw "Expected disposition PASS, got $($json4.disposition)" }
    $localExecute = Get-EnvCertPhase -ResultJson $json4 -Phase 'EXECUTE_PAYLOAD'
    if ($localExecute.status -ne 'PASS') { throw "Local EXECUTE_PAYLOAD must pass, got $($localExecute.status)" }
    if (-not $json4.provenance.verified) { throw "Local provenance must verify" }
    if (-not $json4.cleanup.succeeded) { throw "Local cleanup must be clean" }
    Write-Host "    PASS (Local execution proven: exit=$($json4.provenance.exitCode), payload bound)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $workDir4) { Remove-Item -LiteralPath $workDir4 -Recurse -Force -ErrorAction SilentlyContinue }
}

# Test 5: Every simulated condition maps to its terminal classification.
Write-Host "  Test 5: Fixture matrix covers all lifecycle conditions..."
$matrix = @(
    @{ Scenario = 'success'; Status = 'PASS'; Disposition = 'PASS'; FailPhase = $null },
    @{ Scenario = 'preflight-only'; Status = 'REQUIRED-BUT-UNAVAILABLE'; Disposition = 'BLOCKED_EXTERNAL'; FailPhase = $null },
    @{ Scenario = 'preflight-unavailable'; Status = 'REQUIRED-BUT-UNAVAILABLE'; Disposition = 'BLOCKED_EXTERNAL'; FailPhase = $null },
    @{ Scenario = 'provision-failure'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'PROVISION' },
    @{ Scenario = 'activation-failure'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'ACTIVATE' },
    @{ Scenario = 'channel-failure'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'ESTABLISH_SECURE_GUEST_CHANNEL' },
    @{ Scenario = 'transfer-failure'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'TRANSFER_PAYLOAD' },
    @{ Scenario = 'execution-failure'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'EXECUTE_PAYLOAD' },
    @{ Scenario = 'collection-failure'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'COLLECT_STRUCTURED_RESULTS' },
    @{ Scenario = 'malformed-result'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'VERIFY_RESULT_PROVENANCE' },
    @{ Scenario = 'provenance-mismatch'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'VERIFY_RESULT_PROVENANCE' },
    @{ Scenario = 'timeout'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'WAIT_READY' },
    @{ Scenario = 'rollback-failure'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'RESTORE_ROLLBACK' },
    @{ Scenario = 'cleanup-failure'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'CLEANUP' },
    @{ Scenario = 'cancellation'; Status = 'FAIL'; Disposition = 'FAIL'; FailPhase = 'EXECUTE_PAYLOAD' }
)
$matrixResults = @()
foreach ($case in $matrix) {
    $workDir = New-EnvCertWorkDir
    try {
        $adapter = New-ProviderFixtureAdapter -Scenario $case['Scenario']
        $resultJson = Invoke-EnvCertRun -RunnerParams @{
            WorkDir = $workDir
            Provider = 'hyperv'
            ProviderAdapter = $adapter
        }
        $label = "scenario $($case['Scenario'])"
        Assert-EnvCertLifecycleShape -ResultJson $resultJson -Label $label
        if ($resultJson.status -ne $case['Status']) {
            throw "$label : expected status $($case['Status']), got $($resultJson.status)"
        }
        if ($resultJson.disposition -ne $case['Disposition']) {
            throw "$label : expected disposition $($case['Disposition']), got $($resultJson.disposition)"
        }
        if ($null -ne $case['FailPhase']) {
            $failPhase = Get-EnvCertPhase -ResultJson $resultJson -Phase $case['FailPhase']
            if ($failPhase.status -ne 'FAIL') {
                throw "$label : expected phase $($case['FailPhase']) to FAIL, got $($failPhase.status)"
            }
        }
        if ($case['Scenario'] -eq 'success') {
            $expectedCaps = @('hyperv-provision', 'guest-execution', 'provider-rollback')
            foreach ($cap in $expectedCaps) {
                if ($resultJson.capabilities -notcontains $cap) { throw "success: missing capability '$cap'" }
            }
            if (-not $resultJson.provenance.verified) { throw "success: provenance must verify" }
            if (-not $resultJson.cleanup.succeeded) { throw "success: cleanup must be clean" }
            foreach ($record in $resultJson.phases) {
                if ($record.status -ne 'PASS') { throw "success: phase $($record.phase) must pass, got $($record.status)" }
            }
            if ($resultJson.details.cancelled) { throw "success: cancelled flag must be false" }
        } else {
            # Non-success runs may still carry honestly-earned capabilities
            # (e.g. rollback-failure executed with verified provenance but
            # failed later); Test 6 proves each capability implies its phase.
            if ($resultJson.status -eq 'PASS') { throw "$label : only the success fixture may PASS" }
        }
        if ($case['Scenario'] -eq 'preflight-only') {
            $preflight = Get-EnvCertPhase -ResultJson $resultJson -Phase 'PREFLIGHT'
            $execute = Get-EnvCertPhase -ResultJson $resultJson -Phase 'EXECUTE_PAYLOAD'
            if ($preflight.status -ne 'PASS') { throw "preflight-only: PREFLIGHT must pass, got $($preflight.status)" }
            if ($execute.status -eq 'PASS') { throw "preflight-only: EXECUTE must not pass (preflight != execution)" }
        }
        if ($case['Scenario'] -eq 'cleanup-failure') {
            if ($resultJson.cleanup.succeeded) { throw "cleanup-failure: cleanup.succeeded must be false (failure visible, never hidden)" }
            if ($null -eq $resultJson.failureReason) { throw "cleanup-failure: failureReason must name the cleanup failure" }
        }
        if ($case['Scenario'] -eq 'cancellation') {
            if (-not $resultJson.details.cancelled) { throw "cancellation: details.cancelled must be true" }
            $cancelPhase = Get-EnvCertPhase -ResultJson $resultJson -Phase 'EXECUTE_PAYLOAD'
            if ($cancelPhase.reason -ne 'CANCELLED') { throw "cancellation: EXECUTE reason must be CANCELLED, got $($cancelPhase.reason)" }
        }
        if ($case['Scenario'] -eq 'rollback-failure') {
            if (-not $resultJson.cleanup.succeeded) { throw "rollback-failure: cleanup must still run clean" }
        }
        $matrixResults += [pscustomobject]@{ Scenario = $case['Scenario']; Json = $resultJson }
        Write-Host "    PASS scenario $($case['Scenario']) => $($resultJson.status) / $($resultJson.disposition)" -ForegroundColor Green
    } finally {
        if (Test-Path -LiteralPath $workDir) { Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

# Test 6: Cross-cutting invariants over every matrix result.
Write-Host "  Test 6: PASS requires EXECUTE_PAYLOAD + provenance + clean cleanup..."
foreach ($entry in $matrixResults) {
    $resultJson = $entry.Json
    $execute = Get-EnvCertPhase -ResultJson $resultJson -Phase 'EXECUTE_PAYLOAD'
    $verify = Get-EnvCertPhase -ResultJson $resultJson -Phase 'VERIFY_RESULT_PROVENANCE'
    if ($resultJson.status -eq 'PASS') {
        if ($execute.status -ne 'PASS') { throw "$($entry.Scenario): PASS without EXECUTE_PAYLOAD pass" }
        if ($verify.status -ne 'PASS' -or (-not $resultJson.provenance.verified)) {
            throw "$($entry.Scenario): PASS without verified provenance"
        }
        if (-not $resultJson.cleanup.succeeded) { throw "$($entry.Scenario): PASS without clean cleanup" }
    }
    if (($resultJson.capabilities -contains 'guest-execution') -and (-not $resultJson.provenance.verified)) {
        throw "$($entry.Scenario): guest-execution claimed without verified provenance"
    }
    if (($resultJson.capabilities -contains 'hyperv-provision') -and ((Get-EnvCertPhase -ResultJson $resultJson -Phase 'PROVISION').status -ne 'PASS')) {
        throw "$($entry.Scenario): hyperv-provision claimed without PROVISION pass"
    }
    if (($resultJson.capabilities -contains 'provider-rollback') -and ((Get-EnvCertPhase -ResultJson $resultJson -Phase 'RESTORE_ROLLBACK').status -ne 'PASS')) {
        throw "$($entry.Scenario): provider-rollback claimed without RESTORE_ROLLBACK pass"
    }
}
Write-Host "    PASS (No PASS without execution proof; capabilities honest)" -ForegroundColor Green

# Test 7: Schema validation for representative artifacts (when python+jsonschema exist).
Write-Host "  Test 7: Representative artifacts validate against certification schema..."
$pythonCmd = Get-Command python -ErrorAction SilentlyContinue
$schemaOk = $false
if ($null -ne $pythonCmd) {
    try {
        $probe = & $pythonCmd.Source -c "import jsonschema; print('OK')" 2>&1
        if ($probe -match 'OK') { $schemaOk = $true }
    } catch {
        $schemaOk = $false
    }
}
if ($schemaOk) {
    foreach ($name in @('success', 'preflight-unavailable', 'cleanup-failure')) {
        $workDir = New-EnvCertWorkDir
        try {
            $adapter = New-ProviderFixtureAdapter -Scenario $name
            $outPath = Join-Path $workDir 'result.json'
            & $envRunner -Provider hyperv -ProviderAdapter $adapter -OutputPath $outPath -RepoRoot $RepoRoot | Out-Null
            $pyCmd = "import json, jsonschema; s = json.load(open(r'$schemaPath')); d = json.load(open(r'$outPath')); jsonschema.validate(instance=d, schema=s); print('OK')"
            $val = & $pythonCmd.Source -c $pyCmd 2>&1
            if ($val -notmatch 'OK') { throw "Schema validation failed for $name : $val" }
            Write-Host "    PASS schema ($name)" -ForegroundColor Green
        } finally {
            if (Test-Path -LiteralPath $workDir) { Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue }
        }
    }
} else {
    Write-Host "    SKIPPED (python+jsonschema unavailable; structural checks already passed)" -ForegroundColor Yellow
}

Write-Host "All Test-ProviderCertification tests PASSED!" -ForegroundColor Green
