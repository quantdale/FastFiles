<#
    Historical performance baselines (task 7.7). Per-fingerprint baseline store
    under verify/baselines/<fingerprint-key>/baseline.json with a rolling sample
    window, trimmed-mean baselines, configurable per-metric thresholds, graceful
    first-baseline seeding, and advisory (non-gating) regression verdicts. The
    gate verb (Gate.psm1) decides whether regressions block, per gate policy.
#>

$script:BaselinesRoot = Join-Path $PSScriptRoot '..\baselines'
$script:MaxSamplesPerMetric = 8
$script:DefaultRegressionThreshold = 0.20

function Get-BaselineFingerprintKey {
    param([Parameter(Mandatory)] [pscustomobject] $Fingerprint)
    $identity = "$($Fingerprint.OsBuild)|$($Fingerprint.Toolchain.InstallationVersion)|$($Fingerprint.Toolchain.WindowsSdkVersion)"
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($identity))
    ($hash | ForEach-Object { $_.ToString('x2') }) -join ''
}

function Get-BaselineStorePath {
    param([Parameter(Mandatory)] [pscustomobject] $Fingerprint)
    $key = Get-BaselineFingerprintKey -Fingerprint $Fingerprint
    Join-Path $script:BaselinesRoot "$key\baseline.json"
}

function Get-BaselineStore {
    param([Parameter(Mandatory)] [pscustomobject] $Fingerprint)
    $path = Get-BaselineStorePath -Fingerprint $Fingerprint
    if (-not (Test-Path -LiteralPath $path)) {
        # Metrics = $null (not a bare hashtable): a hashtable's PSObject members
        # (Keys/Values/Count/...) would leak into the store if copied.
        return [pscustomobject]@{ Path = $path; Metrics = $null; UpdatedAtUtc = $null; FingerprintKey = $null }
    }
    $store = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    [pscustomobject]@{ Path = $path; Metrics = $store.metrics; UpdatedAtUtc = $store.updatedAtUtc; FingerprintKey = $store.fingerprintKey }
}

function Get-TrimmedMean {
    param([Parameter(Mandatory)] [double[]] $Values)
    if ($Values.Count -eq 0) { return 0.0 }
    if ($Values.Count -le 2) { return ($Values | Measure-Object -Average).Average }
    $sorted = @($Values | Sort-Object)
    $inner = $sorted[1..($sorted.Count - 2)]
    [math]::Round(($inner | Measure-Object -Average).Average, 1)
}

function Get-MetricBaseline {
    <#
        Returns the stored baseline (trimmed mean) for a metric, or $null when no
        baseline exists yet (first-baseline seeding).
    #>
    param(
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint,
        [Parameter(Mandatory)] [string] $MetricId
    )
    $store = Get-BaselineStore -Fingerprint $Fingerprint
    $metric = $store.Metrics.$MetricId
    if (-not $metric) { return $null }
    [pscustomobject]@{
        Baseline = [double] $metric.baseline
        SampleCount = [int] $metric.sampleCount
        LastValue = [double] $metric.lastValue
    }
}

function Compare-MetricAgainstBaseline {
    <#
        Advisory comparison: returns the ratio vs baseline and a regression flag
        driven by the metric threshold (default 0.20). The returned verdict is
        always advisory - it does not fail the capability; the gate decides.
    #>
    param(
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint,
        [Parameter(Mandatory)] [string] $MetricId,
        [Parameter(Mandatory)] [double] $Value,
        [double] $Threshold = $script:DefaultRegressionThreshold
    )
    $baseline = Get-MetricBaseline -Fingerprint $Fingerprint -MetricId $MetricId
    if (-not $baseline) {
        return [pscustomobject]@{ HasBaseline = $false; Baseline = $null; Ratio = $null; Regression = $false; Threshold = $Threshold }
    }
    $ratio = [math]::Round($Value / $baseline.Baseline, 3)
    [pscustomobject]@{
        HasBaseline = $true
        Baseline = $baseline.Baseline
        Ratio = $ratio
        Regression = $ratio -ge (1.0 + $Threshold)
        Threshold = $Threshold
    }
}

function Update-MetricBaseline {
    <#
        Rolling store: appends the sample, trims to the last MaxSamplesPerMetric
        entries, recomputes the trimmed-mean baseline, and persists.
    #>
    param(
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint,
        [Parameter(Mandatory)] [string] $MetricId,
        [Parameter(Mandatory)] [double] $Value
    )
    $store = Get-BaselineStore -Fingerprint $Fingerprint
    # [ordered] (OrderedDictionary), not [hashtable]: ConvertTo-Json serializes
    # plain hashtables by their members (Keys/Values/Count...) in pwsh 7.
    $metrics = [ordered]@{}
    if ($store.Metrics) {
        foreach ($name in $store.Metrics.PSObject.Properties.Name) {
            $metrics[$name] = $store.Metrics.$name
        }
    }
    $metric = $metrics[$MetricId]
    $samples = @()
    if ($metric -and $metric.samples) { $samples = @($metric.samples) }
    $samples += [math]::Round($Value, 1)
    if ($samples.Count -gt $script:MaxSamplesPerMetric) {
        $samples = $samples[($samples.Count - $script:MaxSamplesPerMetric)..($samples.Count - 1)]
    }
    $metrics[$MetricId] = [pscustomobject]@{
        samples = $samples
        baseline = Get-TrimmedMean -Values $samples
        sampleCount = $samples.Count
        lastValue = [math]::Round($Value, 1)
    }
    $key = Get-BaselineFingerprintKey -Fingerprint $Fingerprint
    $blob = [pscustomobject]@{
        fingerprintKey = $key
        updatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        metrics = $metrics
    }
    $directory = Split-Path $store.Path -Parent
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $blob | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $store.Path -Encoding utf8
}

Export-ModuleMember -Function Get-BaselineFingerprintKey, Get-BaselineStorePath, Get-BaselineStore, Get-MetricBaseline, Compare-MetricAgainstBaseline, Update-MetricBaseline
