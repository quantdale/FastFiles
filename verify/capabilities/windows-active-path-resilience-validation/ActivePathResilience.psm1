<#
    Active-path resilience capability.

    The executable owns a marked disposable fixture and exercises the real
    degraded enumerator, DirectoryWatcher, UiServer, SnapshotPublisher, and
    EngineClient seams in one process. This module owns only the external
    lifecycle: it launches that exact owned process, samples its resource
    counters, bounds teardown, and records the seed and observations under the
    run artifact namespace.

    Resource samples are diagnostic evidence, not a flat-memory threshold.
    A process that completes the deterministic workload and is reclaimed
    cleanly is a pass; unexplained sampling, timeout, or cleanup failures fail
    the corresponding sub-result.
#>

$script:HarnessRelativePath = 'build\debug\tests\engine\ffengine_active_path_resilience_tests.exe'

function New-ActivePathSubResult {
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [ValidateSet('PASS', 'FAIL', 'SKIPPED')] [string] $Status,
        [string] $Reason,
        [double] $DurationMs = 0,
        [Parameter(Mandatory)] [string] $Detail,
        [string[]] $Diagnostics = @()
    )

    [pscustomobject]@{
        id = $Id
        tier = 1
        status = $Status
        reason = $Reason
        requiredContext = $null
        durationMs = [math]::Round($DurationMs, 0)
        detail = $Detail
        diagnostics = $Diagnostics
    }
}

function Test-ActivePathResilienceAvailability {
    param([Parameter(Mandatory)] $Fingerprint)

    if (-not $IsWindows) {
        return [pscustomobject]@{
            Available = $false
            Reason = 'windows-required'
            RequiredContext = [pscustomobject]@{ needs = 'Windows degraded filesystem runtime' }
        }
    }

    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-ActivePathResilienceDiagnostics {
    return @(
        'deterministic-marked-fixture-churn'
        'watch-root-cap-and-lru-cycling'
        'unicode-long-name-delete-recreate'
        'filesystem-to-published-snapshot-convergence'
        'engine-client-reconnect-and-remap'
        'resource-sampling-handles-threads-memory-working-set-cpu'
        'owned-process-bounded-cleanup'
        'interactive-ui-optional-explicit-skip'
    )
}

function Get-UInt64Option {
    param(
        [hashtable] $Options,
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [UInt64] $Default
    )

    if ($null -eq $Options -or -not $Options.ContainsKey($Name)) {
        return $Default
    }

    $raw = [string]$Options[$Name]
    try {
        if ($raw -match '^0[xX]([0-9a-fA-F]+)$') {
            return [Convert]::ToUInt64($matches[1], 16)
        }
        return [Convert]::ToUInt64($raw, 10)
    } catch {
        throw "Capability option '$Name' must be an unsigned integer (decimal or 0x-prefixed): '$raw'."
    }
}

function Get-SoakSeconds {
    param([hashtable] $Options)

    $value = Get-UInt64Option -Options $Options -Name 'ActivePathSoakSeconds' -Default 600
    if ($value -lt 1 -or $value -gt 3600) {
        throw "Capability option 'ActivePathSoakSeconds' must be between 1 and 3600 seconds."
    }
    return [int]$value
}

function Get-ProcessResourceSample {
    param(
        [Parameter(Mandatory)] [System.Diagnostics.Process] $Process,
        [Parameter(Mandatory)] [double] $ElapsedMs
    )

    try {
        $Process.Refresh()
        if ($Process.HasExited) {
            return [pscustomobject]@{
                elapsedMs = [math]::Round($ElapsedMs, 0)
                processLiveness = 'exited'
                handleCount = $null
                threadCount = $null
                privateBytes = $null
                workingSetBytes = $null
                cpuMilliseconds = $null
                sampleError = $null
            }
        }

        return [pscustomobject]@{
            elapsedMs = [math]::Round($ElapsedMs, 0)
            processLiveness = 'running'
            handleCount = $Process.HandleCount
            threadCount = $Process.Threads.Count
            privateBytes = $Process.PrivateMemorySize64
            workingSetBytes = $Process.WorkingSet64
            cpuMilliseconds = [math]::Round($Process.TotalProcessorTime.TotalMilliseconds, 0)
            sampleError = $null
        }
    } catch {
        return [pscustomobject]@{
            elapsedMs = [math]::Round($ElapsedMs, 0)
            processLiveness = 'unknown'
            handleCount = $null
            threadCount = $null
            privateBytes = $null
            workingSetBytes = $null
            cpuMilliseconds = $null
            sampleError = $_.Exception.Message
        }
    }
}

function Stop-OwnedProcess {
    param(
        [Parameter(Mandatory)] [System.Diagnostics.Process] $Process,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [System.Collections.Generic.List[object]] $CleanupDiagnostics
    )

    try {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction Stop
            if (-not $Process.WaitForExit(5000)) {
                [void]$CleanupDiagnostics.Add('owned-process-exit-timeout')
                return $false
            }
        }
    } catch {
        [void]$CleanupDiagnostics.Add("owned-process-stop-failed:$($_.Exception.Message)")
        return $false
    }

    try {
        Get-Process -Id $Process.Id -ErrorAction Stop | Out-Null
        [void]$CleanupDiagnostics.Add('owned-process-still-present-after-cleanup')
        return $false
    } catch [Microsoft.PowerShell.Commands.ProcessCommandException] {
        return $true
    } catch {
        [void]$CleanupDiagnostics.Add("owned-process-post-cleanup-check-failed:$($_.Exception.Message)")
        return $false
    }
}

function Invoke-ActivePathResilienceCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $started = Get-Date
    $seconds = Get-SoakSeconds -Options $Options
    # 0xA17E5EED is above Int32::MaxValue; use its unsigned decimal form so
    # PowerShell does not first materialize the hexadecimal literal as -1...
    # and then reject the conversion to UInt64.
    $seed = Get-UInt64Option -Options $Options -Name 'ActivePathSeed' -Default ([UInt64]2709413613)
    $exe = Join-Path $Options.RepoRoot $script:HarnessRelativePath

    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        return [pscustomobject]@{
            Status = 'SKIPPED'
            Reason = 'active-path-harness-not-built'
            Summary = "Headless active-path harness is not built; expected $script:HarnessRelativePath."
            Artifacts = @()
            SubResults = @(
                (New-ActivePathSubResult -Id 'headless-churn' -Status 'SKIPPED' -Reason 'harness-not-built' -Detail "Missing $script:HarnessRelativePath.")
                (New-ActivePathSubResult -Id 'resource-sampling' -Status 'SKIPPED' -Reason 'harness-not-built' -Detail 'No owned process was started, so resource sampling was unavailable.')
                (New-ActivePathSubResult -Id 'owned-process-cleanup' -Status 'SKIPPED' -Reason 'harness-not-built' -Detail 'No owned process was started.')
                (New-ActivePathSubResult -Id 'interactive-ui-resilience' -Status 'SKIPPED' -Reason 'interactive-desktop-not-requested' -Detail 'The capability intentionally provides headless engine/runtime coverage; GUI-only validation remains in the UIA and visual capabilities.')
            )
        }
    }

    $stdoutPath = Join-Path $ArtifactsDir 'harness.stdout.log'
    $stderrPath = Join-Path $ArtifactsDir 'harness.stderr.log'
    $samplesPath = Join-Path $ArtifactsDir 'resource-samples.json'
    $argumentList = @('--soak-seconds', ([string]$seconds), '--seed', ([string]$seed))
    $process = $null
    $samples = [System.Collections.Generic.List[object]]::new()
    $cleanupDiagnostics = [System.Collections.Generic.List[object]]::new()
    $samplingErrors = [System.Collections.Generic.List[string]]::new()
    $timedOut = $false
    $startException = $null
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

    try {
        try {
            $process = Start-Process -FilePath $exe -ArgumentList $argumentList -PassThru `
                -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -WindowStyle Hidden -ErrorAction Stop
        } catch {
            $startException = $_.Exception.Message
        }

        if ($null -ne $process) {
            # Sample approximately once per second. WaitForExit is the bounded
            # synchronization primitive; no blind sleep is used.
            $deadlineMs = (($seconds + 60) * 1000)
            do {
                $sample = Get-ProcessResourceSample -Process $process -ElapsedMs $stopwatch.Elapsed.TotalMilliseconds
                [void]$samples.Add($sample)
                if ($sample.sampleError) {
                    [void]$samplingErrors.Add([string]$sample.sampleError)
                }

                if ($sample.processLiveness -eq 'exited') {
                    break
                }

                if ($stopwatch.ElapsedMilliseconds -ge $deadlineMs) {
                    $timedOut = $true
                    break
                }

                [void]$process.WaitForExit(1000)
            } while ($true)
        }
    } finally {
        if ($null -ne $process) {
            if (-not (Stop-OwnedProcess -Process $process -CleanupDiagnostics $cleanupDiagnostics)) {
                # Stop-OwnedProcess records the structured reason; the result
                # below turns it into a failing cleanup row.
                $timedOut = $timedOut -or $true
            }
        }
        $stopwatch.Stop()
    }

    $samples | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $samplesPath -Encoding utf8

    $totalMs = $stopwatch.Elapsed.TotalMilliseconds
    $artifacts = @(
        [pscustomobject]@{ path = 'artifacts/windows-active-path-resilience-validation/harness.stdout.log'; type = 'headless-harness-output' }
        [pscustomobject]@{ path = 'artifacts/windows-active-path-resilience-validation/harness.stderr.log'; type = 'headless-harness-error-output' }
        [pscustomobject]@{ path = 'artifacts/windows-active-path-resilience-validation/resource-samples.json'; type = 'resource-samples' }
    )

    $headlessStatus = 'FAIL'
    $headlessReason = 'harness-start-failed'
    $headlessDetail = if ($startException) { "start-failed=$startException" } else { "seed=$seed; soakSeconds=$seconds; samples=$($samples.Count)" }
    $exitCode = $null
    if ($null -ne $process) {
        try {
            $process.Refresh()
            $exitCode = $process.ExitCode
        } catch {
            $headlessDetail += "; exit-code-unavailable=$($_.Exception.Message)"
        }
    }
    if ($null -ne $process -and -not $startException -and -not $timedOut -and $exitCode -eq 0) {
        $headlessStatus = 'PASS'
        $headlessReason = $null
    } elseif ($timedOut) {
        $headlessReason = 'harness-timeout-or-cleanup-failure'
        $headlessDetail += '; bounded timeout/cleanup failure observed'
    } elseif ($null -ne $exitCode) {
        $headlessReason = 'harness-failed'
        $headlessDetail += "; exitCode=$exitCode"
    }

    $resourceStatus = 'PASS'
    $resourceReason = $null
    $resourceDetail = "samples=$($samples.Count); samplingErrors=$($samplingErrors.Count); byte-growth-is-observational"
    if ($null -eq $process) {
        $resourceStatus = 'FAIL'
        $resourceReason = 'process-not-started'
    } elseif ($samples.Count -lt 2) {
        $resourceStatus = 'FAIL'
        $resourceReason = 'insufficient-resource-samples'
    } elseif ($samplingErrors.Count -gt 0) {
        $resourceStatus = 'FAIL'
        $resourceReason = 'resource-sampling-failed'
        $resourceDetail += "; firstError=$($samplingErrors[0])"
    }

    $cleanupStatus = if ($cleanupDiagnostics.Count -eq 0 -and $null -ne $process) { 'PASS' } else { 'FAIL' }
    $cleanupReason = if ($cleanupStatus -eq 'PASS') { $null } else { 'owned-process-cleanup-failed' }
    $cleanupDetail = if ($cleanupDiagnostics.Count -eq 0) { 'owned harness process exited and PID was no longer present' } else { $cleanupDiagnostics -join '; ' }

    $subResults = @(
        (New-ActivePathSubResult -Id 'headless-churn' -Status $headlessStatus -Reason $headlessReason -DurationMs $totalMs -Detail $headlessDetail -Diagnostics @('watch-cycle', 'snapshot-convergence', 'engine-reconnect'))
        (New-ActivePathSubResult -Id 'resource-sampling' -Status $resourceStatus -Reason $resourceReason -DurationMs $totalMs -Detail $resourceDetail -Diagnostics @('handle-count', 'thread-count', 'private-bytes', 'working-set', 'cpu', 'process-liveness'))
        (New-ActivePathSubResult -Id 'owned-process-cleanup' -Status $cleanupStatus -Reason $cleanupReason -DurationMs $totalMs -Detail $cleanupDetail -Diagnostics @('exact-owned-pid'))
        (New-ActivePathSubResult -Id 'interactive-ui-resilience' -Status 'SKIPPED' -Reason 'interactive-desktop-not-requested' -Detail 'Headless runtime coverage completed; UIA and visual capabilities own GUI-only scenarios.')
    )

    $failures = @($subResults | Where-Object status -eq 'FAIL').Count
    [pscustomobject]@{
        Status = if ($failures -gt 0) { 'FAIL' } else { 'PASS' }
        Reason = if ($failures -gt 0) { 'active-path-resilience-failed' } else { $null }
        Summary = "deterministic degraded-path harness seed=$seed ran for up to $seconds seconds; $(@($subResults | Where-Object status -eq 'PASS').Count) passed, $failures failed, $(@($subResults | Where-Object status -eq 'SKIPPED').Count) skipped"
        Artifacts = $artifacts
        SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-ActivePathResilienceAvailability, Invoke-ActivePathResilienceCapability, Get-ActivePathResilienceDiagnostics
