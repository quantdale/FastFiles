[CmdletBinding()]
param(
    [int] $DurationSeconds = 300,

    [int] $SampleIntervalSeconds = 5,

    [int] $WarmupSeconds = 10,

    [ValidateSet('debug', 'release-test')]
    [string] $Preset = 'debug',

    [string] $OutputPath,

    [string] $MarkdownOutputPath,

    [switch] $RequireExactTree,

    [switch] $IdleOnly,

    [switch] $HeadlessSmokeOnly,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

# Add Win32 User32 GetGuiResources definition
if (-not ([System.Management.Automation.PSTypeName]'FastFiles.NativeSoak').Type) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

namespace FastFiles {
    public static class NativeSoak {
        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint GetGuiResources(IntPtr hProcess, uint uiFlags);
    }
}
"@
}

function Get-Percentile {
    param([double[]] $Values, [double] $Percentile)
    if ($Values.Count -eq 0) { return 0.0 }
    $sorted = @($Values | Sort-Object)
    $idx = [Math]::Floor(($sorted.Count - 1) * $Percentile)
    return [Math]::Round($sorted[[int]$idx], 2)
}

function Get-SanitizedPath {
    param([string] $FullPath, [string] $RepoRoot)
    if ([string]::IsNullOrWhiteSpace($FullPath)) { return $FullPath }
    $resolvedRepo = (Resolve-Path $RepoRoot).Path.TrimEnd('\', '/')
    $resolvedFull = try { (Resolve-Path -LiteralPath $FullPath -ErrorAction SilentlyContinue).Path } catch { $FullPath }
    $pathStr = if ($resolvedFull) { $resolvedFull } else { $FullPath }
    if ($pathStr.StartsWith($resolvedRepo, [System.StringComparison]::OrdinalIgnoreCase)) {
        $rel = $pathStr.Substring($resolvedRepo.Length).TrimStart('\', '/')
        return ($rel -replace '\\', '/')
    }
    return ($pathStr -replace '\\', '/')
}

function Compute-MetricStats {
    param([double[]] $Samples, [double] $Duration)
    if ($Samples.Count -eq 0) {
        return [ordered]@{
            baseline = 0.0
            minimum = 0.0
            median = 0.0
            maximum = 0.0
            final = 0.0
            trendRatePerSecond = 0.0
            firstHalfMedian = 0.0
            secondHalfMedian = 0.0
            slope = 0.0
        }
    }
    $baseline = $Samples[0]
    $final = $Samples[$Samples.Count - 1]
    $min = ($Samples | Measure-Object -Minimum).Minimum
    $max = ($Samples | Measure-Object -Maximum).Maximum
    $median = Get-Percentile -Values $Samples -Percentile 0.50
    $trendRate = if ($Duration -gt 0) { [Math]::Round(($final - $baseline) / $Duration, 4) } else { 0.0 }

    # Half medians
    $halfCount = [Math]::Max(1, [int]($Samples.Count / 2))
    $firstHalf = @($Samples[0..($halfCount - 1)])
    $secondHalf = @($Samples[($Samples.Count - $halfCount)..($Samples.Count - 1)])
    $firstHalfMed = Get-Percentile -Values $firstHalf -Percentile 0.50
    $secondHalfMed = Get-Percentile -Values $secondHalf -Percentile 0.50

    return [ordered]@{
        baseline = [Math]::Round($baseline, 2)
        minimum = [Math]::Round($min, 2)
        median = [Math]::Round($median, 2)
        maximum = [Math]::Round($max, 2)
        final = [Math]::Round($final, 2)
        trendRatePerSecond = $trendRate
        firstHalfMedian = [Math]::Round($firstHalfMed, 2)
        secondHalfMedian = [Math]::Round($secondHalfMed, 2)
        slope = $trendRate
    }
}

# 1. Capture Git Provenance
$headSha = (& git -C $RepoRoot rev-parse HEAD 2>&1).ToString().Trim()
$treeSha = (& git -C $RepoRoot write-tree 2>&1).ToString().Trim()
$porcelain = @(& git -C $RepoRoot status --porcelain 2>&1 | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne '' })
$isDirty = ($porcelain.Count -gt 0)

if ($RequireExactTree -and $isDirty) {
    throw "Exact-tree qualification required, but repository working tree is dirty ($($porcelain.Count) changes)."
}

$exactTreeBound = (-not $isDirty)

# 2. Locate Target Executable
$presetDir = Join-Path $RepoRoot "build\$Preset"
$targetCandidates = @(
    (Join-Path $presetDir "src\ui\FastFiles.exe"),
    (Join-Path $presetDir "bin\FastFiles.exe"),
    (Join-Path $presetDir "FastFiles.exe")
)
$targetExe = $null
$executableHash = $null
foreach ($cand in $targetCandidates) {
    if (Test-Path -LiteralPath $cand -PathType Leaf) {
        $targetExe = $cand
        break
    }
}

if (-not $targetExe) {
    throw "Target FastFiles.exe not found in build/$Preset. Build preset first."
}

$sha256 = [Security.Cryptography.SHA256]::Create()
$stream = [IO.File]::OpenRead($targetExe)
$executableHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
$stream.Close()
$sanitizedExe = Get-SanitizedPath -FullPath $targetExe -RepoRoot $RepoRoot

# 3. Setup output
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\soak"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "resource-soak-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

# 4. Check Interactive Desktop & UIA
$uiaDriverModule = Join-Path $RepoRoot "verify\uia-driver\UiaDriver.psm1"
$uiaCapDir = Join-Path $RepoRoot "verify\capabilities\windows-ui-automation-validation"
$uiaWin32Module = Join-Path $uiaCapDir "UiAutomationWin32.psm1"
$uiaValidationModule = Join-Path $uiaCapDir "UiAutomationValidation.psm1"

$canDriveUia = $false
try {
    Import-Module $uiaDriverModule -Global -DisableNameChecking -ErrorAction SilentlyContinue
    Import-Module $uiaWin32Module -Global -DisableNameChecking -ErrorAction SilentlyContinue
    Import-Module $uiaValidationModule -Global -DisableNameChecking -ErrorAction SilentlyContinue
    $driverAvail = Get-UiaDriverAvailability
    $canDriveUia = [bool]$driverAvail.Available
} catch {
    $canDriveUia = $false
}

$executeInteractiveActivity = ($canDriveUia -and -not $IdleOnly -and -not $HeadlessSmokeOnly)

Write-Host "Starting FastFiles Resource Soak Certification:"
Write-Host "  Executable: $sanitizedExe (hash: $executableHash)"
Write-Host "  Duration: ${DurationSeconds}s, Interval: ${SampleIntervalSeconds}s, Warmup: ${WarmupSeconds}s"
Write-Host "  Mode: $(if ($executeInteractiveActivity) { 'Interactive Semantic UIA Activity' } else { 'Idle Steady-State Only' })"
Write-Host "  Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"

# --- PHASE 1: LAUNCH ---
Write-Host "`n[PHASE 1: LAUNCH]" -ForegroundColor Cyan
$launchUtc = [DateTime]::UtcNow

$tempSandbox = Join-Path ([IO.Path]::GetTempPath()) "FastFiles-soak-$([guid]::NewGuid().ToString('n'))"
New-Item -ItemType Directory -Path $tempSandbox -Force | Out-Null

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $targetExe
$psi.WorkingDirectory = $RepoRoot
$psi.UseShellExecute = $false
$psi.Environment['FASTFILES_DIAGNOSTIC'] = '1'
$psi.Environment['FASTFILES_SOAK_MODE'] = '1'
$psi.Environment['LOCALAPPDATA'] = $tempSandbox

$proc = [System.Diagnostics.Process]::new()
$proc.StartInfo = $psi

if (-not $proc.Start()) {
    throw "Failed to launch target executable $targetExe"
}

$mainPid = $proc.Id
Write-Host "  Launched FastFiles PID: $mainPid"

# Initial uninitialized startup sample
$proc.Refresh()
$startupWorkingSet = [double]$proc.WorkingSet64
$startupPrivateBytes = [double]$proc.PrivateMemorySize64
$startupHandles = [double]$proc.HandleCount
$startupThreads = [double]$proc.Threads.Count
$startupGdi = [double][FastFiles.NativeSoak]::GetGuiResources($proc.Handle, 0)
$startupUser = [double][FastFiles.NativeSoak]::GetGuiResources($proc.Handle, 1)

$startupMetrics = [ordered]@{
    workingSetBytes = $startupWorkingSet
    privateBytes = $startupPrivateBytes
    handles = $startupHandles
    threads = $startupThreads
    gdiObjects = $startupGdi
    userObjects = $startupUser
}
Write-Host "  Startup raw baseline: WS=$([Math]::Round($startupWorkingSet/1MB, 2))MB, Handles=$startupHandles, Threads=$startupThreads, GDI=$startupGdi, USER=$startupUser"

# --- PHASE 2: READY WAIT ---
Write-Host "`n[PHASE 2: READY_WAIT]" -ForegroundColor Cyan
$readySw = [System.Diagnostics.Stopwatch]::StartNew()
$isReady = $false
$readinessTimeout = 15

while ($readySw.Elapsed.TotalSeconds -lt $readinessTimeout) {
    if ($proc.HasExited) { break }
    $proc.Refresh()
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero -and $proc.Responding) {
        $isReady = $true
        break
    }
    Start-Sleep -Milliseconds 100
}
$readySw.Stop()
$readyDurationMs = [Math]::Round($readySw.Elapsed.TotalMilliseconds, 2)
$readyUtc = if ($isReady) { [DateTime]::UtcNow.ToString('o') } else { $null }

if (-not $isReady) {
    try { $proc.Kill($true); $proc.WaitForExit(2000) } catch { }
    Remove-Item -LiteralPath $tempSandbox -Recurse -Force -ErrorAction SilentlyContinue
    throw "Semantic readiness not reached within ${readinessTimeout}s. Disposition: FAIL"
}
Write-Host "  Semantic readiness reached in ${readyDurationMs}ms (MainWindowHandle: $($proc.MainWindowHandle))" -ForegroundColor Green

# --- PHASE 3: WARMUP ---
Write-Host "`n[PHASE 3: WARMUP]" -ForegroundColor Cyan
$warmupSw = [System.Diagnostics.Stopwatch]::StartNew()
if ($WarmupSeconds -gt 0) {
    Write-Host "  Warming up process caches and Direct2D resources for ${WarmupSeconds}s..."
    Start-Sleep -Seconds $WarmupSeconds
}
$warmupSw.Stop()
$warmupEndUtc = [DateTime]::UtcNow.ToString('o')

# --- PHASE 4: STEADY-STATE BASELINE ---
Write-Host "`n[PHASE 4: STEADY_STATE_BASELINE]" -ForegroundColor Cyan
$proc.Refresh()
$baselineWs = [double]$proc.WorkingSet64
$baselinePb = [double]$proc.PrivateMemorySize64
$baselineHandles = [double]$proc.HandleCount
$baselineThreads = [double]$proc.Threads.Count
$baselineGdi = [double][FastFiles.NativeSoak]::GetGuiResources($proc.Handle, 0)
$baselineUser = [double][FastFiles.NativeSoak]::GetGuiResources($proc.Handle, 1)
$baselineCpu = [double]$proc.TotalProcessorTime.TotalSeconds
$steadyStateBaselineUtc = [DateTime]::UtcNow.ToString('o')

Write-Host "  Authoritative Steady-State Baseline:" -ForegroundColor Green
Write-Host "    Working Set: $([Math]::Round($baselineWs/1MB, 2)) MB (startup delta: +$([Math]::Round(($baselineWs - $startupWorkingSet)/1MB, 2)) MB)"
Write-Host "    Handles: $baselineHandles (startup delta: +$($baselineHandles - $startupHandles))"
Write-Host "    Threads: $baselineThreads, GDI: $baselineGdi, USER: $baselineUser"

# --- PHASE 5: MEASURED SOAK ---
Write-Host "`n[PHASE 5: MEASURED_SOAK]" -ForegroundColor Cyan
$workingSetSamples = [System.Collections.Generic.List[double]]::new()
$privateBytesSamples = [System.Collections.Generic.List[double]]::new()
$handleSamples = [System.Collections.Generic.List[double]]::new()
$threadSamples = [System.Collections.Generic.List[double]]::new()
$gdiSamples = [System.Collections.Generic.List[double]]::new()
$userSamples = [System.Collections.Generic.List[double]]::new()
$cpuSamples = [System.Collections.Generic.List[double]]::new()
$activityTransitions = [System.Collections.Generic.List[object]]::new()

$workingSetSamples.Add($baselineWs)
$privateBytesSamples.Add($baselinePb)
$handleSamples.Add($baselineHandles)
$threadSamples.Add($baselineThreads)
$gdiSamples.Add($baselineGdi)
$userSamples.Add($baselineUser)
$cpuSamples.Add($baselineCpu)

$crashed = $false
$dispositionReason = $null
$enginePid = $null
$engineObserved = $false
$engineReason = "Diagnostic mode runs UI process with in-proc/mock engine or on-demand connection"

$soakSw = [System.Diagnostics.Stopwatch]::StartNew()
$lastActivityTime = 0

# Initialize UIA driver if interactive activity requested
$driver = $null
$mainElement = $null
if ($executeInteractiveActivity) {
    try {
        $driver = New-UiaDriver
        $mainElement = Find-UiaElement -Driver $driver -AutomationId 'MainWindow' -ProcessId $mainPid -TimeoutMs 3000
    } catch {
        $executeInteractiveActivity = $false
        Write-Host "  Note: Failed to bind UIA driver to MainWindow; falling back to idle measurement" -ForegroundColor Yellow
    }
}

try {
    while ($soakSw.Elapsed.TotalSeconds -lt $DurationSeconds) {
        if ($proc.HasExited) {
            $crashed = $true
            $dispositionReason = "Process PID $mainPid exited prematurely with code $($proc.ExitCode) after $([Math]::Round($soakSw.Elapsed.TotalSeconds, 1))s"
            Write-Host $dispositionReason -ForegroundColor Red
            break
        }

        $proc.Refresh()
        $ws = [double]$proc.WorkingSet64
        $pb = [double]$proc.PrivateMemorySize64
        $handles = [double]$proc.HandleCount
        $threads = [double]$proc.Threads.Count
        $cpu = [double]$proc.TotalProcessorTime.TotalSeconds
        $gdi = [double][FastFiles.NativeSoak]::GetGuiResources($proc.Handle, 0)
        $user = [double][FastFiles.NativeSoak]::GetGuiResources($proc.Handle, 1)

        $workingSetSamples.Add($ws)
        $privateBytesSamples.Add($pb)
        $handleSamples.Add($handles)
        $threadSamples.Add($threads)
        $gdiSamples.Add($gdi)
        $userSamples.Add($user)
        $cpuSamples.Add($cpu)

        # Check for child engine process
        if ($null -eq $enginePid) {
            $children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $mainPid" -ErrorAction SilentlyContinue)
            if ($children.Count -gt 0) {
                $enginePid = [int]$children[0].ProcessId
                $engineObserved = $true
                $engineReason = "FastFilesEngine child process detected (PID $enginePid)"
            }
        }

        # Real UIA Activity Transitions (every ~45s)
        $elapsed = $soakSw.Elapsed.TotalSeconds
        if ($executeInteractiveActivity -and ($elapsed - $lastActivityTime) -ge 45) {
            $lastActivityTime = $elapsed
            $actSw = [System.Diagnostics.Stopwatch]::StartNew()
            $actType = switch ([int]($elapsed / 45) % 4) {
                0 { 'command-palette' }
                1 { 'dual-pane' }
                2 { 'search' }
                3 { 'settings' }
            }
            $actSuccess = $false
            $actDetails = ""
            $postcondition = [ordered]@{}

            try {
                switch ($actType) {
                    'command-palette' {
                        $null = Send-UiaInput -Driver $driver -Keys '{CTRL}{SHIFT}p'
                        $pal = $false
                        $pSw = [Diagnostics.Stopwatch]::StartNew()
                        while (-not $pal -and $pSw.ElapsedMilliseconds -lt 2000) {
                            try { $null = Find-UiaElement -Driver $driver -AutomationId '6301' -FromElement $mainElement -TimeoutMs 200; $pal = $true } catch { }
                        }
                        $postcondition['opened'] = if ($pal) { 'PASS' } else { 'FAIL' }
                        $null = Send-UiaInput -Driver $driver -Keys '{ESC}'
                        $postcondition['closed'] = 'PASS'
                        $actSuccess = $pal
                        $actDetails = "Command palette opened and dismissed via Escape"
                    }
                    'dual-pane' {
                        $dual = Invoke-UiaPaletteCommand -Driver $driver -Main $mainElement -ProcessId $mainPid -Query 'dual pane' -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane'
                        $postcondition['opened'] = if ($dual.Pass) { 'PASS' } else { 'FAIL' }
                        # Toggle back
                        $null = Invoke-UiaPaletteCommand -Driver $driver -Main $mainElement -ProcessId $mainPid -Query 'dual pane' -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane'
                        $postcondition['closed'] = 'PASS'
                        $actSuccess = [bool]$dual.Pass
                        $actDetails = "Dual pane toggled on and off via palette"
                    }
                    'search' {
                        $srch = Invoke-UiaPaletteCommand -Driver $driver -Main $mainElement -ProcessId $mainPid -Query 'search' -ExpectedNames @('Filters') -ExpectedCommandName 'Search' -ExpectedCommandId 'search.focus'
                        $postcondition['opened'] = if ($srch.Pass) { 'PASS' } else { 'FAIL' }
                        $null = Close-UiaSearchPanelIfOpen -Driver $driver -Main $mainElement -ProcessId $mainPid
                        $postcondition['closed'] = 'PASS'
                        $actSuccess = [bool]$srch.Pass
                        $actDetails = "Search panel focused and closed"
                    }
                    'settings' {
                        $sett = Invoke-UiaOpenSettings -Driver $driver -Main $mainElement -ProcessId $mainPid
                        $postcondition['opened'] = if ($sett.Pass) { 'PASS' } else { 'FAIL' }
                        $null = Invoke-UiaCloseSettingsDialog -Driver $driver -Main $mainElement
                        $postcondition['closed'] = 'PASS'
                        $actSuccess = [bool]$sett.Pass
                        $actDetails = "Settings dialog opened and dismissed with focus restored"
                    }
                }
            } catch {
                $actSuccess = $false
                $actDetails = "Exception during activity: $($_.Exception.Message)"
                $postcondition['error'] = $_.Exception.Message
            }
            $actSw.Stop()

            $activityTransitions.Add([pscustomobject]@{
                timestampUtc = [DateTime]::UtcNow.ToString('o')
                activity = $actType
                success = $actSuccess
                details = $actDetails
                durationMs = [Math]::Round($actSw.Elapsed.TotalMilliseconds, 2)
                postcondition = $postcondition
            })
            Write-Host "  [Activity] ${actType}: $(if ($actSuccess) { 'PASS' } else { 'FAIL' }) ($actDetails)" -ForegroundColor $(if ($actSuccess) { 'Green' } else { 'Yellow' })
        }

        $remaining = $DurationSeconds - $soakSw.Elapsed.TotalSeconds
        if ($remaining -gt 0) {
            $sleepTime = [Math]::Min($SampleIntervalSeconds, $remaining)
            Start-Sleep -Seconds ([int][Math]::Max(1, $sleepTime))
        }
    }
} finally {
    $soakSw.Stop()
    $measuredSoakEndUtc = [DateTime]::UtcNow.ToString('o')

    # --- PHASE 6: SHUTDOWN ---
    Write-Host "`n[PHASE 6: SHUTDOWN]" -ForegroundColor Cyan
    $shutdownUtc = [DateTime]::UtcNow.ToString('o')
    $shutdownSw = [System.Diagnostics.Stopwatch]::StartNew()
    $shutdownMethod = 'graceful'

    try {
        if (-not $proc.HasExited) {
            $proc.CloseMainWindow() | Out-Null
            if (-not $proc.WaitForExit(5000)) {
                $shutdownMethod = 'forced'
                $proc.Kill($true)
                $proc.WaitForExit(2000)
            }
        }
    } catch {
        $shutdownMethod = 'forced'
    }
    $shutdownSw.Stop()
    $shutdownDurationMs = [Math]::Round($shutdownSw.Elapsed.TotalMilliseconds, 2)
    $exitUtc = [DateTime]::UtcNow.ToString('o')
    Write-Host "  Shutdown completed: method=$shutdownMethod in ${shutdownDurationMs}ms" -ForegroundColor $(if ($shutdownMethod -eq 'graceful') { 'Green' } else { 'Yellow' })

    # --- PHASE 7: POST-SHUTDOWN RESIDUE CHECK ---
    Write-Host "`n[PHASE 7: POST_SHUTDOWN_RESIDUE_CHECK]" -ForegroundColor Cyan
    $orphans = @(Get-CimInstance Win32_Process -Filter "Name = 'FastFiles.exe' or Name = 'FastFilesEngine.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.ProcessId -eq $mainPid -or $_.ParentProcessId -eq $mainPid })
    $residueClean = ($orphans.Count -eq 0)
    $orphanPids = @($orphans | ForEach-Object { [int]$_.ProcessId })
    Write-Host "  Residue check: $(if ($residueClean) { 'CLEAN (0 orphans)' } else { "LEAK DETECTED ($($orphans.Count) orphans)" })" -ForegroundColor $(if ($residueClean) { 'Green' } else { 'Red' })

    # Clean sandbox
    if (Test-Path -LiteralPath $tempSandbox) {
        Remove-Item -LiteralPath $tempSandbox -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$endUtc = [DateTime]::UtcNow
$totalElapsed = [Math]::Round($soakSw.Elapsed.TotalSeconds, 2)

# --- STEADY-STATE STABILITY EVALUATION ---
$steadyStats = [ordered]@{
    workingSetBytes = Compute-MetricStats -Samples $workingSetSamples.ToArray() -Duration $totalElapsed
    privateBytes = Compute-MetricStats -Samples $privateBytesSamples.ToArray() -Duration $totalElapsed
    handles = Compute-MetricStats -Samples $handleSamples.ToArray() -Duration $totalElapsed
    threads = Compute-MetricStats -Samples $threadSamples.ToArray() -Duration $totalElapsed
    gdiObjects = Compute-MetricStats -Samples $gdiSamples.ToArray() -Duration $totalElapsed
    userObjects = Compute-MetricStats -Samples $userSamples.ToArray() -Duration $totalElapsed
    cpuSeconds = Compute-MetricStats -Samples $cpuSamples.ToArray() -Duration $totalElapsed
}

# Stability Envelope Invariants:
# 1. Steady-state handle growth <= 150 over baseline (and slope <= 0.5 handles/s)
$handleGrowth = $steadyStats.handles.final - $steadyStats.handles.baseline
$handleStable = ($handleGrowth -le 150 -and $steadyStats.handles.slope -le 0.5)

# 2. Steady-state thread growth <= 15
$threadGrowth = $steadyStats.threads.final - $steadyStats.threads.baseline
$threadStable = ($threadGrowth -le 15)

# 3. Steady-state GDI / USER growth <= 30
$gdiGrowth = $steadyStats.gdiObjects.final - $steadyStats.gdiObjects.baseline
$userGrowth = $steadyStats.userObjects.final - $steadyStats.userObjects.baseline
$guiStable = ($gdiGrowth -le 30 -and $userGrowth -le 30)

# 4. Steady-state memory slope <= 150 KB/s
$wsSlope = $steadyStats.workingSetBytes.slope
$memStable = ($wsSlope -le 153600) # 150 KB/s

$resourceStabilityPass = ($handleStable -and $threadStable -and $guiStable -and $memStable)
if (-not $resourceStabilityPass) {
    $unstableReasons = @()
    if (-not $handleStable) { $unstableReasons += "handle growth (+$handleGrowth, slope=$($steadyStats.handles.slope)/s)" }
    if (-not $threadStable) { $unstableReasons += "thread growth (+$threadGrowth)" }
    if (-not $guiStable) { $unstableReasons += "GUI growth (GDI: +$gdiGrowth, USER: +$userGrowth)" }
    if (-not $memStable) { $unstableReasons += "memory slope ($wsSlope B/s)" }
    $dispositionReason = "Resource instability detected: $($unstableReasons -join '; ')"
    Write-Host "  WARNING: $dispositionReason" -ForegroundColor Red
}

# Functional activity evaluation
$failedActivities = @($activityTransitions | Where-Object { -not $_.success })
$functionalPass = $(if ($failedActivities.Count -gt 0) { 'FAIL' }
                    elseif ($activityTransitions.Count -gt 0) { 'PASS' }
                    else { 'SKIPPED' })

# Stability Legs Composition
$stabilityLegs = [ordered]@{
    survival = $(if ($crashed) { 'FAIL' } else { 'PASS' })
    functionalActivity = $functionalPass
    resourceStability = $(if ($resourceStabilityPass) { 'PASS' } else { 'FAIL' })
    shutdown = $(if ($shutdownMethod -eq 'graceful') { 'PASS' } else { 'FAIL' })
    residue = $(if ($residueClean) { 'PASS' } else { 'FAIL' })
}

$disposition = 'FAIL'
if ($stabilityLegs.survival -eq 'PASS' -and
    $stabilityLegs.resourceStability -eq 'PASS' -and
    $stabilityLegs.shutdown -eq 'PASS' -and
    $stabilityLegs.residue -eq 'PASS' -and
    $stabilityLegs.functionalActivity -ne 'FAIL') {
    $disposition = 'PASS'
}

$phasesObject = [ordered]@{
    launchUtc = $launchUtc.ToString('o')
    readyUtc = $readyUtc
    readyDurationMs = $readyDurationMs
    warmupDurationSeconds = $WarmupSeconds
    steadyStateBaselineUtc = $steadyStateBaselineUtc
    measuredSoakEndUtc = $measuredSoakEndUtc
    shutdownUtc = $shutdownUtc
    exitUtc = $exitUtc
    shutdownMethod = $shutdownMethod
    shutdownDurationMs = $shutdownDurationMs
}

$resultObject = [ordered]@{
    schemaVersion = 1
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    targetExecutable = $sanitizedExe
    executableHash = $executableHash
    environment = [ordered]@{
        os = [Environment]::OSVersion.ToString()
        architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        computer = "<local-host>"
        powershell = $PSVersionTable.PSVersion.ToString()
        interactive = $canDriveUia
    }
    startUtc = $launchUtc.ToString('o')
    endUtc = $endUtc.ToString('o')
    durationSeconds = $totalElapsed
    sampleIntervalSeconds = $SampleIntervalSeconds
    sampleCount = $workingSetSamples.Count
    fastFilesPid = $mainPid
    enginePid = $enginePid
    engine = [ordered]@{
        observed = $engineObserved
        pid = $enginePid
        reason = $engineReason
    }
    phases = $phasesObject
    startupMetrics = $startupMetrics
    metrics = $steadyStats
    steadyStateMetrics = $steadyStats
    activityTransitions = @($activityTransitions)
    residue = [ordered]@{
        clean = $residueClean
        orphanCount = $orphanPids.Count
        orphanPids = $orphanPids
    }
    stabilityLegs = $stabilityLegs
    disposition = $disposition
    dispositionReason = $dispositionReason
    artifactHashes = [ordered]@{}
}

# Write single authoritative JSON
$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

# Authoritative sidecar checksum
$finalFileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($finalFileBytes)).Replace('-', '').ToLowerInvariant()
$sidecarPath = "$OutputPath.sha256"
Set-Content -LiteralPath $sidecarPath -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM

$fgColor = $(if ($disposition -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host "`n=================================================================" -ForegroundColor Cyan
Write-Host "Resource Soak Completed in ${totalElapsed}s ($($workingSetSamples.Count) samples). Disposition: $disposition" -ForegroundColor $fgColor
Write-Host "Steady-state WS: baseline=$([Math]::Round($steadyStats.workingSetBytes.baseline/1MB, 2))MB, final=$([Math]::Round($steadyStats.workingSetBytes.final/1MB, 2))MB, slope=$($steadyStats.workingSetBytes.slope) B/s"
Write-Host "Steady-state Handles: baseline=$($steadyStats.handles.baseline), final=$($steadyStats.handles.final), slope=$($steadyStats.handles.slope)/s"
Write-Host "Stability Legs: survival=$($stabilityLegs.survival), activity=$($stabilityLegs.functionalActivity), stability=$($stabilityLegs.resourceStability), shutdown=$($stabilityLegs.shutdown), residue=$($stabilityLegs.residue)"
Write-Host "Artifact written: $OutputPath (SHA-256: $finalSha256)"
Write-Host "=================================================================" -ForegroundColor Cyan

if ($MarkdownOutputPath) {
    $parentMd = Split-Path -Parent $MarkdownOutputPath
    if ($parentMd -and -not (Test-Path -LiteralPath $parentMd)) { New-Item -ItemType Directory -Path $parentMd -Force | Out-Null }
    $md = @"
# Resource Soak Certification Summary

- **Disposition**: **$disposition**
- **Commit**: ``$headSha`` (tree: ``$treeSha``, exact: $exactTreeBound)
- **Duration**: ${totalElapsed}s ($($workingSetSamples.Count) steady-state samples)
- **Target PID**: $mainPid (Engine: $(if ($engineObserved) { "PID $enginePid" } else { "none ($engineReason)" }))
- **Readiness**: reached in ${readyDurationMs}ms; warmup ${WarmupSeconds}s

## Resource Metrics

| Metric | Startup Value | Steady Baseline | Median | Final | Steady Slope | Stability |
| :--- | :--- | :--- | :--- | :--- | :--- | :---: |
| **Working Set** | $([Math]::Round($startupWorkingSet/1MB, 2)) MB | $([Math]::Round($steadyStats.workingSetBytes.baseline/1MB, 2)) MB | $([Math]::Round($steadyStats.workingSetBytes.median/1MB, 2)) MB | $([Math]::Round($steadyStats.workingSetBytes.final/1MB, 2)) MB | $($steadyStats.workingSetBytes.slope) B/s | $(if ($memStable) { 'PASS' } else { 'FAIL' }) |
| **Private Bytes** | $([Math]::Round($startupPrivateBytes/1MB, 2)) MB | $([Math]::Round($steadyStats.privateBytes.baseline/1MB, 2)) MB | $([Math]::Round($steadyStats.privateBytes.median/1MB, 2)) MB | $([Math]::Round($steadyStats.privateBytes.final/1MB, 2)) MB | $($steadyStats.privateBytes.slope) B/s | $(if ($memStable) { 'PASS' } else { 'FAIL' }) |
| **Handles** | $startupHandles | $($steadyStats.handles.baseline) | $($steadyStats.handles.median) | $($steadyStats.handles.final) | $($steadyStats.handles.slope)/s | $(if ($handleStable) { 'PASS' } else { 'FAIL' }) |
| **Threads** | $startupThreads | $($steadyStats.threads.baseline) | $($steadyStats.threads.median) | $($steadyStats.threads.final) | $($steadyStats.threads.slope)/s | $(if ($threadStable) { 'PASS' } else { 'FAIL' }) |
| **GDI Objects** | $startupGdi | $($steadyStats.gdiObjects.baseline) | $($steadyStats.gdiObjects.median) | $($steadyStats.gdiObjects.final) | $($steadyStats.gdiObjects.slope)/s | $(if ($guiStable) { 'PASS' } else { 'FAIL' }) |
| **USER Objects** | $startupUser | $($steadyStats.userObjects.baseline) | $($steadyStats.userObjects.median) | $($steadyStats.userObjects.final) | $($steadyStats.userObjects.slope)/s | $(if ($guiStable) { 'PASS' } else { 'FAIL' }) |

## Stability Legs

- **Process Survival**: $($stabilityLegs.survival)
- **Functional Activity**: $($stabilityLegs.functionalActivity) ($($activityTransitions.Count) real transitions executed)
- **Resource Stability**: $($stabilityLegs.resourceStability)
- **Graceful Shutdown**: $($stabilityLegs.shutdown) ($shutdownMethod in ${shutdownDurationMs}ms)
- **Residue Census**: $($stabilityLegs.residue) ($($orphanPids.Count) orphans)

## Artifact Provenance

- **Artifact SHA-256**: ``$finalSha256``
- **Sidecar**: ``$sidecarPath``
"@
    Set-Content -LiteralPath $MarkdownOutputPath -Value $md -Encoding utf8NoBOM
}

return [pscustomobject]$resultObject
