<#
    Performance-baselines capability (task 7.7). Replays nothing: it reads the
    latest prior run's index.json envelopes (durationMs per capability) as the
    metric set, compares each against the per-fingerprint baseline store, updates
    the store with the current run's already-recorded envelopes, and reports
    advisory regression verdicts. First run seeds the baseline (graceful).
    Regression verdicts never FAIL this capability - the gate verb applies policy.
#>

Import-Module (Join-Path $PSScriptRoot '..\..\core\Baselines.psm1') -Force -Global

function New-BaselineSubResult {
    param([string] $Id, [string] $Status, [string] $Reason, [double] $DurationMs, [string] $Detail)
    [pscustomobject]@{ id = $Id; tier = 0; status = $Status; reason = $Reason; requiredContext = $null; durationMs = [math]::Round($DurationMs, 0); detail = $Detail; diagnostics = @() }
}

function Test-PerformanceBaselinesAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows host with run-tree evidence' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-PerformanceBaselinesDiagnostics {
    return @('per-capability-duration-vs-baseline', 'first-baseline-seeding', 'baseline-store-update')
}

function Get-PreviousRunIndex {
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)
    $changeRunsDir = Split-Path $RunContext.RunPath -Parent
    $previous = Get-ChildItem -Path $changeRunsDir -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -lt $RunContext.Timestamp } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $previous) { return $null }
    $indexPath = Join-Path $previous.FullName 'index.json'
    if (-not (Test-Path -LiteralPath $indexPath)) { return $null }
    $index = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json
    if (@($index.capabilities).Count -eq 0) { return $null }
    $index
}

function Invoke-PerformanceBaselinesCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $subResults = @()
    $regressionCount = 0
    $seeded = @()
    $compared = 0

    # Comparison is against the latest prior run's envelopes only - the store is
    # never consulted for verification beyond the baseline numbers it already holds.
    $previousIndex = Get-PreviousRunIndex -RunContext $RunContext
    if ($previousIndex) {
        foreach ($cap in $previousIndex.capabilities) {
            if ($cap.status -ne 'PASS' -or $cap.durationMs -le 0) { continue }
            $metricId = $cap.capabilityId
            $comparison = Compare-MetricAgainstBaseline -Fingerprint $Fingerprint -MetricId $metricId -Value ([double] $cap.durationMs)
            if (-not $comparison.HasBaseline) {
                $seeded += $metricId
                continue
            }
            $compared++
            if ($comparison.Regression) {
                $regressionCount++
                $subResults += New-BaselineSubResult -Id "advisory-regression-$metricId" -Status 'PASS' -Reason 'advisory-regression' -DurationMs 0 `
                    -Detail "ratio=$($comparison.Ratio) (baseline=$($comparison.Baseline) ms, value=$($cap.durationMs) ms, threshold=$($comparison.Threshold)); advisory - gate policy decides whether this blocks"
            } else {
                $subResults += New-BaselineSubResult -Id "duration-$metricId" -Status 'PASS' -Reason $null -DurationMs 0 `
                    -Detail "ratio=$($comparison.Ratio) vs baseline=$($comparison.Baseline) ms"
            }
        }
    }

    # The store is updated from the CURRENT run's envelopes only (this capability
    # is discovered last, so every other capability has already saved its result):
    # exactly one sample per capability per run, no double counting.
    if (Test-Path -LiteralPath $RunContext.ArtifactsRoot) {
        Get-ChildItem -Path $RunContext.ArtifactsRoot -Directory | ForEach-Object {
            $resultPath = Join-Path $_.FullName 'result.json'
            if (Test-Path -LiteralPath $resultPath) {
                $envelope = Get-Content $resultPath -Raw | ConvertFrom-Json
                if ($envelope.status -eq 'PASS' -and $envelope.durationMs -gt 0) {
                    Update-MetricBaseline -Fingerprint $Fingerprint -MetricId $envelope.capabilityId -Value ([double] $envelope.durationMs)
                }
            }
        }
    }

    $summaryParts = @()
    if ($previousIndex) {
        $summaryParts += "$compared metrics compared against baseline"
        if ($seeded.Count -gt 0) { $summaryParts += "$($seeded.Count) metrics awaiting their first baseline (no prior samples)" }
        if ($regressionCount -gt 0) { $summaryParts += "$regressionCount advisory regression$(if ($regressionCount -eq 1) { '' } else { 's' }) (non-gating)" }
    } else {
        $summaryParts += "no previous run found; baseline store seeded from this run"
    }
    $summary = ($summaryParts -join '; ')

    [pscustomobject]@{
        Status = 'PASS'
        Reason = $null
        Summary = $summary
        Artifacts = @()
        SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-PerformanceBaselinesAvailability, Invoke-PerformanceBaselinesCapability, Get-PerformanceBaselinesDiagnostics
