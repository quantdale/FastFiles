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

        # Real UIA Activity Transitions (every ~45s). Each transition carries
        # explicit semantic postconditions: open is proved by element
        # appearance, close is proved by actual element disappearance or
        # provider restoration. A sent Escape (or any dispatched keystroke)
        # alone is never recorded as 'closed'. A failed required transition
        # fails the functional leg (success stays $false).
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
                        $palEl = Wait-UiaElement -Driver $driver -AutomationId '6301' -FromElement $mainElement -TimeoutMs 2000
                        $palOpened = ($null -ne $palEl)
                        if ($palOpened) { $postcondition['opened'] = 'PASS' }
                        else { $postcondition['opened'] = 'FAIL: command palette edit 6301 did not appear after Ctrl+Shift+P' }
                        $null = Send-UiaInput -Driver $driver -Keys '{ESC}'
                        $palGone = Wait-UiaElementGone -Driver $driver -AutomationId '6301' -FromElement $mainElement -TimeoutMs 3000
                        $postcondition['closed'] = $(if ($palGone) { 'PASS' } else { 'FAIL: command palette edit 6301 still present after Escape; close not verified' })
                        $actSuccess = ($palOpened -and $palGone)
                        $actDetails = "Command palette open verified by edit appearance and close verified by edit disappearance"
                    }
                    'dual-pane' {
                        $dual = Invoke-UiaPaletteCommand -Driver $driver -Main $mainElement -ProcessId $mainPid -Query 'dual pane' -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane'
                        $dualCmdOk = [bool]$dual.Pass
                        if ($dualCmdOk) { $postcondition['command'] = 'PASS' }
                        else { $postcondition['command'] = "FAIL: dual pane enable command failed: $($dual.Detail)" }
                        $pane0On = $false
                        $pane1On = $false
                        try { $null = Find-UiaElement -Driver $driver -AutomationId 'Pane.0' -FromElement $mainElement -TimeoutMs 2000; $pane0On = $true } catch { }
                        try { $null = Find-UiaElement -Driver $driver -AutomationId 'Pane.1' -FromElement $mainElement -TimeoutMs 2000; $pane1On = $true } catch { }
                        $enabledCount = 0
                        if ($pane0On) { $enabledCount++ }
                        if ($pane1On) { $enabledCount++ }
                        $enabledOk = (($enabledCount -eq 2) -and $pane0On -and $pane1On)
                        if ($enabledOk) { $postcondition['pane-count-2'] = 'PASS' }
                        else { $postcondition['pane-count-2'] = "FAIL: dual-pane enable postcondition not met: pane count=$enabledCount (expected 2), Pane.0 present=$pane0On, Pane.1 present=$pane1On" }
                        # Toggle back to single pane and prove restoration.
                        $dualOff = Invoke-UiaPaletteCommand -Driver $driver -Main $mainElement -ProcessId $mainPid -Query 'dual pane' -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane'
                        $dualOffOk = [bool]$dualOff.Pass
                        $pane0Back = $false
                        $pane1StillThere = $true
                        try { $null = Find-UiaElement -Driver $driver -AutomationId 'Pane.0' -FromElement $mainElement -TimeoutMs 2000; $pane0Back = $true } catch { }
                        try { $null = Find-UiaElement -Driver $driver -AutomationId 'Pane.1' -FromElement $mainElement -TimeoutMs 800; $pane1StillThere = $true } catch { $pane1StillThere = $false }
                        $restoredOk = ($dualOffOk -and $pane0Back -and (-not $pane1StillThere))
                        $postcondition['closed'] = $(if ($restoredOk) { 'PASS' } else { "FAIL: single-pane restoration not verified: disable command Pass=$dualOffOk, Pane.0 present=$pane0Back (expected True), Pane.1 present=$pane1StillThere (expected False)" })
                        $actSuccess = ($dualCmdOk -and $enabledOk -and $restoredOk)
                        $actDetails = "Dual pane enable proved by Pane.0+Pane.1 presence and disable proved by Pane.1 absence with Pane.0 restored"
                    }
                    'search' {
                        $srch = Invoke-UiaPaletteCommand -Driver $driver -Main $mainElement -ProcessId $mainPid -Query 'search' -ExpectedNames @('Filters') -ExpectedCommandName 'Search' -ExpectedCommandId 'search.focus'
                        $searchCmdOk = [bool]$srch.Pass
                        $searchSurface = $false
                        if ($searchCmdOk) {
                            try { $null = Find-UiaElement -Driver $driver -AutomationId 'Search.Results' -FromElement $mainElement -TimeoutMs 2000; $searchSurface = $true } catch { }
                        }
                        $searchOpened = ($searchCmdOk -and $searchSurface)
                        if ($searchOpened) { $postcondition['opened'] = 'PASS' }
                        else { $postcondition['opened'] = "FAIL: Search did not open (command Pass=$searchCmdOk, Search.Results present=$searchSurface): $($srch.Detail)" }
                        $filtersBtn = $null
                        if ($searchOpened) {
                            try { $filtersBtn = Find-UiaElement -Driver $driver -AutomationId 'Search.Filters' -FromElement $mainElement -TimeoutMs 2000 } catch { }
                            if ($null -eq $filtersBtn) {
                                try { $filtersBtn = Find-UiaElement -Driver $driver -Name 'Filters' -FromElement $mainElement -TimeoutMs 2000 } catch { }
                            }
                        }
                        $filtersFound = ($null -ne $filtersBtn)
                        if ($filtersFound) { $postcondition['filters-disclosure'] = 'PASS' }
                        else { $postcondition['filters-disclosure'] = "FAIL: 'Filters' disclosure not found after Search opened" }
                        $contentsOk = $false
                        if ($filtersFound) {
                            $expandState = ''
                            try {
                                if (Test-UiaPatternAvailable -Driver $driver -Element $filtersBtn -PatternName 'ExpandCollapse') {
                                    $expandState = [string](Invoke-UiaPattern -Driver $driver -Element $filtersBtn -PatternName 'ExpandCollapse' -Method 'GetState')
                                }
                            } catch { }
                            if ($expandState -ne 'Expanded') {
                                try {
                                    if (Test-UiaPatternAvailable -Driver $driver -Element $filtersBtn -PatternName 'ExpandCollapse') {
                                        $null = Invoke-UiaPattern -Driver $driver -Element $filtersBtn -PatternName 'ExpandCollapse' -Method 'Expand'
                                    } else {
                                        $null = Invoke-UiaElementAction -Driver $driver -Element $filtersBtn -Action 'Click'
                                    }
                                } catch { }
                                Start-Sleep -Milliseconds 500
                            }
                            $clearFound = $false
                            $ascFound = $false
                            try { $null = Find-UiaElement -Driver $driver -Name 'Clear history' -FromElement $mainElement -TimeoutMs 1500; $clearFound = $true } catch { }
                            try { $null = Find-UiaElement -Driver $driver -Name 'Ascending' -FromElement $mainElement -TimeoutMs 1500; $ascFound = $true } catch { }
                            $contentsOk = ($clearFound -and $ascFound)
                            if ($contentsOk) { $postcondition['filters-contents'] = 'PASS' }
                            else { $postcondition['filters-contents'] = "FAIL: expanded 'Filters' row missing expected contents: 'Clear history' present=$clearFound, 'Ascending' present=$ascFound" }
                        }
                        $closeRes = Close-UiaSearchPanelIfOpen -Driver $driver -Main $mainElement -ProcessId $mainPid
                        $searchGone = $false
                        try { $null = Find-UiaElement -Driver $driver -AutomationId 'Search.Results' -FromElement $mainElement -TimeoutMs 800 } catch { $searchGone = $true }
                        $closeOk = ([bool]$closeRes.Pass -and $searchGone)
                        $postcondition['closed'] = $(if ($closeOk) { 'PASS' } else { "FAIL: Search panel close not verified (closer Pass=$($closeRes.Pass), Search.Results absent=$searchGone): $($closeRes.Detail)" })
                        $actSuccess = ($searchOpened -and $filtersFound -and $contentsOk -and $closeOk)
                        $actDetails = "Search panel open proved by Search.Results, Filters disclosure expanded with expected contents, close proved by Search.Results absence"
                    }
                    'settings' {
                        $sett = Invoke-UiaOpenSettings -Driver $driver -Main $mainElement -ProcessId $mainPid
                        $settingsOpened = [bool]$sett.Pass
                        if ($settingsOpened) { $postcondition['opened'] = 'PASS' }
                        else { $postcondition['opened'] = "FAIL: settings dialog did not open: $($sett.Detail)" }
                        $settingsClose = Invoke-UiaCloseSettingsDialog -Driver $driver -Main $mainElement -ProcessId $mainPid
                        $settingsClosed = [bool]$settingsClose.Pass
                        $postcondition['closed'] = $(if ($settingsClosed) { 'PASS' } else { "FAIL: settings dialog close not verified (dialog still present or cancel failed): $($settingsClose.Detail)" })
                        $actSuccess = ($settingsOpened -and $settingsClosed)
                        $actDetails = "Settings dialog open proved by dialog appearance and close proved by verified dismissal with focus restored"
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

# Stability Envelope Invariants (steady-state, post-warmup):
# 1. Steady-state handles: fast path (growth <= 150 over baseline and
#    slope <= 0.5 handles/s) OR settled path (second-half median drift
#    <= 25). The settled path exists because first-open UI surface caches
#    (Direct2D, fonts, imagelists) step once and then hold flat
#    indefinitely -- proven bounded by flat second-half medians plus
#    repeated open/close cycling with zero per-cycle growth (NV-011). A
#    genuine leak keeps drifting in the second half, so the settled path
#    still fails it; the fast path is unchanged.
$handleGrowth = $steadyStats.handles.final - $steadyStats.handles.baseline
$handleHalfDrift = $steadyStats.handles.secondHalfMedian - $steadyStats.handles.firstHalfMedian
$handleFastOk = ($handleGrowth -le 150 -and $steadyStats.handles.slope -le 0.5)
$handleSettled = ($handleHalfDrift -le 25)
$handleStable = ($handleFastOk -or $handleSettled)

# 2. Steady-state thread growth <= 15
$threadGrowth = $steadyStats.threads.final - $steadyStats.threads.baseline
$threadStable = ($threadGrowth -le 15)

# 3. Steady-state GDI / USER growth <= 30
$gdiGrowth = $steadyStats.gdiObjects.final - $steadyStats.gdiObjects.baseline
$userGrowth = $steadyStats.userObjects.final - $steadyStats.userObjects.baseline
$guiStable = ($gdiGrowth -le 30 -and $userGrowth -le 30)

# 4. Steady-state working-set slope <= 150 KB/s. The working set is a
#    shared-address-space metric: paged-out memory can mask real growth, so
#    this verdict NEVER substitutes for the private-bytes gate below.
$wsSlope = $steadyStats.workingSetBytes.slope
$wsStable = ($wsSlope -le 153600) # 150 KB/s

# 5. SEPARATE private-bytes stability gate (committed-memory leak detector).
#    Private bytes cannot be paged away, so a heap/VirtualAlloc leak is
#    visible here even when the working set looks flat. Documented
#    thresholds, independent of the working-set verdict:
#      (a) slope <= 150 KB/s (same rate ceiling as working set);
#      (b) baseline -> final growth <= 50 MB;
#      (c) second-half median minus first-half median <= 25 MB (catches slow
#          ramps whose endpoints still look close together).
#    All three sub-conditions must hold; any violation fails the gate.
$pbStats = $steadyStats.privateBytes
$pbSlope = $pbStats.slope
$pbGrowth = $pbStats.final - $pbStats.baseline
$pbHalfDrift = $pbStats.secondHalfMedian - $pbStats.firstHalfMedian
$pbSlopeOk = ($pbSlope -le 153600) # 150 KB/s
$pbGrowthOk = ($pbGrowth -le 52428800) # 50 MB
$pbDriftOk = ($pbHalfDrift -le 26214400) # 25 MB
$privateBytesStable = ($pbSlopeOk -and $pbGrowthOk -and $pbDriftOk)

$resourceStabilityPass = ($handleStable -and $threadStable -and $guiStable -and $wsStable -and $privateBytesStable)
if (-not $resourceStabilityPass) {
    $unstableReasons = @()
    if (-not $handleStable) { $unstableReasons += "handle growth (+$handleGrowth, slope=$($steadyStats.handles.slope)/s, second-half drift=$handleHalfDrift)" }
    if (-not $threadStable) { $unstableReasons += "thread growth (+$threadGrowth)" }
    if (-not $guiStable) { $unstableReasons += "GUI growth (GDI: +$gdiGrowth, USER: +$userGrowth)" }
    if (-not $wsStable) { $unstableReasons += "working-set slope ($wsSlope B/s exceeds 150 KB/s)" }
    if (-not $privateBytesStable) {
        $pbReasons = @()
        if (-not $pbSlopeOk) { $pbReasons += "slope $pbSlope B/s exceeds 150 KB/s" }
        if (-not $pbGrowthOk) { $pbReasons += "baseline-to-final growth $([Math]::Round($pbGrowth/1MB, 2)) MB exceeds 50 MB" }
        if (-not $pbDriftOk) { $pbReasons += "second-half drift $([Math]::Round($pbHalfDrift/1MB, 2)) MB exceeds 25 MB" }
        $unstableReasons += "private-bytes instability ($($pbReasons -join '; '))"
    }
    $dispositionReason = "Resource instability detected: $($unstableReasons -join '; ')"
    Write-Host "  WARNING: $dispositionReason" -ForegroundColor Red
}
# Functional activity evaluation. Explicit soak modes: idle modes perform no
# transitions, so functionalActivity=SKIPPED is the valid idle result. Any
# mode that executes transitions must prove each one; a failed required
# transition fails the functional leg.
$failedActivities = @($activityTransitions | Where-Object { -not $_.success })
$functionalPass = $(if ($failedActivities.Count -gt 0) { 'FAIL' }
                    elseif ($activityTransitions.Count -gt 0) { 'PASS' }
                    else { 'SKIPPED' })
$soakMode = 'idle-fallback-no-uia'
if ($IdleOnly) { $soakMode = 'idle-only' }
elseif ($HeadlessSmokeOnly) { $soakMode = 'headless-smoke-only' }
elseif ($executeInteractiveActivity) { $soakMode = 'interactive' }
if ((($soakMode -eq 'idle-only') -or ($soakMode -eq 'headless-smoke-only') -or ($soakMode -eq 'idle-fallback-no-uia')) -and ($activityTransitions.Count -eq 0)) {
    $functionalPass = 'SKIPPED'
}

# Cleanup census: the campaign-owned sandbox must be gone and no owned
# process may remain. A cleanup failure forces FAIL, never hidden.
$sandboxRemoved = -not (Test-Path -LiteralPath $tempSandbox)
$cleanupPass = ($residueClean -and $sandboxRemoved)
$cleanupStatus = 'FAIL'
if ($cleanupPass) { $cleanupStatus = 'PASS' }
$cleanupResult = [ordered]@{
    pass = $cleanupPass
    status = $cleanupStatus
    method = 'graceful-close-then-verify'
    sandboxRemoved = $sandboxRemoved
    shutdownMethod = $shutdownMethod
    orphanCount = $orphanPids.Count
    orphanPids = @($orphanPids)
}
if (-not $cleanupPass) {
    $cleanupNote = "Cleanup census failed (sandboxRemoved=$sandboxRemoved, orphans=$($orphanPids.Count))"
    if ($dispositionReason) { $dispositionReason = "$dispositionReason; $cleanupNote" }
    else { $dispositionReason = $cleanupNote }
    Write-Host "  WARNING: $cleanupNote" -ForegroundColor Red
}

# Stability Legs Composition
$stabilityLegs = [ordered]@{
    survival = $(if ($crashed) { 'FAIL' } else { 'PASS' })
    functionalActivity = $functionalPass
    resourceStability = $(if ($resourceStabilityPass) { 'PASS' } else { 'FAIL' })
    shutdown = $(if ($shutdownMethod -eq 'graceful') { 'PASS' } else { 'FAIL' })
    residue = $(if ($residueClean) { 'PASS' } else { 'FAIL' })
    cleanup = $cleanupStatus
}

$disposition = 'FAIL'
if ($stabilityLegs.survival -eq 'PASS' -and
    $stabilityLegs.resourceStability -eq 'PASS' -and
    $stabilityLegs.shutdown -eq 'PASS' -and
    $stabilityLegs.residue -eq 'PASS' -and
    $stabilityLegs.cleanup -eq 'PASS' -and
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
    mode = $soakMode
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
    cleanup = $cleanupResult
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
Write-Host "Steady-state PrivateBytes: baseline=$([Math]::Round($steadyStats.privateBytes.baseline/1MB, 2))MB, final=$([Math]::Round($steadyStats.privateBytes.final/1MB, 2))MB, slope=$($steadyStats.privateBytes.slope) B/s, growth=$([Math]::Round($pbGrowth/1MB, 2))MB, halfDrift=$([Math]::Round($pbHalfDrift/1MB, 2))MB"
Write-Host "Steady-state Handles: baseline=$($steadyStats.handles.baseline), final=$($steadyStats.handles.final), slope=$($steadyStats.handles.slope)/s, second-half drift=$handleHalfDrift"
Write-Host "Stability Legs: survival=$($stabilityLegs.survival), activity=$($stabilityLegs.functionalActivity), stability=$($stabilityLegs.resourceStability), shutdown=$($stabilityLegs.shutdown), residue=$($stabilityLegs.residue), cleanup=$($stabilityLegs.cleanup)"
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
| **Working Set** | $([Math]::Round($startupWorkingSet/1MB, 2)) MB | $([Math]::Round($steadyStats.workingSetBytes.baseline/1MB, 2)) MB | $([Math]::Round($steadyStats.workingSetBytes.median/1MB, 2)) MB | $([Math]::Round($steadyStats.workingSetBytes.final/1MB, 2)) MB | $($steadyStats.workingSetBytes.slope) B/s | $(if ($wsStable) { 'PASS' } else { 'FAIL' }) |
| **Private Bytes** | $([Math]::Round($startupPrivateBytes/1MB, 2)) MB | $([Math]::Round($steadyStats.privateBytes.baseline/1MB, 2)) MB | $([Math]::Round($steadyStats.privateBytes.median/1MB, 2)) MB | $([Math]::Round($steadyStats.privateBytes.final/1MB, 2)) MB | $($steadyStats.privateBytes.slope) B/s | $(if ($privateBytesStable) { 'PASS' } else { 'FAIL' }) |
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
