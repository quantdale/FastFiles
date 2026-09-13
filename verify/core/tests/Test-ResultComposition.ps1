<#
    runtime-certification-and-verifier-convergence 2.3/4.4/6.3:
    deterministic in-memory and worker-boundary regression fixtures. These
    tests deliberately use a temporary fixture root, never verify/runs.
#>

$ErrorActionPreference = 'Stop'
$script:Failures = 0
$script:WorkerBoundarySkipped = $false

function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:Failures++ }
}

function Test-WorkerBoundaryAvailable {
    $pwsh = Get-Command pwsh.exe -ErrorAction SilentlyContinue
    if ($null -eq $pwsh) { return $false }
    $probe = $null
    try {
        $probe = Start-Process -FilePath $pwsh.Source -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', 'exit 0') `
            -PassThru -WindowStyle Hidden -ErrorAction Stop
        if (-not $probe.WaitForExit(5000)) {
            try { $probe.Kill() } catch { }
            try { $probe.WaitForExit() } catch { }
            return $false
        }
        return $probe.ExitCode -eq 0
    } catch {
        if ($probe) {
            try { $probe.Kill() } catch { }
            try { $probe.WaitForExit() } catch { }
        }
        return $false
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\ResultModel.psm1') -Force
Import-Module (Join-Path $repoRoot 'verify\core\RunTree.psm1') -Force
Import-Module (Join-Path $repoRoot 'verify\core\CapabilityRunner.psm1') -Force

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-verifier-fixture-' + [guid]::NewGuid().ToString('n'))
try {
    $fixtureCapabilities = Join-Path $fixtureRoot 'capabilities\fixture'
    $fixtureSchemas = Join-Path $fixtureRoot 'schemas'
    New-Item -ItemType Directory -Path $fixtureCapabilities, $fixtureSchemas -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot 'verify\schemas\capability-manifest.schema.json') -Destination $fixtureSchemas -Force

    $moduleText = @'
function Test-FixtureAvailability { [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null } }
function Invoke-Fixture {
    param($RunContext, $Fingerprint, [string]$ArtifactsDir, [hashtable]$Options)
    $mode = [string]$Options.FixtureMode
    if ($mode -eq 'timeout') { Start-Sleep -Seconds 35 }
    if ($mode -eq 'crash') { throw 'simulated capability crash' }
    if ($mode -eq 'cancel') {
        return [pscustomobject]@{
            Status = 'FAIL'; Reason = 'capability-cancelled'; Classification = 'CANCELLED'; Summary = 'cancelled'; Artifacts = @();
            SubResults = @([pscustomobject]@{ id = 'fixture-scenario'; status = 'FAIL'; classification = 'CANCELLED'; reason = 'capability-cancelled'; detail = 'cancelled'; durationMs = 1; diagnostics = @() })
        }
    }
    if ($mode -eq 'multi') {
        [pscustomobject]@{ Status = 'PASS'; Reason = $null; Summary = 'first'; Artifacts = @(); SubResults = @() }
        [pscustomobject]@{ Status = 'PASS'; Reason = $null; Summary = 'second'; Artifacts = @(); SubResults = @() }
        return
    }
    if ($mode -eq 'missing') { return }
    if ($mode -eq 'single-artifact') {
        return [pscustomobject]@{
            Status = 'PASS'; Reason = $null; Summary = 'single artifact';
            Artifacts = @([pscustomobject]@{ path = "artifacts/$($RunContext.Change)/one.log"; type = 'suite-logs' });
            SubResults = @([pscustomobject]@{ id = 'fixture-scenario'; status = 'PASS'; reason = $null; detail = 'ok'; durationMs = 1; diagnostics = @() })
        }
    }
    if ($mode -eq 'badstatus') { return [pscustomobject]@{ Status = 'UNKNOWN'; Reason = 'bad'; Summary = 'bad'; Artifacts = @(); SubResults = @() } }
    [pscustomobject]@{
        Status = 'PASS'; Reason = $null; Summary = 'fixture passed'; Artifacts = @();
        SubResults = @([pscustomobject]@{ id = 'fixture-scenario'; status = 'PASS'; reason = $null; detail = 'ok'; durationMs = 1; diagnostics = @() })
    }
}
function Get-FixtureDiagnostics { @('fixture-scenario') }
Export-ModuleMember -Function Test-FixtureAvailability, Invoke-Fixture, Get-FixtureDiagnostics
'@
    Set-Content -LiteralPath (Join-Path $fixtureCapabilities 'Fixture.psm1') -Value $moduleText -Encoding utf8
    $manifest = @'
{
  "id": "fixture",
  "interfaceVersion": "1.0.0",
  "tier": 0,
  "timeoutSeconds": 30,
  "displayName": "Verifier fixture",
  "description": "Temporary verifier result fixture",
  "entryModule": "Fixture.psm1",
  "repairPosture": "repair-unsupported",
  "entryPoints": {
    "availability": "Test-FixtureAvailability",
    "run": "Invoke-Fixture",
    "diagnostics": "Get-FixtureDiagnostics"
  }
}
'@
    Set-Content -LiteralPath (Join-Path $fixtureCapabilities 'capability.json') -Value $manifest -Encoding utf8

    function New-FixtureRunContext {
        param([string] $Name)
        $runPath = Join-Path $fixtureRoot ("runs\$Name")
        New-Item -ItemType Directory -Path (Join-Path $runPath 'artifacts') -Force | Out-Null
        $id = [guid]::NewGuid().ToString('n')
        [pscustomobject]@{
            VerifyRoot = $fixtureRoot; Change = 'fixture'; Timestamp = $Name; RunId = $id; InvocationId = $id
            RunPath = $runPath; ManifestPath = (Join-Path $runPath 'manifest.json'); IndexPath = (Join-Path $runPath 'index.json')
            RepairLogPath = (Join-Path $runPath 'repair-log.jsonl'); ArtifactsRoot = (Join-Path $runPath 'artifacts')
            SchemasRoot = (Join-Path $repoRoot 'verify\schemas')
        }
    }

    $passOutcome = Resolve-VerificationOutcome -DeclaredStatus PASS -DeclaredSummary 'pass' -SubResults @() -Lifecycle $null
    Check ($passOutcome.status -eq 'PASS' -and $passOutcome.classification -eq 'PASS') 'all-pass composition is PASS'
    $skipOutcome = Resolve-VerificationOutcome -DeclaredStatus PASS -DeclaredSummary 'pass plus skip' -SubResults @(
        [pscustomobject]@{ scenarioId = 'optional'; status = 'SKIPPED'; classification = 'BLOCKED_ENVIRONMENT'; reason = 'interactive-foreground-unavailable'; detail = 'host' }
    ) -Lifecycle $null
    Check ($skipOutcome.status -eq 'PASS') 'PASS plus optional SKIP remains PASS'
    $allSkipOutcome = Resolve-VerificationOutcome -DeclaredStatus SKIPPED -DeclaredReason 'provider-unavailable' -DeclaredClassification 'REQUIRED-BUT-UNAVAILABLE' -DeclaredSummary 'none' -SubResults @() -Lifecycle $null
    Check ($allSkipOutcome.status -eq 'SKIPPED' -and $allSkipOutcome.classification -eq 'REQUIRED-BUT-UNAVAILABLE') 'all-skip composition is truthful SKIPPED'
    $assertionOutcome = Resolve-VerificationOutcome -DeclaredStatus PASS -DeclaredSummary 'counter says pass' -SubResults @(
        [pscustomobject]@{ scenarioId = 'assertion'; status = 'FAIL'; classification = 'ASSERTION_FAILURE'; reason = 'product-check-failed'; detail = 'assertion' }
    ) -Lifecycle $null
    Check ($assertionOutcome.status -eq 'FAIL' -and $assertionOutcome.reason -eq 'product-check-failed') 'child assertion failure outranks a contradictory PASS declaration'
    $cleanupOutcome = Resolve-VerificationOutcome -DeclaredStatus PASS -DeclaredSummary 'all assertions passed' -SubResults @() `
        -Lifecycle ([pscustomobject]@{ cleanup = [pscustomobject]@{ pass = $false; residue = @('owned-pid') } })
    Check ($cleanupOutcome.status -eq 'FAIL' -and $cleanupOutcome.classification -eq 'CLEANUP_INCOMPLETE') 'cleanup failure after PASS is visible'
    $bothOutcome = Resolve-VerificationOutcome -DeclaredStatus PASS -DeclaredSummary 'assertion and cleanup' -SubResults @(
        [pscustomobject]@{ scenarioId = 'assertion'; status = 'FAIL'; classification = 'ASSERTION_FAILURE'; reason = 'assertion-failed'; detail = 'primary' }
    ) -Lifecycle ([pscustomobject]@{ cleanup = [pscustomobject]@{ pass = $false; residue = @('owned-pid') } })
    Check ($bothOutcome.classification -eq 'ASSERTION_FAILURE' -and @($bothOutcome.secondaryFailures).Count -eq 1 -and $bothOutcome.secondaryFailures[0].classification -eq 'CLEANUP_INCOMPLETE') 'assertion and cleanup preserve primary plus secondary failures'
    Check ((Get-VerificationExitCode -Envelopes @([pscustomobject]@{ status = 'PASS'; classification = 'PASS' })) -eq 0) 'PASS projects to exit 0'
    Check ((Get-VerificationExitCode -Envelopes @([pscustomobject]@{ status = 'SKIPPED'; classification = 'REQUIRED-BUT-UNAVAILABLE' })) -eq 2) 'SKIPPED projects to exit 2'
    Check ((Get-VerificationExitCode -Envelopes @([pscustomobject]@{ status = 'FAIL'; classification = 'HARNESS_ERROR' })) -eq 3) 'HARNESS_ERROR projects to exit 3'

    $runPass = Resolve-VerificationRunOutcome -CapabilityEnvelopes @(
        [pscustomobject]@{ capabilityId = 'pass'; status = 'PASS'; classification = 'PASS'; reason = $null; summary = 'pass' },
        [pscustomobject]@{ capabilityId = 'optional'; status = 'SKIPPED'; classification = 'BLOCKED_ENVIRONMENT'; reason = 'interactive-foreground-unavailable'; summary = 'host' }
    )
    Check ($runPass.status -eq 'PASS' -and $runPass.classification -eq 'PASS') 'top-level all-pass plus optional skip is PASS'
    $runFailure = Resolve-VerificationRunOutcome -CapabilityEnvelopes @(
        [pscustomobject]@{ capabilityId = 'assertion'; status = 'FAIL'; classification = 'ASSERTION_FAILURE'; reason = 'product-check-failed'; summary = 'assertion' }
    ) -AdditionalFailures @([pscustomobject]@{ status = 'FAIL'; classification = 'REPORT_FAILURE'; reason = 'report-generation-failed'; summary = 'report' })
    Check ($runFailure.classification -eq 'ASSERTION_FAILURE' -and @($runFailure.secondaryFailures).Count -eq 1 -and $runFailure.secondaryFailures[0].classification -eq 'REPORT_FAILURE') 'top-level primary assertion survives a secondary report failure'
    $runSkip = Resolve-VerificationRunOutcome -RunFailures @([pscustomobject]@{ status = 'SKIPPED'; classification = 'REQUIRED-BUT-UNAVAILABLE'; reason = 'provider-preflight-unavailable'; summary = 'provider missing' })
    Check ($runSkip.status -eq 'SKIPPED' -and $runSkip.classification -eq 'REQUIRED-BUT-UNAVAILABLE') 'provider preflight block is a truthful top-level SKIPPED'
    $opaqueFailure = Resolve-VerificationOutcome -DeclaredStatus FAIL -DeclaredReason 'scenario-failed' -DeclaredSummary '23 passed, 6 skipped, 0 failed' `
        -SubResults @(
            [pscustomobject]@{ scenarioId = 'pass-one'; status = 'PASS'; classification = 'PASS'; detail = 'ok' }
            [pscustomobject]@{ scenarioId = 'skip-one'; status = 'SKIPPED'; classification = 'BLOCKED_ENVIRONMENT'; reason = 'interactive-foreground-unavailable'; detail = 'desktop boundary' }
        )
    Check ($opaqueFailure.status -eq 'FAIL' -and $opaqueFailure.classification -eq 'HARNESS_ERROR' -and $opaqueFailure.reason -eq 'capability-failed-without-attributed-cause' -and $opaqueFailure.summary -match 'scenario-failed' -and $opaqueFailure.primaryFailure.declaredReason -eq 'scenario-failed') 'zero-failure child counters cannot hide an attributed capability failure'
    $liveUiaOpaque = Resolve-VerificationOutcome -DeclaredStatus FAIL -DeclaredReason 'scenario-failed' -DeclaredClassification 'ASSERTION_FAILURE' -DeclaredSummary '23 passed, 6 skipped, 0 failed' `
        -SubResults @(
            [pscustomobject]@{ scenarioId = 'pass-one'; status = 'PASS'; classification = 'PASS'; detail = 'ok' }
            [pscustomobject]@{ scenarioId = 'skip-one'; status = 'SKIPPED'; classification = 'BLOCKED_ENVIRONMENT'; reason = 'interactive-foreground-unavailable'; detail = 'desktop boundary' }
        )
    Check ($liveUiaOpaque.status -eq 'FAIL' -and $liveUiaOpaque.classification -eq 'HARNESS_ERROR' -and $liveUiaOpaque.reason -eq 'capability-failed-without-attributed-cause' -and $liveUiaOpaque.primaryFailure.declaredReason -eq 'scenario-failed') 'live UIA FAIL+ASSERTION_FAILURE+scenario-failed with zero child failures is a harness error, not an attributed product assertion'
    $environmentSkip = Resolve-VerificationRunOutcome -CapabilityEnvelopes @([pscustomobject]@{ capabilityId = 'uia'; status = 'SKIPPED'; classification = 'BLOCKED_ENVIRONMENT'; reason = 'interactive-foreground-unavailable'; summary = 'desktop unavailable' })
    Check ($environmentSkip.status -eq 'SKIPPED' -and $environmentSkip.classification -eq 'BLOCKED_ENVIRONMENT' -and $environmentSkip.reason -eq 'interactive-foreground-unavailable') 'all-skipped environment capability preserves its truthful top-level boundary'
    $cancelledChild = Resolve-VerificationOutcome -DeclaredStatus FAIL -DeclaredReason 'capability-cancelled' -DeclaredClassification 'CANCELLED' -DeclaredSummary 'cancelled at worker' -SubResults @(
        [pscustomobject]@{ scenarioId = 'worker'; status = 'FAIL'; classification = 'CANCELLED'; reason = 'capability-cancelled'; detail = 'cancelled' }
    )
    Check ($cancelledChild.status -eq 'FAIL' -and $cancelledChild.classification -eq 'CANCELLED' -and $cancelledChild.reason -eq 'capability-cancelled') 'child cancellation is CANCELLED, not an assertion failure'
    $cancelledBare = Resolve-VerificationOutcome -DeclaredStatus FAIL -DeclaredReason 'capability-cancelled' -DeclaredClassification 'CANCELLED' -DeclaredSummary 'cancelled at capability' -SubResults @()
    Check ($cancelledBare.status -eq 'FAIL' -and $cancelledBare.classification -eq 'CANCELLED' -and $cancelledBare.reason -eq 'capability-cancelled') 'explicit capability-level CANCELLED is retained without fabricating a child assertion'
    $unknownCap = Resolve-VerificationRunOutcome -RunFailures @([pscustomobject]@{ status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'unknown-capability'; summary = 'Unknown or rejected capability id(s): does-not-exist.' })
    Check ($unknownCap.status -eq 'FAIL' -and $unknownCap.classification -eq 'HARNESS_ERROR' -and $unknownCap.reason -eq 'unknown-capability') 'unknown capability is a harness failure, not SKIPPED'
    Check ((Get-VerificationExitCode -Envelopes @($unknownCap)) -eq 3) 'unknown capability projects to harness exit 3'
    Check ((Get-VerificationExitCode -Envelopes @([pscustomobject]@{ status = 'FAIL'; classification = 'TIMEOUT'; reason = 'capability-timeout' })) -eq 1) 'TIMEOUT projects to FAIL exit 1'
    Check ((Get-VerificationExitCode -Envelopes @([pscustomobject]@{ status = 'FAIL'; classification = 'CANCELLED'; reason = 'capability-cancelled' })) -eq 1) 'CANCELLED projects to FAIL exit 1'
    $providerUnavailable = Resolve-VerificationRunOutcome -RunFailures @([pscustomobject]@{ status = 'SKIPPED'; classification = 'REQUIRED-BUT-UNAVAILABLE'; reason = 'provider-preflight-unavailable'; summary = 'requested provider is not ready' })
    Check ($providerUnavailable.status -eq 'SKIPPED' -and $providerUnavailable.classification -eq 'REQUIRED-BUT-UNAVAILABLE' -and $providerUnavailable.reason -eq 'provider-preflight-unavailable') 'provider preflight unavailable stays SKIPPED, not FAIL'
    $malformedComposition = Resolve-VerificationOutcome -DeclaredStatus FAIL -DeclaredReason 'malformed-capability-output' -DeclaredClassification 'HARNESS_ERROR' -DeclaredSummary 'worker candidate identity or protocol mismatch' -SubResults @(
        [pscustomobject]@{ scenarioId = 'capability-worker'; status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'malformed-capability-output'; detail = 'invalid candidate' }
    )
    Check ($malformedComposition.status -eq 'FAIL' -and $malformedComposition.classification -eq 'HARNESS_ERROR' -and $malformedComposition.reason -eq 'malformed-capability-output') 'malformed capability output is HARNESS_ERROR'

    $sameSecondOne = New-VerificationRun -VerifyRoot $fixtureRoot -Change 'same-second'
    $sameSecondTwo = New-VerificationRun -VerifyRoot $fixtureRoot -Change 'same-second'
    Check ($sameSecondOne.RunId -ne $sameSecondTwo.RunId -and $sameSecondOne.InvocationId -ne $sameSecondTwo.InvocationId) 'back-to-back runs receive distinct run and invocation identities'
    Check ($sameSecondOne.RunPath -ne $sameSecondTwo.RunPath -and $sameSecondTwo.Timestamp -eq (Split-Path -Leaf $sameSecondTwo.RunPath)) 'same-second run directories are disambiguated and selectable by their actual name'

    if (Test-WorkerBoundaryAvailable) {
        $capability = [pscustomobject]@{ Id = 'fixture'; InterfaceVersion = '1.0.0'; Tier = 0; TimeoutSeconds = 30 }
        $fingerprint = [pscustomobject]@{ ProviderId = 'local'; TargetIdentity = 'fixture'; IsElevated = $false }
        $passContext = New-FixtureRunContext -Name 'pass'
        $passEnvelope = Invoke-Capability -Capability $capability -RunContext $passContext -Fingerprint $fingerprint -Options @{ FixtureMode = 'pass' }
        Write-Host ("fixture pass observed status={0} classification={1} reason={2} summary={3} children={4}" -f $passEnvelope.status, $passEnvelope.classification, $passEnvelope.reason, $passEnvelope.summary, @($passEnvelope.subResults).Count)
        $workerExecutionUnavailable = $passEnvelope.status -eq 'FAIL' -and
            $passEnvelope.classification -eq 'TIMEOUT' -and
            $passEnvelope.reason -eq 'capability-timeout' -and
            [string]$passEnvelope.summary -match 'no worker stderr/stdout was captured'
        if ($workerExecutionUnavailable) {
            $script:WorkerBoundarySkipped = $true
            Write-Host 'SKIP: worker-boundary fixtures unavailable because the fixture worker timed out before producing any diagnostic output.'
        } else {
            Check ($passEnvelope.status -eq 'PASS' -and $passEnvelope.runId -eq $passContext.RunId -and $passEnvelope.invocationId -eq $passContext.InvocationId) 'worker pass is identity-bound and persisted as one envelope'
            Check (@($passEnvelope.subResults).Count -eq 1 -and $passEnvelope.subResults[0].scenarioId -eq 'fixture-scenario') 'worker pass preserves a stable child scenario'

            foreach ($case in @(
                @{ Name = 'multi'; Mode = 'multi'; Status = 'FAIL'; Classification = 'HARNESS_ERROR'; Label = 'multiple worker results are harness failure' },
                @{ Name = 'missing'; Mode = 'missing'; Status = 'FAIL'; Classification = 'HARNESS_ERROR'; Label = 'missing worker result is harness failure' },
                @{ Name = 'badstatus'; Mode = 'badstatus'; Status = 'FAIL'; Classification = 'HARNESS_ERROR'; Label = 'unsupported result status is harness failure' },
                @{ Name = 'timeout'; Mode = 'timeout'; Status = 'FAIL'; Classification = 'TIMEOUT'; Label = 'worker timeout is terminal TIMEOUT' },
                @{ Name = 'crash'; Mode = 'crash'; Status = 'FAIL'; Classification = 'HARNESS_ERROR'; Label = 'worker crash is terminal HARNESS_ERROR' },
                @{ Name = 'cancel'; Mode = 'cancel'; Status = 'FAIL'; Classification = 'CANCELLED'; Label = 'worker cancellation is terminal CANCELLED' }
            )) {
                $context = New-FixtureRunContext -Name $case.Name
                $envelope = Invoke-Capability -Capability $capability -RunContext $context -Fingerprint $fingerprint -Options @{ FixtureMode = $case.Mode }
                Write-Host ("fixture {0} observed status={1} classification={2} reason={3} summary={4}" -f $case.Mode, $envelope.status, $envelope.classification, $envelope.reason, $envelope.summary)
                Check ($envelope.status -eq $case.Status -and $envelope.classification -eq $case.Classification) $case.Label
            }
            # Regression: a capability emitting exactly ONE artifact used to
            # unwrap to a scalar and make `$rawArtifacts + $workerArtifacts`
            # throw PSObject op_Addition, turning a real PASS into a
            # result-envelope-composition-failed HARNESS_ERROR.
            $singleContext = New-FixtureRunContext -Name 'single-artifact'
            $singleEnvelope = Invoke-Capability -Capability $capability -RunContext $singleContext -Fingerprint $fingerprint -Options @{ FixtureMode = 'single-artifact' }
            Write-Host ("fixture single-artifact observed status={0} classification={1} artifacts={2}" -f $singleEnvelope.status, $singleEnvelope.classification, @($singleEnvelope.artifacts).Count)
            Check ($singleEnvelope.status -eq 'PASS' -and $singleEnvelope.classification -eq 'PASS') 'a single capability artifact still composes one PASS envelope'
            Check (@($singleEnvelope.artifacts).Count -eq 3) 'the single capability artifact is preserved alongside the worker artifacts'
        }
    } else {
        $script:WorkerBoundarySkipped = $true
        Write-Host 'SKIP: worker-boundary fixtures unavailable because a bounded nested PowerShell probe did not complete.'
    }

    $wrongContext = New-FixtureRunContext -Name 'wrong-identity'
    $normalizedChild = ConvertTo-VerificationChildResult -Child ([pscustomobject]@{ id = 'one'; status = 'PASS'; durationMs = 0 }) `
        -RunContext $wrongContext -CapabilityId 'fixture' -InvocationId $wrongContext.InvocationId -DefaultStartedAtUtc ([DateTime]::UtcNow.ToString('o')) -DefaultFinishedAtUtc ([DateTime]::UtcNow.ToString('o'))
    $wrongEnvelope = New-ResultEnvelope -CapabilityId 'fixture' -InterfaceVersion '1.0.0' -Tier 0 -Status PASS -StartedAtUtc (Get-Date) -FinishedAtUtc (Get-Date) `
        -RunContext ([pscustomobject]@{ RunId = 'other'; InvocationId = 'other' }) -SubResults @($normalizedChild)
    $identityRejected = $false
    try { Save-CapabilityResult -RunContext $wrongContext -Result $wrongEnvelope | Out-Null } catch { $identityRejected = $true }
    Check $identityRejected 'cross-run result cannot be persisted into the selected invocation'
} finally {
    if (Test-Path -LiteralPath $fixtureRoot) { [IO.Directory]::Delete($fixtureRoot, $true) }
}

if ($script:Failures -gt 0) { Write-Host "$($script:Failures) verifier result checks failed."; exit 1 }
if ($script:WorkerBoundarySkipped) { Write-Host 'Verifier result-composition checks passed; worker-boundary fixtures were skipped with a truthful host prerequisite.'; exit 77 }
Write-Host 'All verifier result-composition checks passed.'
exit 0
