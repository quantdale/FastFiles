<#
    Archive gate (tasks 10.1-10.3). Resolves each required capability to
    PASS / FAIL / SKIPPED / REQUIRED-BUT-UNAVAILABLE and passes only when every
    required capability is PASS and no unrepresented product-source edit exists
    in the run. The verdict is reproducible: it reads only run-tree artifacts
    (manifest.json + index.json) and the per-change gate policy under
    verify/policies/. Advisory-first by default; gate policies can declare
    performance regressions to block.
#>

$script:PoliciesRoot = Join-Path $PSScriptRoot '..\policies'
$script:DefaultProductSourcePaths = @('src', 'tests', 'CMakeLists.txt', 'cmake')

function Get-GatePolicy {
    <# Per-change gate policy with _default.json fallback (task 10.2). #>
    param([Parameter(Mandatory)] [string] $Change)

    $policyPath = Join-Path $script:PoliciesRoot "$Change.json"
    if (-not (Test-Path -LiteralPath $policyPath)) {
        $policyPath = Join-Path $script:PoliciesRoot '_default.json'
    }
    if (-not (Test-Path -LiteralPath $policyPath)) {
        throw "No gate policy found under $script:PoliciesRoot (expected $Change.json or _default.json)"
    }
    $policy = Get-Content -LiteralPath $policyPath -Raw | ConvertFrom-Json
    [pscustomobject]@{
        Path                     = $policyPath
        RequiredCapabilities     = @($policy.requiredCapabilities | ForEach-Object { $_.id })
        AcceptableSkipReasons    = @($policy.acceptableSkipReasons)
        PerformanceRegressionsGate = [bool]$policy.performanceRegressionsGate
        ProductSourcePaths       = if ($policy.productSourcePaths) { @($policy.productSourcePaths) } else { $script:DefaultProductSourcePaths }
    }
}

function Get-UnrepresentedProductSourceEdits {
    <#
        Task 10.1: any product-source file (git-tracked, under the policy's
        product-source paths) whose last write is newer than the run started is
        an unrepresented edit - the run's evidence cannot vouch for it.
    #>
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [string] $RunStartedAtUtc,
        [string[]] $ProductSourcePaths
    )

    $runStart = [datetime]::Parse($RunStartedAtUtc).ToUniversalTime()
    $edits = @()

    $pathSpecs = @($ProductSourcePaths | ForEach-Object { $_ })
    $tracked = & git -C $RepoRoot ls-files -- $pathSpecs 2>$null
    foreach ($file in @($tracked)) {
        $fullPath = Join-Path $RepoRoot $file
        if (-not (Test-Path -LiteralPath $fullPath)) { continue }
        $lastWrite = (Get-Item -LiteralPath $fullPath).LastWriteTimeUtc
        if ($lastWrite -gt $runStart) {
            $edits += [pscustomobject]@{ path = $file; lastWriteUtc = $lastWrite.ToString('o') }
        }
    }
    @($edits)
}

function Resolve-GateVerdict {
    <#
        Task 10.1: resolves the run's envelopes against the policy. A required
        capability that was SKIPPED becomes REQUIRED-BUT-UNAVAILABLE unless its
        skip reason is explicitly acceptable for this change (task 10.2).
    #>
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint,
        [Parameter(Mandatory)] [pscustomobject] $Policy,
        [Parameter(Mandatory)] [string] $RepoRoot
    )

    if (-not (Test-Path -LiteralPath $RunContext.IndexPath)) {
        throw "Run index not found: $($RunContext.IndexPath)"
    }
    $index = Get-Content -LiteralPath $RunContext.IndexPath -Raw | ConvertFrom-Json
    $byId = @{}
    foreach ($cap in $index.capabilities) { $byId[$cap.capabilityId] = $cap }

    $verdicts = @()
    foreach ($requiredId in $Policy.RequiredCapabilities) {
        $envelope = $byId[$requiredId]
        if (-not $envelope) {
            $verdicts += [pscustomobject]@{ capabilityId = $requiredId; verdict = 'REQUIRED-BUT-UNAVAILABLE'; reason = 'capability-not-executed-in-run' }
            continue
        }
        switch ($envelope.status) {
            'PASS' { $verdicts += [pscustomobject]@{ capabilityId = $requiredId; verdict = 'PASS'; reason = $null } }
            'FAIL' { $verdicts += [pscustomobject]@{ capabilityId = $requiredId; verdict = 'FAIL'; reason = $envelope.reason } }
            'SKIPPED' {
                if ($Policy.AcceptableSkipReasons -contains $envelope.reason) {
                    $verdicts += [pscustomobject]@{ capabilityId = $requiredId; verdict = 'SKIPPED'; reason = $envelope.reason }
                } else {
                    $verdicts += [pscustomobject]@{ capabilityId = $requiredId; verdict = 'REQUIRED-BUT-UNAVAILABLE'; reason = $envelope.reason }
                }
            }
        }
    }

    $performanceVerdict = $null
    if ($Policy.PerformanceRegressionsGate) {
        $baselineEnvelope = $byId['windows-performance-baselines']
        $regressions = @()
        if ($baselineEnvelope) {
            $resultPath = Join-Path $RunContext.ArtifactsRoot 'windows-performance-baselines\result.json'
            if (Test-Path -LiteralPath $resultPath) {
                $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
                $regressions = @($result.subResults | Where-Object { $_.reason -eq 'advisory-regression' })
            }
        }
        $performanceVerdict = [pscustomobject]@{
            capabilityId = 'windows-performance-baselines'
            verdict = if ($regressions.Count -gt 0) { 'FAIL' } else { 'PASS' }
            reason = if ($regressions.Count -gt 0) { "performance-regression-gated: $($regressions.Count) advisory regression(s)" } else { $null }
        }
        $verdicts += $performanceVerdict
    }

    $unrepresentedEdits = Get-UnrepresentedProductSourceEdits -RepoRoot $RepoRoot `
        -RunStartedAtUtc $Fingerprint.StartedAtUtc -ProductSourcePaths $Policy.ProductSourcePaths

    $blocking = @($verdicts | Where-Object { $_.verdict -in @('FAIL', 'REQUIRED-BUT-UNAVAILABLE') })
    $passed = $blocking.Count -eq 0 -and $unrepresentedEdits.Count -eq 0

    [pscustomobject]@{
        Policy                  = $Policy
        Verdicts                = $verdicts
        UnrepresentedEdits      = $unrepresentedEdits
        BlockingFailures        = $blocking
        Passed                  = $passed
    }
}

Export-ModuleMember -Function Get-GatePolicy, Get-UnrepresentedProductSourceEdits, Resolve-GateVerdict
