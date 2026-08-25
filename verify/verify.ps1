<#
.SYNOPSIS
    FastFiles autonomous runtime-verification harness — entry point.

.DESCRIPTION
    Exposes the fixed verb set (build, install, run, diagnose, report, repair, gate,
    list, doctor, status, release-readiness, release-candidate).
    Every verb is non-interactive and returns one of the documented exit codes below.
    See verify/README.md for the full extensibility/usage guide.

.NOTES
    Exit codes:
      0  PASS               every executed capability/check passed
      1  FAIL                at least one executed capability/check failed
      2  SKIPPED             nothing failed, but nothing required could run either
                            (e.g. only Tier-1 capabilities were requested, unelevated)
      3  HARNESS ERROR        invalid usage or an internal harness exception
      10 NOT-YET-IMPLEMENTED  reserved for future scaffolded verbs
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('build', 'install', 'run', 'diagnose', 'report', 'repair', 'gate', 'list', 'doctor', 'status', 'release-readiness', 'release-candidate')]
    [string] $Verb,

    [string] $Change = 'ad-hoc',
    [string[]] $Capability,
    # The ordinary verifier uses the explicitly non-production optimized
    # profile. Production `release` requires externally provisioned pins and
    # is exercised by release-readiness in its authorized environment.
    [string[]] $Configuration = @('debug', 'release-test'),
    [switch] $Clean,
    [switch] $SkipAnalyze,
    [switch] $SkipTests,
    # stabilize-live-uia-e2e: pass key=value options through to capabilities
    # (e.g. -CapabilityOption UiaScenario=search-deep-result-navigation).
    [string[]] $CapabilityOption,
    [string] $Provider = 'local',
    [string] $ProviderSource,
    [switch] $Elevate,
    [switch] $ElevatedChild,
    [ValidateSet('human', 'json')]
    [string] $Format = 'human',
    [string] $RunTimestamp,
    [string] $RunId,
    [string] $VerificationRunResultPath,
    [ValidateSet('report', 'gate', 'privileged', 'compose')]
    [string] $Mode = 'report',
    [string] $ArtifactRoot,
    [string] $BuildRoot,
    [string] $PackageDirectory,
    [string] $OutputDirectory,
    [ValidateSet('unsigned-validation', 'production')]
    [string] $ReleaseMode = 'unsigned-validation',
    [string] $ManifestSignaturePath,
    [switch] $Force,
    [switch] $SkipVerification,
    [string] $ExpectedSignerThumbprint,
    [switch] $IncludePrivileged,
    [switch] $IncludeInteractive
)

$ErrorActionPreference = 'Stop'
$VerifyRoot = $PSScriptRoot
$RepoRoot = Split-Path $VerifyRoot -Parent

$ExitPass = 0
$ExitFail = 1
$ExitSkipped = 2
$ExitError = 3
$ExitNotImplemented = 10

Import-Module (Join-Path $VerifyRoot 'core\Fingerprint.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Providers.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\RunTree.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Registry.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\CapabilityRunner.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Reporting.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Repair.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Gate.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\ProjectStatus.psm1') -Force

function ConvertTo-ProcessArgument {
    param([Parameter(Mandatory)] [string] $Value)
    '"' + ($Value -replace '(\\*)"', '$1$1\\"') + '"'
}

if ($Elevate -and -not $ElevatedChild -and -not (Test-IsElevated)) {
    # This is deliberately opt-in: UAC approval is the one-time authorization for
    # Tier-1 local validation. The child retains the ordinary non-interactive verbs.
    $childArgs = @('-NoProfile', '-File', $PSCommandPath, $Verb, '-Change', $Change, '-Provider', $Provider, '-ElevatedChild')
    if ($ProviderSource) { $childArgs += @('-ProviderSource', $ProviderSource) }
    if ($Capability -and $Capability.Count -gt 0) { $childArgs += @('-Capability', ($Capability -join ',')) }
    foreach ($config in $Configuration) { $childArgs += @('-Configuration', $config) }
    if ($Clean) { $childArgs += '-Clean' }
    if ($SkipAnalyze) { $childArgs += '-SkipAnalyze' }
    if ($CapabilityOption) { foreach ($opt in $CapabilityOption) { $childArgs += @('-CapabilityOption', $opt) } }
    if ($SkipTests) { $childArgs += '-SkipTests' }
    if ($RunTimestamp) { $childArgs += @('-RunTimestamp', $RunTimestamp) }
    if ($RunId) { $childArgs += @('-RunId', $RunId) }
    if ($VerificationRunResultPath) { $childArgs += @('-VerificationRunResultPath', $VerificationRunResultPath) }
    if ($Format -ne 'human') { $childArgs += @('-Format', $Format) }
    if ($Mode -ne 'report') { $childArgs += @('-Mode', $Mode) }
        if ($ArtifactRoot) { $childArgs += @('-ArtifactRoot', $ArtifactRoot) }
        if ($BuildRoot) { $childArgs += @('-BuildRoot', $BuildRoot) }
        if ($PackageDirectory) { $childArgs += @('-PackageDirectory', $PackageDirectory) }
        if ($OutputDirectory) { $childArgs += @('-OutputDirectory', $OutputDirectory) }
        if ($ReleaseMode -ne 'unsigned-validation') { $childArgs += @('-ReleaseMode', $ReleaseMode) }
        if ($ManifestSignaturePath) { $childArgs += @('-ManifestSignaturePath', $ManifestSignaturePath) }
        if ($Force) { $childArgs += '-Force' }
        if ($SkipVerification) { $childArgs += '-SkipVerification' }
    if ($ExpectedSignerThumbprint) { $childArgs += @('-ExpectedSignerThumbprint', $ExpectedSignerThumbprint) }
    if ($IncludePrivileged) { $childArgs += '-IncludePrivileged' }
    if ($IncludeInteractive) { $childArgs += '-IncludeInteractive' }
    $quotedArgs = ($childArgs | ForEach-Object { ConvertTo-ProcessArgument -Value $_ }) -join ' '
    $child = Start-Process -FilePath (Get-Command pwsh).Source -ArgumentList $quotedArgs -Verb RunAs -Wait -PassThru
    exit $child.ExitCode
}

if ($ElevatedChild -and -not (Test-IsElevated)) {
    throw 'The elevated local-provider child did not receive an administrator token.'
}

function Invoke-VerificationRun {
    param([string[]] $CapabilityFilter)

    $activeProvider = Get-EnvironmentProvider -ProviderId $Provider
    $providerManifestRecord = Get-EnvironmentProviderManifestRecord -Provider $activeProvider
    $discovery = Find-Capabilities -CapabilitiesRoot (Join-Path $VerifyRoot 'capabilities') `
        -ManifestSchemaPath (Join-Path $VerifyRoot 'schemas\capability-manifest.schema.json')
    $runContext = New-VerificationRun -VerifyRoot $VerifyRoot -Change $Change
    $fingerprint = Get-EnvironmentFingerprint -Change $Change -ProviderId $activeProvider.Id -Provider $providerManifestRecord
    $fingerprint.CapabilityLoadDiagnostics = @($discovery.LoadDiagnostics)
    Save-RunManifest -RunContext $runContext -Fingerprint $fingerprint

    $capsToRun = $discovery.Capabilities
    $filterFailure = $null
    if ($CapabilityFilter -and $CapabilityFilter.Count -gt 0) {
        $requestedIds = @($CapabilityFilter | Where-Object { $_ })
        $unknownIds = @($requestedIds | Where-Object { $_ -notin @($discovery.Capabilities | ForEach-Object { $_.Id }) })
        if ($unknownIds.Count -gt 0) {
            $filterFailure = [pscustomobject]@{ status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'unknown-capability'; summary = "Unknown or rejected capability id(s): $($unknownIds -join ', ')." }
            $capsToRun = @()
        } else {
            $capsToRun = @($capsToRun | Where-Object { $requestedIds -contains $_.Id })
        }
    }

    if (@($capsToRun).Count -eq 0) {
        Write-Warning 'No capabilities matched the requested filter (or none are registered).'
    }

    $requiredTier = 0
    if (@($capsToRun).Count -gt 0) {
        $requiredTier = [int] (($capsToRun | Measure-Object -Property Tier -Maximum).Maximum)
    }
    $providerContext = [pscustomobject]@{
        RunContext = $runContext
        Fingerprint = $fingerprint
        RequiredTier = $requiredTier
        SourcePath = $ProviderSource
        VmName = $null
        Cancelled = $false
        State = $null
    }
    $options = @{
        RepoRoot       = $RepoRoot
        Configurations = $Configuration
        Clean          = $Clean.IsPresent
        SkipAnalyze    = $SkipAnalyze.IsPresent
        SkipTests      = $SkipTests.IsPresent
        RunId          = $runContext.RunId
        InvocationId   = $runContext.InvocationId
    }
    # stabilize-live-uia-e2e: -CapabilityOption key=value entries merge into
    # the options hashtable passed to every capability.
    foreach ($opt in @($CapabilityOption | Where-Object { $null -ne $_ })) {
        if ($opt -match '^([^=]+)=(.*)$') { $options[$matches[1]] = $matches[2] }
        else { throw "Invalid -CapabilityOption '$opt' (expected key=value)" }
    }
    # Presence of CapabilityFilter in the options marks a -Capability-filtered run
    # (partial capability set). Consumers such as windows-performance-baselines use
    # it to avoid updating the persistent baseline store from a partial run.
    if ($CapabilityFilter -and @($CapabilityFilter).Count -gt 0) {
        $options.CapabilityFilter = @($CapabilityFilter)
    }

    $envelopes = @()
    $providerProvisioned = $false
    $runFailure = $null
    if ($filterFailure) { $runFailure = $filterFailure }
    $providerLifecycleFailures = [System.Collections.Generic.List[object]]::new()
    try {
        if (-not $runFailure) {
            # Preflight is a separate, reason-coded phase. A requested external
            # provider must never silently degrade to the local host.
            $preflight = Invoke-EnvironmentProviderLifecycle -Provider $activeProvider -Phase preflight -ProviderContext $providerContext
            $preflight | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $runContext.RunPath 'provider-preflight.json') -Encoding utf8
            if (-not $preflight.Ready) {
            Write-Host "[SKIPPED] Environment provider '$($activeProvider.Id)' is not ready: $($preflight.Detail)" -ForegroundColor Yellow
            Write-Host "        Reason codes: $(@($preflight.ReasonCodes) -join ', ')"
            Write-Host '        Refusing to run capabilities on the local host instead of the requested provider.'
            $runFailure = [pscustomobject]@{ status = 'SKIPPED'; classification = 'REQUIRED-BUT-UNAVAILABLE'; reason = 'provider-preflight-unavailable'; summary = [string]$preflight.Detail }
            } else {
            # Match intake.ps1 (Invoke-PhaseProvision): a provider that cannot provision
            # must not fall back to running capabilities on the local host.
            $provision = Invoke-EnvironmentProviderLifecycle -Provider $activeProvider -Phase provision -ProviderContext $providerContext
            if (-not $provision.Ready) {
                Write-Host "[SKIPPED] Environment provider '$($activeProvider.Id)' is not ready: $($provision.Reason)" -ForegroundColor Yellow
                Write-Host '        Refusing to run capabilities on the local host instead of the requested provider.'
                $runFailure = [pscustomobject]@{ status = 'SKIPPED'; classification = 'REQUIRED-BUT-UNAVAILABLE'; reason = 'provider-provision-unavailable'; summary = [string]$provision.Reason }
            } else {
                $providerProvisioned = $true
                $activation = Invoke-EnvironmentProviderLifecycle -Provider $activeProvider -Phase activate -ProviderContext $providerContext
                if ($activation.Ready -eq $false) {
                    $runFailure = [pscustomobject]@{ status = 'FAIL'; classification = 'PROVIDER_FAILURE'; reason = 'provider-activation-failed'; summary = [string]$activation.Detail }
                } else {
                    foreach ($cap in $capsToRun) {
                        Write-Host "==> Running capability: $($cap.Id)" -ForegroundColor Cyan
                        $envelope = Invoke-Capability -Capability $cap -RunContext $runContext -Fingerprint $fingerprint -Options $options
                        $envelopes += $envelope
                        Write-Host "    status=$($envelope.status) classification=$($envelope.classification)$(if ($envelope.reason) { " reason=$($envelope.reason)" })$(if ($envelope.summary) { " summary=$($envelope.summary)" })"
                    }
                }
            }
            }
        }
    } catch {
        $runFailure = [pscustomobject]@{ status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'verification-run-exception'; summary = $_.Exception.Message }
    } finally {
        if ($providerProvisioned) {
            try {
                Invoke-EnvironmentProviderLifecycle -Provider $activeProvider -Phase collectLogs -ProviderContext $providerContext | Out-Null
            } catch {
                $providerLifecycleFailures.Add([pscustomobject]@{ classification = 'REPORT_FAILURE'; reason = 'provider-log-collection-failed'; summary = $_.Exception.Message })
            } finally {
                try {
                    $cleanup = Invoke-EnvironmentProviderLifecycle -Provider $activeProvider -Phase cleanup -ProviderContext $providerContext
                    if (-not $cleanup.succeeded) {
                        $providerLifecycleFailures.Add([pscustomobject]@{ classification = 'CLEANUP_INCOMPLETE'; reason = 'provider-cleanup-incomplete'; summary = "Provider teardown reported failure; see $($runContext.RunPath)\provider-cleanup.json" })
                    }
                } catch {
                    $providerLifecycleFailures.Add([pscustomobject]@{ classification = 'CLEANUP_INCOMPLETE'; reason = 'provider-cleanup-exception'; summary = $_.Exception.Message })
                }
            }
        }
    }

    $additionalFailures = @()
    $runFinishedAt = (Get-Date).ToUniversalTime()
    $runStartedAt = [DateTime]::Parse([string]$fingerprint.StartedAtUtc).ToUniversalTime()
    function New-CurrentRunResult {
        param([object[]] $ExtraFailures = @())
        $outcome = Resolve-VerificationRunOutcome -CapabilityEnvelopes $envelopes `
            -RunFailures @($runFailure) -LifecycleFailures @($providerLifecycleFailures) `
            -AdditionalFailures $ExtraFailures
        [pscustomobject]@{
            runId = [string]$runContext.RunId
            invocationId = [string]$runContext.InvocationId
            change = $runContext.Change
            status = $outcome.status
            classification = $outcome.classification
            reason = $outcome.reason
            summary = $outcome.summary
            startedAtUtc = $runStartedAt.ToString('o')
            finishedAtUtc = $runFinishedAt.ToString('o')
            durationMs = [math]::Round(($runFinishedAt - $runStartedAt).TotalMilliseconds, 0)
            primaryFailure = $outcome.primaryFailure
            secondaryFailures = @($outcome.secondaryFailures)
            capabilities = @($envelopes | ForEach-Object { [pscustomobject]@{ capabilityId = $_.capabilityId; status = $_.status; classification = $_.classification; reason = $_.reason } })
        }
    }
    $runResult = New-CurrentRunResult
    Save-RunResult -RunContext $runContext -Result $runResult | Out-Null
    try {
        Build-RunIndex -RunContext $runContext | Out-Null
        New-JsonRunReport -RunContext $runContext | Out-Null
        New-MarkdownRunReport -RunContext $runContext | Out-Null
        New-HtmlRunReport -RunContext $runContext | Out-Null
        New-JUnitRunReport -RunContext $runContext | Out-Null
        $fidelity = Test-RunReportFidelity -RunContext $runContext
        if (-not $fidelity.Valid) { throw "Report fidelity check failed: $($fidelity.Errors -join '; ')" }
    } catch {
        $additionalFailures = @([pscustomobject]@{
                status = 'FAIL'
                classification = 'REPORT_FAILURE'
                reason = 'report-generation-failed'
                summary = Limit-VerificationText $_.Exception.Message
            })
        $runResult = New-CurrentRunResult -ExtraFailures $additionalFailures
        Save-RunResult -RunContext $runContext -Result $runResult | Out-Null
    }
    try { Build-RunIndex -RunContext $runContext | Out-Null } catch { Write-Warning "Final run-index projection failed: $($_.Exception.Message)" }
    Write-Host "Run result: status=$($runResult.status) classification=$($runResult.classification)$(if ($runResult.reason) { " reason=$($runResult.reason)" })$(if ($runResult.summary) { " summary=$($runResult.summary)" })"

    [pscustomobject]@{ RunContext = $runContext; Envelopes = $envelopes; Fingerprint = $fingerprint; RunResult = $runResult }
}

function Get-VerbExitCode {
    param([array] $Envelopes, [pscustomobject] $RunResult)
    if ($RunResult) { return (Get-VerificationExitCode -Envelopes @($RunResult)) }
    return (Get-VerificationExitCode -Envelopes $Envelopes)
}

function Write-InspectionOutput {
    param([Parameter(Mandatory)] $Value)
    if ($Format -eq 'json') {
        $Value | ConvertTo-Json -Depth 12
    } else {
        $Value | Format-Table -AutoSize | Out-String | Write-Host
    }
}

function Get-ExistingRunContext {
    param(
        [string] $RequestedTimestamp,
        [string] $RequestedRunId
    )

    $changeRunsDir = Join-Path $VerifyRoot "runs\$Change"
    if (-not $RequestedTimestamp -and -not $RequestedRunId) {
        throw "Run selection is required: provide -RunTimestamp or -RunId; implicit latest-run discovery is disabled."
    }
    if ($RequestedRunId -and -not $RequestedTimestamp) {
        $matches = @()
        foreach ($candidate in @(Get-ChildItem -Path $changeRunsDir -Directory -ErrorAction SilentlyContinue)) {
            $candidateManifestPath = Join-Path $candidate.FullName 'manifest.json'
            if (-not (Test-Path -LiteralPath $candidateManifestPath)) { continue }
            try {
                $candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json
                if ([string]$candidateManifest.runId -eq $RequestedRunId -or [string]$candidateManifest.invocationId -eq $RequestedRunId) { $matches += $candidate }
            } catch {
                # A malformed sibling is not the selected run unless its identity matches;
                # its own report operation will fail closed when selected explicitly.
            }
        }
        if ($matches.Count -eq 0) { throw "No run with identity '$RequestedRunId' found for change '$Change'." }
        if ($matches.Count -ne 1) { throw "Run identity '$RequestedRunId' is ambiguous across $($matches.Count) run directories." }
        $RequestedTimestamp = $matches[0].Name
    }
    $runPath = Join-Path $changeRunsDir $RequestedTimestamp
    if (-not (Test-Path -LiteralPath $runPath)) { throw "Run not found: $runPath" }
    $manifestPath = Join-Path $runPath 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw "Run manifest not found: $manifestPath" }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace([string]$manifest.runId) -or [string]::IsNullOrWhiteSpace([string]$manifest.invocationId)) {
        throw "Run '$RequestedTimestamp' has no identity-bound manifest; it cannot be selected by the current verifier."
    }
    if ([string]$manifest.change -and [string]$manifest.change -ne $Change) {
        throw "Run '$RequestedTimestamp' belongs to change '$($manifest.change)', not '$Change'."
    }
    if ($RequestedRunId -and $RequestedRunId -ne [string]$manifest.runId -and $RequestedRunId -ne [string]$manifest.invocationId) {
        throw "Run '$RequestedTimestamp' does not match requested identity '$RequestedRunId'."
    }
    [pscustomobject]@{
        VerifyRoot    = $VerifyRoot
        Change        = $Change
        Timestamp     = $RequestedTimestamp
        RunId         = [string]$manifest.runId
        InvocationId  = [string]$manifest.invocationId
        RunPath       = $runPath
        ManifestPath  = Join-Path $runPath 'manifest.json'
        IndexPath     = Join-Path $runPath 'index.json'
        RepairLogPath = Join-Path $runPath 'repair-log.jsonl'
        ArtifactsRoot = Join-Path $runPath 'artifacts'
        SchemasRoot   = Join-Path $VerifyRoot 'schemas'
    }
}

function Write-RunReports {
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    Build-RunIndex -RunContext $RunContext | Out-Null
    New-JsonRunReport -RunContext $RunContext | Out-Null
    $markdownPath = New-MarkdownRunReport -RunContext $RunContext
    New-HtmlRunReport -RunContext $RunContext | Out-Null
    New-JUnitRunReport -RunContext $RunContext | Out-Null
    $fidelity = Test-RunReportFidelity -RunContext $RunContext
    if (-not $fidelity.Valid) { throw "Report fidelity check failed: $($fidelity.Errors -join '; ')" }
    return $markdownPath
}

switch ($Verb) {
    'status' {
        $statusDirectory = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $VerifyRoot 'status\generated' }
        $status = New-ProjectStatus -RepoRoot $RepoRoot -OutputDirectory $statusDirectory -VerificationRunResultPath $VerificationRunResultPath
        if ($Format -eq 'json') {
            $status | ConvertTo-Json -Depth 20
        } else {
            Write-Host "Project status: $($status.git.sha) on $($status.git.branch)"
            Write-Host "Worktree dirty: $($status.git.dirty); CTest discovered: $($status.tests.discoveredCount)"
            Write-Host "OpenSpec unchecked tasks: $($status.openSpec.totalOpenTasks); release candidate: $($status.release.classification)"
            Write-Host "JSON: $($status.jsonPath)"
            Write-Host "Markdown: $($status.markdownPath)"
        }
        exit $ExitPass
    }
    'release-candidate' {
        $candidateScript = Join-Path $VerifyRoot 'release\Build-ReleaseCandidate.ps1'
        $candidateArgs = @('-NoProfile', '-File', $candidateScript, '-Mode', $ReleaseMode, '-OutputFormat', $Format)
        if ($BuildRoot) { $candidateArgs += @('-BuildRoot', $BuildRoot) }
        if ($OutputDirectory) { $candidateArgs += @('-OutputDirectory', $OutputDirectory) }
        if ($ManifestSignaturePath) { $candidateArgs += @('-ManifestSignaturePath', $ManifestSignaturePath) }
        if ($ExpectedSignerThumbprint) { $candidateArgs += @('-ExpectedSignerThumbprint', $ExpectedSignerThumbprint) }
        if ($Force) { $candidateArgs += '-Force' }
        if ($SkipVerification) { $candidateArgs += '-SkipVerification' }
        & (Get-Command pwsh).Source @candidateArgs
        exit $LASTEXITCODE
    }
    'release-readiness' {
        $releaseScript = Join-Path $VerifyRoot 'release\ReleaseReadiness.ps1'
        $releaseArgs = @('-NoProfile', '-File', $releaseScript, '-Mode', $Mode, '-OutputFormat', $Format)
        if ($ArtifactRoot) { $releaseArgs += @('-ArtifactRoot', $ArtifactRoot) }
        if ($PackageDirectory) { $releaseArgs += @('-PackageDirectory', $PackageDirectory) }
        if ($OutputDirectory) { $releaseArgs += @('-OutputDirectory', $OutputDirectory) }
        if ($ExpectedSignerThumbprint) { $releaseArgs += @('-ExpectedSignerThumbprint', $ExpectedSignerThumbprint) }
        if ($IncludePrivileged) { $releaseArgs += '-IncludePrivileged' }
        if ($IncludeInteractive) { $releaseArgs += '-IncludeInteractive' }
        & (Get-Command pwsh).Source @releaseArgs
        exit $LASTEXITCODE
    }
    'build' {
        $result = Invoke-VerificationRun -CapabilityFilter @('windows-build-validation')
        Write-Host ''
        Write-Host "Run tree: $($result.RunContext.RunPath)"
        exit (Get-VerbExitCode -Envelopes $result.Envelopes -RunResult $result.RunResult)
    }
    'install' {
        # Tier-1 install validation plus the §6.2-6.5 harnesses that validate the
        # privileged boundary against the freshly installed product. Each one
        # availability-gates itself; unelevated runs SKIP with a reason.
        $result = Invoke-VerificationRun -CapabilityFilter @(
            'windows-install-service-validation',
            'windows-privilege-validation',
            'windows-object-security-validation',
            'windows-engine-service-validation',
            'windows-ipc-validation')
        Write-Host ''
        Write-Host "Run tree: $($result.RunContext.RunPath)"
        exit (Get-VerbExitCode -Envelopes $result.Envelopes -RunResult $result.RunResult)
    }
    'run' {
        # pwsh -File passes -Capability as literal strings; accept a comma-joined
        # list as well as a single id so multi-capability runs work non-interactively.
        $filter = @($Capability | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
        $result = Invoke-VerificationRun -CapabilityFilter $filter
        Write-Host ''
        Write-Host "Run tree: $($result.RunContext.RunPath)"
        exit (Get-VerbExitCode -Envelopes $result.Envelopes -RunResult $result.RunResult)
    }
    'diagnose' {
        if (-not $RunTimestamp -and -not $RunId) {
            $result = Invoke-VerificationRun -CapabilityFilter @('diagnostics', 'crash-analysis')
            Write-Host ''
            Write-Host "Run tree: $($result.RunContext.RunPath)"
            exit (Get-VerbExitCode -Envelopes $result.Envelopes -RunResult $result.RunResult)
        }

        $runContext = Get-ExistingRunContext -RequestedTimestamp $RunTimestamp -RequestedRunId $RunId
        if (-not (Test-Path -LiteralPath $runContext.ManifestPath)) { throw "Run manifest not found: $($runContext.ManifestPath)" }
        $fingerprint = Get-Content -LiteralPath $runContext.ManifestPath -Raw | ConvertFrom-Json
        $discovery = Find-Capabilities -CapabilitiesRoot (Join-Path $VerifyRoot 'capabilities') `
            -ManifestSchemaPath (Join-Path $VerifyRoot 'schemas\capability-manifest.schema.json')
        $crashCapability = $discovery.Capabilities | Where-Object Id -eq 'crash-analysis' | Select-Object -First 1
        if (-not $crashCapability) { throw 'The crash-analysis capability is not registered or failed registry validation.' }
        $options = @{ RepoRoot = $RepoRoot }
        $envelope = Invoke-Capability -Capability $crashCapability -RunContext $runContext -Fingerprint $fingerprint -Options $options
        Write-RunReports -RunContext $runContext | Out-Null
        Write-Host "Crash analysis: status=$($envelope.status)$(if ($envelope.reason) { " reason=$($envelope.reason)" })"
        Write-Host "Run tree: $($runContext.RunPath)"
        exit (Get-VerbExitCode -Envelopes @($envelope))
    }
    'report' {
        try {
            $runContext = Get-ExistingRunContext -RequestedTimestamp $RunTimestamp -RequestedRunId $RunId
            $mdPath = Write-RunReports -RunContext $runContext
            $index = Build-RunIndex -RunContext $runContext
            if ($null -eq $index.runResult) { throw 'Selected run has no authoritative run-result.json.' }
            Write-Host "Report written: $mdPath"
            exit (Get-VerbExitCode -RunResult $index.runResult)
        } catch {
            Write-Host "Report projection failed: $($_.Exception.Message)" -ForegroundColor Red
            exit $ExitError
        }
    }
    'list' {
        $discovery = Find-Capabilities -CapabilitiesRoot (Join-Path $VerifyRoot 'capabilities') `
            -ManifestSchemaPath (Join-Path $VerifyRoot 'schemas\capability-manifest.schema.json')
        $loaded = @($discovery.Capabilities | ForEach-Object {
            [pscustomobject]@{ id = $_.Id; interfaceVersion = $_.InterfaceVersion; tier = $_.Tier; loadStatus = 'loaded'; dependsOn = @($_.DependsOn) -join ','; reason = $null }
        })
        $rejected = @($discovery.LoadDiagnostics | ForEach-Object {
            [pscustomobject]@{ id = $_.capabilityId; interfaceVersion = $null; tier = $null; loadStatus = 'rejected'; dependsOn = $null; reason = $_.reason }
        })
        Write-InspectionOutput -Value @($loaded + $rejected)
        exit $ExitPass
    }
    'doctor' {
        $diagnosticsModule = Join-Path $VerifyRoot 'capabilities\diagnostics\Diagnostics.psm1'
        Import-Module $diagnosticsModule -Force -Global
        $inventory = @(Get-DiagnosticToolInventory)
        $toolchain = Find-VSToolchain
        $inventory += [pscustomobject]@{ id = 'powershell'; status = 'PASS'; reason = $null; path = (Get-Command pwsh).Source; version = $PSVersionTable.PSVersion.ToString(); discovery = 'command-resolver'; detail = 'PowerShell 7 host' }
        $inventory += [pscustomobject]@{ id = 'visual-studio-vc-toolset'; status = if ($toolchain) { 'PASS' } else { 'SKIPPED' }; reason = if ($toolchain) { $null } else { 'vs-toolchain-not-found' }; path = if ($toolchain) { $toolchain.InstallationPath } else { $null }; version = if ($toolchain) { $toolchain.InstallationVersion } else { $null }; discovery = 'vswhere'; detail = 'VC.Tools.x86.x64 with CMake/Ninja/CTest from PATH or discovered installation' }
        $inventory += [pscustomobject]@{ id = 'windows-sdk'; status = if ($toolchain -and $toolchain.WindowsSdkVersion) { 'PASS' } else { 'SKIPPED' }; reason = if ($toolchain -and $toolchain.WindowsSdkVersion) { $null } else { 'windows-sdk-not-found' }; path = $null; version = if ($toolchain) { $toolchain.WindowsSdkVersion } else { $null }; discovery = 'vswhere/developer-environment'; detail = 'Selected Windows SDK' }
        $inventory += [pscustomobject]@{ id = 'cmake'; status = if ($toolchain -and $toolchain.CMakeExe) { 'PASS' } else { 'SKIPPED' }; reason = if ($toolchain -and $toolchain.CMakeExe) { $null } else { 'cmake-not-found' }; path = if ($toolchain) { $toolchain.CMakeExe } else { $null }; version = $null; discovery = 'path-or-visual-studio-installation'; detail = 'CMake from PATH or discovered installation' }
        $inventory += [pscustomobject]@{ id = 'ninja'; status = if ($toolchain -and $toolchain.NinjaExe) { 'PASS' } else { 'SKIPPED' }; reason = if ($toolchain -and $toolchain.NinjaExe) { $null } else { 'ninja-not-found' }; path = if ($toolchain) { $toolchain.NinjaExe } else { $null }; version = $null; discovery = 'path-or-visual-studio-installation'; detail = 'Ninja from PATH or discovered installation' }
        $inventory += [pscustomobject]@{ id = 'ctest'; status = if ($toolchain -and $toolchain.CTestExe) { 'PASS' } else { 'SKIPPED' }; reason = if ($toolchain -and $toolchain.CTestExe) { $null } else { 'ctest-not-found' }; path = if ($toolchain) { $toolchain.CTestExe } else { $null }; version = $null; discovery = 'path-or-cmake-installation'; detail = 'CTest from PATH or the resolved CMake installation' }
        $inventory += [pscustomobject]@{ id = 'hyper-v'; status = if (Get-Module -ListAvailable Hyper-V) { 'PASS' } else { 'SKIPPED' }; reason = if (Get-Module -ListAvailable Hyper-V) { $null } else { 'hyper-v-module-not-found' }; path = $null; version = $null; discovery = 'powershell-module'; detail = 'Hyper-V provider prerequisite' }
        $inventory += [pscustomobject]@{ id = 'windows-sandbox'; status = if (Get-Command WindowsSandbox.exe -ErrorAction SilentlyContinue) { 'PASS' } else { 'SKIPPED' }; reason = if (Get-Command WindowsSandbox.exe -ErrorAction SilentlyContinue) { $null } else { 'windows-sandbox-not-found' }; path = $null; version = $null; discovery = 'command-resolver'; detail = 'Windows Sandbox provider prerequisite' }
        Write-InspectionOutput -Value $inventory
        exit $ExitPass
    }
    'repair' {
        # Task 9: run, repair failures through the coordinator loop (Class A fixes
        # auto-applied; Class B flagged), re-run repaired capabilities, re-report.
        $result = Invoke-VerificationRun -CapabilityFilter $Capability
        $discovery = Find-Capabilities -CapabilitiesRoot (Join-Path $VerifyRoot 'capabilities') `
            -ManifestSchemaPath (Join-Path $VerifyRoot 'schemas\capability-manifest.schema.json')
        $options = @{
            RepoRoot       = $RepoRoot
            Configurations = $Configuration
            Clean          = $Clean.IsPresent
            SkipAnalyze    = $SkipAnalyze.IsPresent
            SkipTests      = $SkipTests.IsPresent
        }
        $loop = Invoke-RepairLoop -RunContext $result.RunContext -Fingerprint $result.Fingerprint `
            -Discovery $discovery -Envelopes $result.Envelopes -Options $options
        Write-RunReports -RunContext $result.RunContext | Out-Null
        Write-Host ''
        Write-Host "Run tree: $($result.RunContext.RunPath)"
        if ($loop.Escalation) {
            Write-Host "Repair loop escalated: $($loop.Escalation.reason) (capability $($loop.Escalation.capabilityId))"
            exit $ExitFail
        }
        $remainingFailures = @($loop.Envelopes | Where-Object { $_.status -eq 'FAIL' }).Count
        if ($remainingFailures -gt 0) {
            Write-Host "Repair loop finished with $remainingFailures unrepaired failure(s); see repair-log.jsonl"
            exit $ExitFail
        }
        Write-Host "Repair loop finished cleanly after $($loop.Iterations) iteration(s); repaired: $($loop.RepairedIds -join ', ')"
        exit $ExitPass
    }
    'gate' {
        # Task 10: resolve required capabilities to PASS/FAIL/SKIPPED/
        # REQUIRED-BUT-UNAVAILABLE against the per-change policy, and refuse a
        # run that does not represent current product-source state.
        $runContext = Get-ExistingRunContext -RequestedTimestamp $RunTimestamp -RequestedRunId $RunId
        if (-not (Test-Path -LiteralPath $runContext.ManifestPath)) { throw "Run manifest not found: $($runContext.ManifestPath)" }
        $fingerprint = Get-Content -LiteralPath $runContext.ManifestPath -Raw | ConvertFrom-Json
        $policy = Get-GatePolicy -Change $Change
        $verdict = Resolve-GateVerdict -RunContext $runContext -Fingerprint $fingerprint -Policy $policy -RepoRoot $RepoRoot

        $verdictPath = Join-Path $runContext.RunPath 'gate-verdict.json'
        $verdict | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $verdictPath -Encoding utf8

        foreach ($v in $verdict.Verdicts) {
            $marker = if ($v.verdict -eq 'PASS') { '[PASS]' } elseif ($v.verdict -eq 'SKIPPED') { '[SKIP]' } else { "[$($v.verdict)]" }
            Write-Host "$marker $($v.capabilityId)$(if ($v.reason) { " -> $($v.reason)" })"
        }
        if ($verdict.UnrepresentedEdits.Count -gt 0) {
            Write-Host "[FAIL] unrepresented product-source edits: $($verdict.UnrepresentedEdits.path -join ', ')"
        }
        Write-Host "Gate policy: $($verdict.Policy.Path)"
        Write-Host "Gate verdict: $(if ($verdict.Passed) { 'PASS' } else { 'FAIL' })"
        exit $(if ($verdict.Passed) { $ExitPass } else { $ExitFail })
    }
}
