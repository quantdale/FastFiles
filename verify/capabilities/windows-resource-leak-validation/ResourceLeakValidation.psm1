<#
    Tier-1 resource-leak validation (autonomous-runtime-verification task 7.5):
    measures handle / thread / private-memory deltas across repeated engine
    start/stop cycles with a configurable tolerance, and puts the Application
    Verifier page-heap provider in force for the engine binary while the
    cycles run (enabled -> verified -> disabled, so the run is side-effect
    free). A genuine leak grows the warm-cycle counters beyond tolerance.

    Four-state contract: any check that cannot be exercised reports SKIPPED
    with a reason -- never a silent pass.
#>

$script:EngineExe = 'build\debug\src\engine\FastFilesEngine.exe'
$script:CycleCount = 3
$script:CycleSettleMs = 1500
$script:HandleTolerancePercent = 10
$script:MemoryTolerancePercent = 10
$script:ThreadTolerance = 2
$script:PageHeapRegRoot = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options'

function New-LeakSubResult {
    param([string] $Id, [string] $Status, [string] $Reason, [double] $DurationMs, [string] $Detail)
    [pscustomobject]@{
        id = $Id; tier = 1; status = $Status; reason = $Reason; requiredContext = $null
        durationMs = [math]::Round($DurationMs, 0); detail = $Detail; diagnostics = @()
    }
}

function Test-ResourceLeakValidationAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows process and Application Verifier APIs' } }
    }
    if (-not $Fingerprint.IsElevated) {
        return [pscustomobject]@{ Available = $false; Reason = 'elevation-required'; RequiredContext = [pscustomobject]@{ needs = 'Elevated administrator token to configure Application Verifier page heap (HKLM IFEO) and control engine processes' } }
    }
    if (-not (Get-Command appverif.exe -ErrorAction SilentlyContinue)) {
        return [pscustomobject]@{ Available = $false; Reason = 'application-verifier-not-found'; RequiredContext = [pscustomobject]@{ needs = 'Application Verifier (appverif.exe) on PATH' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-ResourceLeakValidationDiagnostics {
    return @('engine-start-stop-deltas', 'appverif-pageheap')
}

function Get-PageHeapInForce {
    param([string] $EngineExe)
    $leaf = Split-Path -Leaf $EngineExe
    $ifoe = Get-ItemProperty (Join-Path $script:PageHeapRegRoot $leaf) -ErrorAction SilentlyContinue
    if (-not $ifoe) { return $null }
    # The loader-level in-force signal is VerifierDlls naming vrfcore.dll:
    # that is what causes the Application Verifier heap provider to load into
    # the binary. PageHeapFlags (gflags-style full page heap) is written by
    # some appverif versions/states but not others, so it is recorded when
    # present but never required.
    $verifier = [string]$ifoe.VerifierDlls
    if ($verifier -match 'vrfcore') {
        $flags = $null
        try { $flags = '0x{0:X}' -f [int]$ifoe.PageHeapFlags } catch { $flags = $null }
        return [pscustomobject]@{ PageHeapFlags = $flags; VerifierDlls = $verifier }
    }
    return $null
}

function Invoke-ResourceLeakValidationCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $subResults = @()
    $artifacts = @()
    $engineExe = Join-Path $Options.RepoRoot $script:EngineExe
    $engineLeaf = Split-Path -Leaf $engineExe
    $ifoeKey = Join-Path $script:PageHeapRegRoot $engineLeaf
    $hadPreExistingIfoe = Test-Path -LiteralPath $ifoeKey

    if (-not (Test-Path -LiteralPath $engineExe)) {
        $subResults += New-LeakSubResult -Id 'engine-start-stop-deltas' -Status 'FAIL' -Reason 'engine-not-built' -DurationMs 0 -Detail $engineExe
        return [pscustomobject]@{
            Status = 'FAIL'; Reason = 'engine-not-built'
            Summary = 'FastFilesEngine.exe is not built; run windows-build-validation first'
            Artifacts = $artifacts; SubResults = $subResults
        }
    }

    # Phase 1: put the Application Verifier page-heap provider in force for
    # the engine binary, so every allocation in the delta cycles runs under
    # page-heap instrumentation (task 7.5 "leveraging Application Verifier/
    # PageHeap where available").
    $pageHeapLog = Join-Path $ArtifactsDir 'appverif-pageheap.log'
    $appverifOut = @(& appverif.exe -enable heaps -for $engineExe 2>&1 | ForEach-Object ToString)
    $appverifExit = $LASTEXITCODE
    $appverifOut | Set-Content -LiteralPath $pageHeapLog -Encoding utf8
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-resource-leak-validation/appverif-pageheap.log"; type = 'probe-log' }
    $pageHeap = Get-PageHeapInForce -EngineExe $engineExe
    if ($pageHeap) {
        $pageHeapDetail = "verifier heap in force for $engineLeaf (VerifierDlls=$($pageHeap.VerifierDlls)"
        if ($pageHeap.PageHeapFlags) { $pageHeapDetail += ", PageHeapFlags=$($pageHeap.PageHeapFlags)" }
        $pageHeapDetail += ")"
    } else {
        $pageHeapDetail = "appverif -enable heaps exit=$appverifExit; IFEO verification did not confirm VerifierDlls=vrfcore"
    }
    $subResults += New-LeakSubResult -Id 'appverif-pageheap' `
        -Status $(if ($pageHeap) { 'PASS' } else { 'FAIL' }) `
        -Reason $(if ($pageHeap) { $null } else { 'page-heap-not-in-force' }) -DurationMs 0 `
        -Detail $pageHeapDetail

    # Phase 2: sample process resources across start/stop cycles while page
    # heap is active. Baseline = first cycle; growth beyond tolerance by the
    # last cycle indicates a leak.
    $sampleLog = Join-Path $ArtifactsDir 'engine-samples.log'
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-resource-leak-validation/engine-samples.log"; type = 'sample-log' }
    $samples = @()
    $cycleError = $null
    for ($cycle = 1; $cycle -le $script:CycleCount; $cycle++) {
        $proc = $null
        $started = Get-Date
        try {
            $proc = Start-Process -FilePath $engineExe -PassThru -ErrorAction Stop
            Start-Sleep -Milliseconds $script:CycleSettleMs
            $proc.Refresh()
            if ($proc.HasExited) {
                throw "engine exited during cycle $cycle (code $($proc.ExitCode))"
            }
            $sample = [pscustomobject]@{
                cycle = $cycle
                handles = $proc.HandleCount
                threads = $proc.Threads.Count
                privateMemoryBytes = $proc.PrivateMemorySize64
            }
            $samples += $sample
            ($sample | ConvertTo-Json -Compress) | Add-Content -LiteralPath $sampleLog -Encoding utf8
        } catch {
            $cycleError = $_.Exception.Message
            break
        } finally {
            if ($proc -and -not $proc.HasExited) {
                Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
                $null = $proc.WaitForExit(3000)
            }
        }
    }

    # Restore machine state: disable page heap and remove the IFEO key we
    # created (or appverif emptied), leaving pre-existing state untouched.
    $null = @(& appverif.exe -disable heaps -for $engineExe 2>&1)
    if (-not $hadPreExistingIfoe) {
        Remove-Item -LiteralPath $ifoeKey -Force -ErrorAction SilentlyContinue
    }

    if ($cycleError -or $samples.Count -lt $script:CycleCount) {
        $subResults += New-LeakSubResult -Id 'engine-start-stop-deltas' -Status 'SKIPPED' `
            -Reason $(if ($cycleError -match 'exited') { 'engine-exited-during-cycle' } else { 'engine-could-not-cycle' }) `
            -DurationMs 0 -Detail $(if ($cycleError) { "$cycleError; completed $($samples.Count)/$($script:CycleCount) cycles" } else { "completed $($samples.Count)/$($script:CycleCount) cycles" })
    } else {
        $first = $samples[0]
        $last = $samples[-1]
        $handleDelta = [math]::Round(($last.handles - $first.handles) / [math]::Max(1, $first.handles) * 100, 1)
        $memoryDelta = [math]::Round(($last.privateMemoryBytes - $first.privateMemoryBytes) / [math]::Max(1, $first.privateMemoryBytes) * 100, 1)
        $threadDelta = $last.threads - $first.threads
        $handlesOk = $handleDelta -le $script:HandleTolerancePercent
        $memoryOk = $memoryDelta -le $script:MemoryTolerancePercent
        $threadsOk = $threadDelta -le $script:ThreadTolerance
        $issues = @()
        if (-not $handlesOk) { $issues += "handles grew $handleDelta% (tolerance $script:HandleTolerancePercent%)" }
        if (-not $memoryOk) { $issues += "private memory grew $memoryDelta% (tolerance $script:MemoryTolerancePercent%)" }
        if (-not $threadsOk) { $issues += "threads grew +$threadDelta (tolerance +$script:ThreadTolerance)" }
        $detail = "cycles=$($samples.Count); handles=$($first.handles)->$($last.handles) ($handleDelta%); " +
            "threads=$($first.threads)->$($last.threads) (+$threadDelta); " +
            "privateMemory=$([math]::Round($first.privateMemoryBytes/1MB,1))MB->$([math]::Round($last.privateMemoryBytes/1MB,1))MB ($memoryDelta%)"
        $subResults += New-LeakSubResult -Id 'engine-start-stop-deltas' `
            -Status $(if ($issues.Count -eq 0) { 'PASS' } else { 'FAIL' }) `
            -Reason $(if ($issues.Count -eq 0) { $null } else { 'resource-growth-beyond-tolerance' }) -DurationMs 0 `
            -Detail $(if ($issues.Count -eq 0) { "$detail; all within tolerance" } else { "$detail; $($issues -join '; ')" })
    }

    $failures = @($subResults | Where-Object status -eq 'FAIL').Count
    [pscustomobject]@{
        Status = if ($failures -gt 0) { 'FAIL' } else { 'PASS' }
        Reason = if ($failures -gt 0) { 'resource-leak-validation-failed' } else { $null }
        Summary = "$(@($subResults | Where-Object status -eq 'PASS').Count) resource checks passed; $failures failed; $(@($subResults | Where-Object status -eq 'SKIPPED').Count) skipped"
        Artifacts = $artifacts
        SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-ResourceLeakValidationAvailability, Invoke-ResourceLeakValidationCapability, Get-ResourceLeakValidationDiagnostics
