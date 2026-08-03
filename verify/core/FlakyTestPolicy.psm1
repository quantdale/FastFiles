<#
.SYNOPSIS
    FastFiles flaky-test policy (zero-touch 7.3).

.DESCRIPTION
    Implements the repository's flaky-test policy: root-cause-first, retries
    never mask. On an intermittent failure the policy (1) preserves the initial
    output, (2) reproduces under stress, (3) captures timing/resource/thread/
    process/environment data, and (4) classifies the outcome as either
    Deterministic (surface for diagnosis) or Intermittent (requires a root-cause
    fix or a documented non-determinism bound before a terminal PASS). A
    "passed on retry" is never the terminal state without a root-cause fix or a
    documented bound.

.NOTES
    This module is imported by verify/intake.ps1 (the autonomous orchestrator)
    and is unit-testable standalone. It is deliberately framework-free: it writes
    evidence as JSON it can also read back, so a later agent can audit it.
#>

[CmdletBinding()]
param()

Set-StrictMode -Version Latest

function New-FlakyEvidenceDir {
    <#
    .SYNOPSIS
        Creates the evidence directory for a flaky-monitored test.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $TestName
    )
    $safe = $TestName -replace '[^\w\.\-]', '_'
    $dir = Join-Path $RunRoot "flaky-evidence\$safe"
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    return $dir
}

function Get-CurrentProcessSnapshot {
    <#
    .SYNOPSIS
        Captures a coarse timing/resource/process/environment snapshot.
    #>
    [CmdletBinding()]
    param()
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $ps = Get-Process -ErrorAction SilentlyContinue |
        Sort-Object CPU -Descending |
        Select-Object -First 8 Id, ProcessName, CPU, WorkingSet64, @{n='Threads';e={$_.Threads.Count}} |
        ForEach-Object { [pscustomobject]@{ id=$_.Id; name=$_.ProcessName; cpu=[math]::Round($_.CPU,2); workingSetMb=[math]::Round($_.WorkingSet64/1MB,1); threads=$_.Threads } }
    $sw.Stop()

    # Self-contained elevation/session probes so this module also works standalone.
    $elevated = $false
    try { $elevated = (New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) } catch { $elevated = $false }
    $interactive = $false
    try { $interactive = [Environment]::UserInteractive -and -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('SESSIONNAME')) } catch { $interactive = $false }

    [pscustomobject]@{
        capturedAt = (Get-Date).ToString('o')
        elapsedMs   = $sw.ElapsedMilliseconds
        processCount = @(Get-Process -ErrorAction SilentlyContinue).Count
        topProcesses = @($ps)
        env = @{
            cpuCount = [Environment]::ProcessorCount
            osVersion = [Environment]::OSVersion.VersionString
            pwshVersion = $PSVersionTable.PSVersion.ToString()
            isElevated = $elevated
            interactive = $interactive
        }
    }
}

function Invoke-TestWithFlakyPolicy {
    <#
    .SYNOPSIS
        Runs a test command once, and on failure applies the flaky-test policy:
        preserve initial output, reproduce under stress, capture timing/resource/
        process/env data, and classify Determinate vs Intermittent.

    .PARAMETER ScriptBlock
        The test to run. Must set $global:LASTEXITCODE or return a non-zero value
        on failure. Writes its own stdout/stderr; the caller captures it.

    .PARAMETER TestName
        Logical name used for evidence paths.

    .PARAMETER RunRoot
        Root directory (run dir) under which flaky-evidence/<TestName>/ is written.

    .PARAMETER StressRuns
        Number of additional runs to attempt when the first run fails (default 3).

    .PARAMETER TimeoutSeconds
        Per-run timeout. A run exceeding this is treated as a failure (Category=Timeout).

    .OUTPUTS
        A policy document:
          { test, outcome, initialRun, stressRuns, classification,
            recommendation, fixed, evidenceDir, evidencePaths, passedOnRetry }
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [scriptblock] $ScriptBlock,
        [Parameter(Mandatory)] [string] $TestName,
        [Parameter(Mandatory)] [string] $RunRoot,
        [int] $StressRuns = 3,
        [int] $TimeoutSeconds = 300
    )

    $evidenceDir = New-FlakyEvidenceDir -RunRoot $RunRoot -TestName $TestName
    $before = Get-CurrentProcessSnapshot

    function Invoke-SingleRun {
        param([string] $Label, [int] $TimeoutSeconds)
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $job = Start-Job -ScriptBlock $ScriptBlock
        $finished = $job | Wait-Job -Timeout $TimeoutSeconds
        $sw.Stop()
        if (-not $finished) {
            Stop-Job $job -ErrorAction SilentlyContinue
            Remove-Job $job -Force -ErrorAction SilentlyContinue
            return [pscustomobject]@{ ok = $false; durationMs = $sw.ElapsedMilliseconds; category = 'Timeout'; output = "<timed out after $TimeoutSeconds s>" }
        }
        $output = Receive-Job $job -ErrorAction SilentlyContinue
        $exit = $job.State -eq 'Completed' -and $job.ExitCode -eq 0
        Remove-Job $job -Force -ErrorAction SilentlyContinue
        return [pscustomobject]@{ ok = [bool]$exit; durationMs = $sw.ElapsedMilliseconds; category = if ($exit) { 'Pass' } else { 'Fail' }; output = ($output -join "`n") }
    }

    $initial = Invoke-SingleRun -Label 'initial' -TimeoutSeconds $TimeoutSeconds
    $initialOutput = @{
        label = 'initial'
        ok = $initial.ok
        category = $initial.category
        durationMs = $initial.durationMs
        output = $initial.output
    }

    $stressRuns = @()
    $passedOnRetry = $false
    if (-not $initial.ok) {
        for ($i = 1; $i -le $StressRuns; $i++) {
            $r = Invoke-SingleRun -Label "stress-$i" -TimeoutSeconds $TimeoutSeconds
            $stressRuns += [pscustomobject]@{
                label = "stress-$i"
                ok = $r.ok
                category = $r.category
                durationMs = $r.durationMs
                output = $r.output
            }
            if ($r.ok) { $passedOnRetry = $true }
        }
    }

    $after = Get-CurrentProcessSnapshot

    # Classification: pass-on-first -> Stable. Fail on every run -> Deterministic.
    # Mixture (some pass, some fail) -> Intermittent.
    $classification = 'Stable'
    $recommendation = 'none'
    $requiresRootCauseFix = $false
    if (-not $initial.ok) {
        $nonPass = @($stressRuns | Where-Object { -not $_.ok }).Count
        if ($nonPass -eq $stressRuns.Count) {
            $classification = 'Deterministic'
            $recommendation = 'surface for diagnosis (root-cause required)'
            $requiresRootCauseFix = $true
        } else {
            $classification = 'Intermittent'
            $recommendation = 'root-cause fix or documented non-determinism bound required before terminal PASS'
            $requiresRootCauseFix = $true
        }
    }

    $doc = [pscustomobject]@{
        test = $TestName
        policy = 'flaky-test-policy-v1'
        outcome = $classification
        passedOnRetry = $passedOnRetry
        requiresRootCauseFix = $requiresRootCauseFix
        recommendation = $recommendation
        initialRun = $initialOutput
        stressRuns = @($stressRuns)
        before = $before
        after = $after
        evidenceDir = $evidenceDir
    }

    # Write the policy document + per-run artifacts.
    $docPath = Join-Path $evidenceDir 'policy.json'
    $doc | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $docPath -Encoding utf8
    $summary = Join-Path $evidenceDir 'summary.txt'
    @(
        "Flaky-test policy evidence for: $TestName"
        "Outcome: $classification"
        "Initial run: $($initial.category) in $($initial.durationMs) ms"
        "Stress runs:"; $stressRuns | ForEach-Object { "  $($_.label): $($_.category) in $($_.durationMs) ms" }
        "Passed-on-retry: $passedOnRetry"
        "Recommendation: $recommendation"
    ) | Set-Content -LiteralPath $summary -Encoding utf8

    $doc | Add-Member -NotePropertyName evidencePaths -NotePropertyValue @($docPath, $summary) -Force
    return $doc
}

function Get-FlakyPolicyAbbreviation {
    <#
    .SYNOPSIS
        Returns a short human/JSON marker for a policy outcome (for status output).
    #>
    [CmdletBinding()]
    param([string] $Outcome)
    $r = switch ($Outcome) {
        'Stable' { 'stable' }
        'Deterministic' { 'deterministic-fail' }
        'Intermittent' { 'intermittent' }
        default { 'unknown' }
    }
    return $r
}

Export-ModuleMember -Function New-FlakyEvidenceDir, Get-CurrentProcessSnapshot, Invoke-TestWithFlakyPolicy, Get-FlakyPolicyAbbreviation