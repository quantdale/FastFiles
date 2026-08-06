<#
.SYNOPSIS
    FastFiles autonomous runtime-verification harness — entry point.

.DESCRIPTION
    Exposes the fixed verb set (build, install, run, diagnose, report, repair, gate).
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
    [ValidateSet('build', 'install', 'run', 'diagnose', 'report', 'repair', 'gate', 'list', 'doctor')]
    [string] $Verb,

    [string] $Change = 'ad-hoc',
    [string[]] $Capability,
    [string[]] $Configuration = @('debug', 'release'),
    [switch] $Clean,
    [switch] $SkipAnalyze,
    [switch] $SkipTests,
    [string] $Provider = 'local',
    [switch] $Elevate,
    [switch] $ElevatedChild,
    [ValidateSet('human', 'json')]
    [string] $Format = 'human',
    [string] $RunTimestamp
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

function ConvertTo-ProcessArgument {
    param([Parameter(Mandatory)] [string] $Value)
    '"' + ($Value -replace '(\\*)"', '$1$1\\"') + '"'
}

if ($Elevate -and -not $ElevatedChild -and -not (Test-IsElevated)) {
    # This is deliberately opt-in: UAC approval is the one-time authorization for
    # Tier-1 local validation. The child retains the ordinary non-interactive verbs.
    $childArgs = @('-NoProfile', '-File', $PSCommandPath, $Verb, '-Change', $Change, '-Provider', $Provider, '-ElevatedChild')
    if ($Capability -and $Capability.Count -gt 0) { $childArgs += @('-Capability', ($Capability -join ',')) }
    foreach ($config in $Configuration) { $childArgs += @('-Configuration', $config) }
    if ($Clean) { $childArgs += '-Clean' }
    if ($SkipAnalyze) { $childArgs += '-SkipAnalyze' }
    if ($SkipTests) { $childArgs += '-SkipTests' }
    if ($RunTimestamp) { $childArgs += @('-RunTimestamp', $RunTimestamp) }
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
    $fingerprint = Get-EnvironmentFingerprint -Change $Change -ProviderId $activeProvider.Id -Provider $providerManifestRecord
    $discovery = Find-Capabilities -CapabilitiesRoot (Join-Path $VerifyRoot 'capabilities') `
        -ManifestSchemaPath (Join-Path $VerifyRoot 'schemas\capability-manifest.schema.json')
    $fingerprint.CapabilityLoadDiagnostics = @($discovery.LoadDiagnostics)

    $runContext = New-VerificationRun -VerifyRoot $VerifyRoot -Change $Change
    Save-RunManifest -RunContext $runContext -Fingerprint $fingerprint

    $capsToRun = $discovery.Capabilities
    if ($CapabilityFilter -and $CapabilityFilter.Count -gt 0) {
        $capsToRun = $capsToRun | Where-Object { $CapabilityFilter -contains $_.Id }
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
        State = $null
    }
    $options = @{
        RepoRoot       = $RepoRoot
        Configurations = $Configuration
        Clean          = $Clean.IsPresent
        SkipAnalyze    = $SkipAnalyze.IsPresent
        SkipTests      = $SkipTests.IsPresent
    }
    # Presence of CapabilityFilter in the options marks a -Capability-filtered run
    # (partial capability set). Consumers such as windows-performance-baselines use
    # it to avoid updating the persistent baseline store from a partial run.
    if ($CapabilityFilter -and @($CapabilityFilter).Count -gt 0) {
        $options.CapabilityFilter = @($CapabilityFilter)
    }

    $envelopes = @()
    $providerProvisioned = $false
    try {
        # Match intake.ps1 (Invoke-PhaseProvision): a provider that cannot provision
        # (e.g. -Provider hyperv without a provisionable Hyper-V target) must NOT fall
        # back to running capabilities on the local host - that would validate the
        # wrong environment and silently produce a different result than requested.
        $provision = Invoke-EnvironmentProviderLifecycle -Provider $activeProvider -Phase provision -ProviderContext $providerContext
        if (-not $provision.Ready) {
            Write-Host "[ERROR] Environment provider '$($activeProvider.Id)' is not ready: $($provision.Reason)" -ForegroundColor Red
            Write-Host "        Refusing to run capabilities on the local host instead of the requested provider."
            exit $ExitError
        }
        $providerProvisioned = $true
        Invoke-EnvironmentProviderLifecycle -Provider $activeProvider -Phase activate -ProviderContext $providerContext | Out-Null

        foreach ($cap in $capsToRun) {
            Write-Host "==> Running capability: $($cap.Id)" -ForegroundColor Cyan
            $envelope = Invoke-Capability -Capability $cap -RunContext $runContext -Fingerprint $fingerprint -Options $options
            $envelopes += $envelope
            Write-Host "    status=$($envelope.status)$(if ($envelope.reason) { " reason=$($envelope.reason)" })"
        }
    } finally {
        if ($providerProvisioned) {
            $cleanup = $null
            try {
                Invoke-EnvironmentProviderLifecycle -Provider $activeProvider -Phase collectLogs -ProviderContext $providerContext | Out-Null
            } finally {
                $cleanup = Invoke-EnvironmentProviderLifecycle -Provider $activeProvider -Phase cleanup -ProviderContext $providerContext
            }
            if (-not $cleanup.succeeded) {
                throw "Local provider teardown failed; see $($runContext.RunPath)\provider-cleanup.json"
            }
        }
    }

    Build-RunIndex -RunContext $runContext | Out-Null
    New-JsonRunReport -RunContext $runContext | Out-Null
    New-MarkdownRunReport -RunContext $runContext | Out-Null
    New-HtmlRunReport -RunContext $runContext | Out-Null
    New-JUnitRunReport -RunContext $runContext | Out-Null
    $fidelity = Test-RunReportFidelity -RunContext $runContext
    if (-not $fidelity.Valid) { throw "Report fidelity check failed: $($fidelity.Errors -join '; ')" }

    [pscustomobject]@{ RunContext = $runContext; Envelopes = $envelopes; Fingerprint = $fingerprint }
}

function Get-VerbExitCode {
    param([array] $Envelopes)
    if (@($Envelopes | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0) { return $ExitFail }
    if (@($Envelopes | Where-Object { $_.status -eq 'PASS' }).Count -eq 0) { return $ExitSkipped }
    return $ExitPass
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
    param([string] $RequestedTimestamp)

    $changeRunsDir = Join-Path $VerifyRoot "runs\$Change"
    if (-not $RequestedTimestamp) {
        $latest = Get-ChildItem -Path $changeRunsDir -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if (-not $latest) { throw "No runs found for change '$Change' under $changeRunsDir" }
        $RequestedTimestamp = $latest.Name
    }
    $runPath = Join-Path $changeRunsDir $RequestedTimestamp
    if (-not (Test-Path -LiteralPath $runPath)) { throw "Run not found: $runPath" }
    [pscustomobject]@{
        VerifyRoot    = $VerifyRoot
        Change        = $Change
        Timestamp     = $RequestedTimestamp
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
    'build' {
        $result = Invoke-VerificationRun -CapabilityFilter @('windows-build-validation')
        Write-Host ''
        Write-Host "Run tree: $($result.RunContext.RunPath)"
        exit (Get-VerbExitCode -Envelopes $result.Envelopes)
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
        exit (Get-VerbExitCode -Envelopes $result.Envelopes)
    }
    'run' {
        # pwsh -File passes -Capability as literal strings; accept a comma-joined
        # list as well as a single id so multi-capability runs work non-interactively.
        $filter = @($Capability | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
        $result = Invoke-VerificationRun -CapabilityFilter $filter
        Write-Host ''
        Write-Host "Run tree: $($result.RunContext.RunPath)"
        exit (Get-VerbExitCode -Envelopes $result.Envelopes)
    }
    'diagnose' {
        if (-not $RunTimestamp) {
            $result = Invoke-VerificationRun -CapabilityFilter @('diagnostics', 'crash-analysis')
            Write-Host ''
            Write-Host "Run tree: $($result.RunContext.RunPath)"
            exit (Get-VerbExitCode -Envelopes $result.Envelopes)
        }

        $runContext = Get-ExistingRunContext -RequestedTimestamp $RunTimestamp
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
        $runContext = Get-ExistingRunContext -RequestedTimestamp $RunTimestamp
        $mdPath = Write-RunReports -RunContext $runContext
        Write-Host "Report written: $mdPath"
        exit $ExitPass
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
        $inventory += [pscustomobject]@{ id = 'visual-studio-vc-toolset'; status = if ($toolchain) { 'PASS' } else { 'SKIPPED' }; reason = if ($toolchain) { $null } else { 'vs-toolchain-not-found' }; path = if ($toolchain) { $toolchain.InstallationPath } else { $null }; version = if ($toolchain) { $toolchain.InstallationVersion } else { $null }; discovery = 'vswhere'; detail = 'VC.Tools.x86.x64 with bundled CMake/Ninja' }
        $inventory += [pscustomobject]@{ id = 'windows-sdk'; status = if ($toolchain -and $toolchain.WindowsSdkVersion) { 'PASS' } else { 'SKIPPED' }; reason = if ($toolchain -and $toolchain.WindowsSdkVersion) { $null } else { 'windows-sdk-not-found' }; path = $null; version = if ($toolchain) { $toolchain.WindowsSdkVersion } else { $null }; discovery = 'vswhere/developer-environment'; detail = 'Selected Windows SDK' }
        $inventory += [pscustomobject]@{ id = 'cmake'; status = if ($toolchain -and $toolchain.CMakeExe) { 'PASS' } else { 'SKIPPED' }; reason = if ($toolchain -and $toolchain.CMakeExe) { $null } else { 'bundled-cmake-not-found' }; path = if ($toolchain) { $toolchain.CMakeExe } else { $null }; version = $null; discovery = 'visual-studio-installation'; detail = 'Toolset-bundled CMake' }
        $inventory += [pscustomobject]@{ id = 'ninja'; status = if ($toolchain -and $toolchain.NinjaExe) { 'PASS' } else { 'SKIPPED' }; reason = if ($toolchain -and $toolchain.NinjaExe) { $null } else { 'bundled-ninja-not-found' }; path = if ($toolchain) { $toolchain.NinjaExe } else { $null }; version = $null; discovery = 'visual-studio-installation'; detail = 'Toolset-bundled Ninja' }
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
        $runContext = Get-ExistingRunContext -RequestedTimestamp $RunTimestamp
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
