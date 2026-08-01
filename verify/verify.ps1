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
      10 NOT-YET-IMPLEMENTED  the verb is scaffolded but its capability lands in a
                              later phase of this change (install/diagnose/repair/gate)
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

function ConvertTo-ProcessArgument {
    param([Parameter(Mandatory)] [string] $Value)
    '"' + ($Value -replace '(\\*)"', '$1$1\\"') + '"'
}

if ($Elevate -and -not $ElevatedChild -and -not (Test-IsElevated)) {
    # This is deliberately opt-in: UAC approval is the one-time authorization for
    # Tier-1 local validation. The child retains the ordinary non-interactive verbs.
    $childArgs = @('-NoProfile', '-File', $PSCommandPath, $Verb, '-Change', $Change, '-Provider', $Provider, '-ElevatedChild')
    foreach ($id in $Capability) { $childArgs += @('-Capability', $id) }
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

    $envelopes = @()
    $providerProvisioned = $false
    try {
        Invoke-EnvironmentProviderLifecycle -Provider $activeProvider -Phase provision -ProviderContext $providerContext | Out-Null
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

    [pscustomobject]@{ RunContext = $runContext; Envelopes = $envelopes }
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

switch ($Verb) {
    'build' {
        $result = Invoke-VerificationRun -CapabilityFilter @('windows-build-validation')
        Write-Host ''
        Write-Host "Run tree: $($result.RunContext.RunPath)"
        exit (Get-VerbExitCode -Envelopes $result.Envelopes)
    }
    'run' {
        $result = Invoke-VerificationRun -CapabilityFilter $Capability
        Write-Host ''
        Write-Host "Run tree: $($result.RunContext.RunPath)"
        exit (Get-VerbExitCode -Envelopes $result.Envelopes)
    }
    'report' {
        $changeRunsDir = Join-Path $VerifyRoot "runs\$Change"
        if (-not $RunTimestamp) {
            $latest = Get-ChildItem -Path $changeRunsDir -Directory -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending | Select-Object -First 1
            if (-not $latest) {
                Write-Error "No runs found for change '$Change' under $changeRunsDir"
                exit $ExitError
            }
            $RunTimestamp = $latest.Name
        }
        $runPath = Join-Path $changeRunsDir $RunTimestamp
        if (-not (Test-Path $runPath)) {
            Write-Error "Run not found: $runPath"
            exit $ExitError
        }
        $runContext = [pscustomobject]@{
            VerifyRoot    = $VerifyRoot
            Change        = $Change
            Timestamp     = $RunTimestamp
            RunPath       = $runPath
            ManifestPath  = Join-Path $runPath 'manifest.json'
            IndexPath     = Join-Path $runPath 'index.json'
            ArtifactsRoot = Join-Path $runPath 'artifacts'
            SchemasRoot   = Join-Path $VerifyRoot 'schemas'
        }
        Build-RunIndex -RunContext $runContext | Out-Null
        New-JsonRunReport -RunContext $runContext | Out-Null
        $mdPath = New-MarkdownRunReport -RunContext $runContext
        New-HtmlRunReport -RunContext $runContext | Out-Null
        New-JUnitRunReport -RunContext $runContext | Out-Null
        $fidelity = Test-RunReportFidelity -RunContext $runContext
        if (-not $fidelity.Valid) { throw "Report fidelity check failed: $($fidelity.Errors -join '; ')" }
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
    default {
        Write-Warning "Verb '$Verb' is scaffolded (exposed per task 1.1) but its capability lands in a later phase of autonomous-runtime-verification: install/service work needs Tier-1 elevation (tasks 5-6), diagnose needs the Diagnostics/Crash-Analysis capability (task 3), repair needs the repair-loop driver (task 9), and gate needs the four-state archive gate (task 10)."
        exit $ExitNotImplemented
    }
}
