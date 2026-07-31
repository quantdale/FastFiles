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
    [ValidateSet('build', 'install', 'run', 'diagnose', 'report', 'repair', 'gate')]
    [string] $Verb,

    [string] $Change = 'ad-hoc',
    [string[]] $Capability,
    [string[]] $Configuration = @('debug', 'release'),
    [switch] $Clean,
    [switch] $SkipAnalyze,
    [switch] $SkipTests,
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
Import-Module (Join-Path $VerifyRoot 'core\RunTree.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Registry.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\CapabilityRunner.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Reporting.psm1') -Force

function Invoke-VerificationRun {
    param([string[]] $CapabilityFilter)

    $fingerprint = Get-EnvironmentFingerprint -Change $Change
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

    $options = @{
        RepoRoot       = $RepoRoot
        Configurations = $Configuration
        Clean          = $Clean.IsPresent
        SkipAnalyze    = $SkipAnalyze.IsPresent
        SkipTests      = $SkipTests.IsPresent
    }

    $envelopes = @()
    foreach ($cap in $capsToRun) {
        Write-Host "==> Running capability: $($cap.Id)" -ForegroundColor Cyan
        $envelope = Invoke-Capability -Capability $cap -RunContext $runContext -Fingerprint $fingerprint -Options $options
        $envelopes += $envelope
        Write-Host "    status=$($envelope.status)$(if ($envelope.reason) { " reason=$($envelope.reason)" })"
    }

    Build-RunIndex -RunContext $runContext | Out-Null
    New-JsonRunReport -RunContext $runContext | Out-Null
    New-MarkdownRunReport -RunContext $runContext | Out-Null

    [pscustomobject]@{ RunContext = $runContext; Envelopes = $envelopes }
}

function Get-VerbExitCode {
    param([array] $Envelopes)
    if (@($Envelopes | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0) { return $ExitFail }
    if (@($Envelopes | Where-Object { $_.status -eq 'PASS' }).Count -eq 0) { return $ExitSkipped }
    return $ExitPass
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
        Write-Host "Report written: $mdPath"
        exit $ExitPass
    }
    default {
        Write-Warning "Verb '$Verb' is scaffolded (exposed per task 1.1) but its capability lands in a later phase of autonomous-runtime-verification: install/service work needs Tier-1 elevation (tasks 5-6), diagnose needs the Diagnostics/Crash-Analysis capability (task 3), repair needs the repair-loop driver (task 9), and gate needs the four-state archive gate (task 10)."
        exit $ExitNotImplemented
    }
}
