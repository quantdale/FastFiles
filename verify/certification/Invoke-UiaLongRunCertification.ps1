[CmdletBinding()]
param(
    [ValidateSet('application-lifecycle', 'rapid-input', 'core-smoke', 'visual-matrix')]
    [string] $Scenario = 'core-smoke',

    [int] $Iterations = 0,

    [int] $TimeoutSecondsPerIteration = 60,
    [ValidateSet('debug', 'release-test')]
    [string] $Preset = 'debug',

    [ValidateSet('Light', 'Dark')]
    [string[]] $Theme = @(),

    [ValidateSet(100, 125, 150, 200)]
    [int[]] $DpiPercent = @(),

    [ValidateSet('Normal', 'HighContrast')]
    [string[]] $Contrast = @(),

    [ValidateSet('Narrow', 'Normal', 'Wide')]
    [string[]] $Viewport = @(),

    [ValidateSet('Single', 'Dual')]
    [string[]] $PaneTopology = @(),

    [ValidateSet('Closed', 'Open')]
    [string[]] $DetailsPane = @(),

    [string] $OutputPath,

    [string] $MarkdownOutputPath,

    [switch] $ContinueOnFailure,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

# Default iterations if not explicitly passed
if ($Iterations -le 0) {
    switch ($Scenario) {
        'application-lifecycle' { $Iterations = 100 }
        'rapid-input'          { $Iterations = 50 }
        'core-smoke'           { $Iterations = 25 }
        'visual-matrix'        { $Iterations = 25 }
    }
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

# 1. Capture Git Provenance
$headSha = (& git -C $RepoRoot rev-parse HEAD 2>&1).ToString().Trim()
$treeSha = (& git -C $RepoRoot write-tree 2>&1).ToString().Trim()
$porcelain = @(& git -C $RepoRoot status --porcelain 2>&1 | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne '' })
$isDirty = ($porcelain.Count -gt 0)

if ($RequireExactTree -and $isDirty) {
    throw "Exact-tree qualification required, but repository working tree is dirty ($($porcelain.Count) changes)."
}

$exactTreeBound = (-not $isDirty)

# 2. Locate FastFiles Target Executable
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
$commandStr = Get-SanitizedPath -FullPath $targetExe -RepoRoot $RepoRoot

# 3. Import UIA Driver and Capability Modules
$uiaDriverModule = Join-Path $RepoRoot "verify\uia-driver\UiaDriver.psm1"
$uiaCapDir = Join-Path $RepoRoot "verify\capabilities\windows-ui-automation-validation"
$uiaWin32Module = Join-Path $uiaCapDir "UiAutomationWin32.psm1"
$uiaValidationModule = Join-Path $uiaCapDir "UiAutomationValidation.psm1"

Import-Module $uiaDriverModule -Global -DisableNameChecking -ErrorAction Stop
Import-Module $uiaWin32Module -Global -DisableNameChecking -ErrorAction Stop
Import-Module $uiaValidationModule -Global -DisableNameChecking -ErrorAction Stop

# 4. Check Interactive Desktop & UIA Availability
$driverAvail = Get-UiaDriverAvailability
$isInteractive = $driverAvail.Available

# 5. Output Setup
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\uia-long-run"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "long-run-uia-$Scenario-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

$testCategory = switch ($Scenario) {
    'application-lifecycle' { 'application-lifecycle' }
    'rapid-input'          { 'rapid-input' }
    Default                { 'uia' }
}

#region Display-matrix requested-vs-observed evaluation (campaign section 14)
# Requested dimensions are NEVER assumed realized. Each requested value is
# compared against the observed host state; mismatches or unobservable host
# state classify the permutation unrealized (honest BLOCKED, never PASS).
function Get-UiaObservedDisplayState {
    $theme = 'Unknown'
    try {
        $key = Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize' -Name 'AppsUseLightTheme' -ErrorAction Stop
        $theme = if ([int]$key.AppsUseLightTheme -eq 0) { 'Dark' } else { 'Light' }
    } catch { $theme = 'Unknown' }

    $dpiPercent = $null
    $contrast = 'Unknown'
    $viewport = 'Unknown'
    try {
        if (-not ([System.Management.Automation.PSTypeName]'FastFiles.UiaDisplayProbe').Type) {
            Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
namespace FastFiles {
    public static class UiaDisplayProbe {
        [DllImport("user32.dll")]
        public static extern uint GetDpiForSystem();
        [DllImport("user32.dll")]
        public static extern int GetSystemMetrics(int nIndex);
        [DllImport("user32.dll")]
        public static extern bool SystemParametersInfo(int uiAction, int uiParam, ref HighContrastFlags pvParam, int fWinIni);
        [StructLayout(LayoutKind.Sequential)]
        public struct HighContrastFlags {
            public int cbSize;
            public int dwFlags;
        }
    }
}
"@
        }
        try {
            $sysDpi = [FastFiles.UiaDisplayProbe]::GetDpiForSystem()
            if ($sysDpi -gt 0) { $dpiPercent = [int][Math]::Round($sysDpi / 96.0 * 100.0) }
        } catch { $dpiPercent = $null }
        try {
            $hc = New-Object FastFiles.UiaDisplayProbe+HighContrastFlags
            $hc.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($hc)
            if ([FastFiles.UiaDisplayProbe]::SystemParametersInfo(0x0042, $hc.cbSize, [ref]$hc, 0)) {
                $contrast = if (($hc.dwFlags -band 1) -ne 0) { 'HighContrast' } else { 'Normal' }
            }
        } catch { $contrast = 'Unknown' }
        try {
            $cx = [FastFiles.UiaDisplayProbe]::GetSystemMetrics(0)
            if ($cx -le 0) { $viewport = 'Unknown' }
            elseif ($cx -lt 1100) { $viewport = 'Narrow' }
            elseif ($cx -lt 1700) { $viewport = 'Normal' }
            else { $viewport = 'Wide' }
        } catch { $viewport = 'Unknown' }
    } catch { }
    return [ordered]@{
        Theme = $theme
        DpiPercent = $dpiPercent
        Contrast = $contrast
        Viewport = $viewport
        PaneTopology = 'Unmeasured'
        DetailsPane = 'Unmeasured'
    }
}

$observedDisplay = Get-UiaObservedDisplayState
$matrixDims = [ordered]@{}
if (@($Theme).Count -gt 0) { $matrixDims['Theme'] = @($Theme) }
if (@($DpiPercent).Count -gt 0) { $matrixDims['DpiPercent'] = @($DpiPercent) }
if (@($Contrast).Count -gt 0) { $matrixDims['Contrast'] = @($Contrast) }
if (@($Viewport).Count -gt 0) { $matrixDims['Viewport'] = @($Viewport) }
if (@($PaneTopology).Count -gt 0) { $matrixDims['PaneTopology'] = @($PaneTopology) }
if (@($DetailsPane).Count -gt 0) { $matrixDims['DetailsPane'] = @($DetailsPane) }

$displayMatrix = @()
$unrealizedDims = @()
if ($matrixDims.Count -eq 0) {
    $displayMatrix += [pscustomobject]@{
        requested = [pscustomobject]@{}
        observed = [pscustomobject]$observedDisplay
        realized = $true
        unrealizedFields = @()
    }
} else {
    $combos = @(@{})
    foreach ($dimName in @($matrixDims.Keys)) {
        $nextCombos = @()
        foreach ($combo in $combos) {
            foreach ($dimVal in @($matrixDims[$dimName])) {
                $clone = $combo.Clone()
                $clone[$dimName] = $dimVal
                $nextCombos += $clone
            }
        }
        $combos = $nextCombos
    }
    foreach ($combo in $combos) {
        $unreal = @()
        foreach ($dimKey in @($combo.Keys)) {
            $obs = $observedDisplay[$dimKey]
            $req = $combo[$dimKey]
            if (($null -eq $obs) -or ($obs -eq 'Unknown') -or ($obs -eq 'Unmeasured')) {
                $unreal += "$dimKey(requested=$req,observed=$obs)"
            } elseif ("$obs" -ne "$req") {
                $unreal += "$dimKey(requested=$req,observed=$obs)"
            }
        }
        $isRealized = ($unreal.Count -eq 0)
        $displayMatrix += [pscustomobject]@{
            requested = [pscustomobject]$combo
            observed = [pscustomobject]$observedDisplay
            realized = $isRealized
            unrealizedFields = @($unreal)
        }
        foreach ($u in $unreal) { if ($unrealizedDims -notcontains $u) { $unrealizedDims += $u } }
    }
}

# Capability advertisement: scenario-proven base plus matrix dimensions that
# were requested AND realized. Unrealized or unrequested dims advertise nothing.
# Only scenarios whose iterations explicitly assert a capability advertise it:
# requesting PaneTopology=Dual never advertises dual-pane by itself.
$scenarioCaps = switch ($Scenario) {
    'rapid-input' { @('uia-core-smoke', 'uia-keyboard-navigation', 'dual-pane') }
    'core-smoke' { @('uia-core-smoke', 'uia-keyboard-navigation', 'dual-pane') }
    'visual-matrix' { @('uia-core-smoke', 'uia-keyboard-navigation', 'dual-pane', 'details-pane', 'storage-parent-percent') }
    default { @() }
}
$matrixCaps = @()
foreach ($perm in $displayMatrix) {
    if (-not $perm.realized) { continue }
    foreach ($prop in @($perm.requested.PSObject.Properties)) {
        $matrixCaps += switch ("$($prop.Name)=$($prop.Value)") {
            'Theme=Light' { 'visual-light-theme' }
            'Theme=Dark' { 'visual-dark-theme' }
            'DpiPercent=100' { 'dpi-100' }
            'DpiPercent=150' { 'dpi-150' }
            'Contrast=HighContrast' { 'high-contrast' }
            'Viewport=Narrow' { 'window-narrow' }
            'Viewport=Wide' { 'window-wide' }
            default { $null }
        }
    }
}
$runnerCapabilities = @($scenarioCaps + @($matrixCaps | Where-Object { $_ }) | Select-Object -Unique)
#endregion

$startUtc = [DateTime]::UtcNow
$durations = [System.Collections.Generic.List[double]]::new()
$passedCount = 0
$failedCount = 0
$timeoutCount = 0
$skippedCount = 0
$firstFailure = $null
# Execution-proof tracking (populated by scenario branches; defaults keep the
# artifact contract complete when a branch never instruments them).
$cleanupResult = [ordered]@{
    pass = $true
    status = 'PASS'
    method = 'none-required'
    residue = @()
    orphanCount = 0
    orphanPids = @()
}
$iterationLedger = [System.Collections.Generic.List[object]]::new()
$totalAssertions = 0
$totalFailedAssertions = 0
$phaseDurations = [ordered]@{}
$semanticPostconditions = [ordered]@{}

Write-Host "Starting UIA long-run certification: scenario='$Scenario', iterations=$Iterations, preset=$Preset"
Write-Host "Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"
Write-Host "Executable: $commandStr (hash: $executableHash)"

if (-not $isInteractive) {
    Write-Host "Host lacks interactive desktop context: $($driverAvail.Reason)" -ForegroundColor Yellow
    $endUtc = [DateTime]::UtcNow
    $resultObject = [ordered]@{
        schemaVersion = 1
        testId = "ffuia_$Scenario"
        category = $testCategory
        command = $commandStr
        commit = $headSha
        tree = $treeSha
        exactTreeBound = $exactTreeBound
        executableHash = $executableHash
        environment = [ordered]@{
            os = [Environment]::OSVersion.ToString()
            architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
            computer = "<local-host>"
            powershell = $PSVersionTable.PSVersion.ToString()
        }
        startUtc = $startUtc.ToString('o')
        endUtc = $endUtc.ToString('o')
        requestedIterations = $Iterations
        completedIterations = 0
        passed = 0
        failed = 0
        timedOut = 0
        skipped = $Iterations
        minDurationMs = 0.0
        medianDurationMs = 0.0
        p95DurationMs = 0.0
        maxDurationMs = 0.0
        firstFailure = [pscustomobject]@{
            iteration = 0
            error = "Host lacks interactive desktop context ($($driverAvail.Reason)). Classified BLOCKED_ENVIRONMENT."
            exitCode = $null
            stdoutTail = ""
            stderrTail = ""
        }
        artifactHashes = [ordered]@{}
        displayMatrix = @($displayMatrix)
        unrealizedDimensions = @($unrealizedDims)
        capabilities = @()
        cleanup = [ordered]@{
            pass = $true
            status = 'PASS'
            method = 'not-executed-blocked-environment'
            residue = @()
            orphanCount = 0
            orphanPids = @()
        }
        disposition = 'BLOCKED_ENVIRONMENT'
    }
    $jsonText = $resultObject | ConvertTo-Json -Depth 10
    Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM
    $fileBytes = [IO.File]::ReadAllBytes($OutputPath)
    $finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
    Set-Content -LiteralPath "$OutputPath.sha256" -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM
    return [pscustomobject]$resultObject
}

# 6. Execute Scenario Iterations
if ($Scenario -eq 'application-lifecycle') {
    for ($i = 1; $i -le $Iterations; $i++) {
        $iterSw = [System.Diagnostics.Stopwatch]::StartNew()
        $tempSandbox = Join-Path ([IO.Path]::GetTempPath()) "FastFiles-uialifecycle-$([guid]::NewGuid().ToString('n'))"
        New-Item -ItemType Directory -Path $tempSandbox -Force | Out-Null

        $proc = $null
        $isGraceful = $false
        $hasOrphans = $false
        $errorMsg = $null

        try {
            $psi = [System.Diagnostics.ProcessStartInfo]::new()
            $psi.FileName = $targetExe
            $psi.WorkingDirectory = $RepoRoot
            $psi.UseShellExecute = $false
            $psi.Environment['FASTFILES_DIAGNOSTIC'] = '1'
            $psi.Environment['FASTFILES_SOAK_MODE'] = '1'
            $psi.Environment['LOCALAPPDATA'] = $tempSandbox

            $proc = [System.Diagnostics.Process]::Start($psi)
            $appPid = $proc.Id

            # Poll for semantic readiness: window HWND and responding state
            $readySw = [System.Diagnostics.Stopwatch]::StartNew()
            $isReady = $false
            while ($readySw.ElapsedMilliseconds -lt ($TimeoutSecondsPerIteration * 1000)) {
                if ($proc.HasExited) { break }
                $proc.Refresh()
                if ($proc.MainWindowHandle -ne [IntPtr]::Zero -and $proc.Responding) {
                    $isReady = $true
                    break
                }
                Start-Sleep -Milliseconds 50
            }

            if (-not $isReady) {
                throw "Semantic readiness not reached within timeout (pid=$appPid)"
            }

            # Graceful shutdown request
            $proc.CloseMainWindow() | Out-Null
            if ($proc.WaitForExit(5000)) {
                $isGraceful = $true
            } else {
                $isGraceful = $false
                try { $proc.Kill($true); $proc.WaitForExit(2000) } catch { }
                throw "Process pid=$appPid failed graceful shutdown (forced kill required)"
            }

            # Post-shutdown residue check
            $orphans = @(Get-CimInstance Win32_Process -Filter "Name = 'FastFiles.exe' or Name = 'FastFilesEngine.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.ProcessId -eq $appPid -or $_.ParentProcessId -eq $appPid })
            if ($orphans.Count -gt 0) {
                $hasOrphans = $true
                throw "Post-shutdown residue detected: $($orphans.Count) orphan process(es) remaining"
            }
        } catch {
            $errorMsg = $_.Exception.Message
        } finally {
            if ($proc -and -not $proc.HasExited) {
                try { $proc.Kill($true); $proc.WaitForExit(2000) } catch { }
            }
            if (Test-Path -LiteralPath $tempSandbox) {
                Remove-Item -LiteralPath $tempSandbox -Recurse -Force -ErrorAction SilentlyContinue
            }
        }

        $iterSw.Stop()
        $elapsedMs = [Math]::Round($iterSw.Elapsed.TotalMilliseconds, 2)
        $durations.Add($elapsedMs)

        if ($isGraceful -and -not $hasOrphans -and -not $errorMsg) {
            $passedCount++
            if ($i % 10 -eq 0 -or $i -eq $Iterations) {
                Write-Host "  Iteration $i/$Iterations PASS (${elapsedMs}ms)" -ForegroundColor Green
            }
        } else {
            $failedCount++
            Write-Host "  Iteration $i/$Iterations FAIL: $errorMsg (${elapsedMs}ms)" -ForegroundColor Red
            if ($null -eq $firstFailure) {
                $firstFailure = [pscustomobject]@{
                    iteration = $i
                    error = $errorMsg
                    exitCode = if ($proc) { $proc.ExitCode } else { -1 }
                    stdoutTail = ""
                    stderrTail = ""
                }
            }
            if (-not $ContinueOnFailure) { break }
        }
    }
} elseif ($Scenario -eq 'rapid-input') {
    # Launch persistent scenario context for rapid input transitions.
    # Every transition ends in an explicit semantic postcondition with counted
    # assertions: open is proved by element appearance, close is proved by
    # element disappearance/restoration. A sent keystroke alone is never
    # recorded as proof. Any failed assertion fails the iteration.
    $quietLog = { param($m) }
    $ctx = $null
    $rapidSetupSw = [System.Diagnostics.Stopwatch]::StartNew()
    $rapidSetupMs = 0.0
    $rapidTeardownMs = 0.0
    try {
        $ctx = New-UiaScenarioContext -Exe $targetExe -RepoRoot $RepoRoot -ScenarioId 'rapid-input' -LogFn $quietLog
        if (-not $ctx -or -not $ctx.Main) { throw "Main window did not appear for rapid input scenario" }

        $driver = $ctx.Driver
        $main = $ctx.Main
        $appPid = [int]$ctx.Launched.Id

        Invoke-UiaActivateAppWindow -Driver $driver -Main $main
        $null = Keep-UiaWindowTopmost -Driver $driver -Element $main -Topmost $true
        $null = Wait-UiaStableGeometry -Driver $driver -Element $main -TimeoutMs 5000
        $rapidSetupSw.Stop()
        $rapidSetupMs = [Math]::Round($rapidSetupSw.Elapsed.TotalMilliseconds, 2)

        for ($i = 1; $i -le $Iterations; $i++) {
            $iterSw = [System.Diagnostics.Stopwatch]::StartNew()
            $iterError = $null
            $iterAssertions = 0
            $iterFailed = 0
            $iterPhaseMs = [ordered]@{}
            $iterChecks = [ordered]@{}

            try {
                # Transition 1: command palette open (proved) -> close (proved)
                $tSw = [System.Diagnostics.Stopwatch]::StartNew()
                $null = Send-UiaInput -Driver $driver -Keys '{CTRL}{SHIFT}p'
                $paletteEl = Wait-UiaElement -Driver $driver -AutomationId '6301' -FromElement $main -TimeoutMs 2500
                $iterAssertions++
                if ($null -eq $paletteEl) {
                    $iterFailed++
                    $iterChecks['palette-opened'] = 'FAIL: command palette edit 6301 did not appear after Ctrl+Shift+P'
                    throw "Command palette edit 6301 not visible after Ctrl+Shift+P"
                }
                $iterChecks['palette-opened'] = 'PASS: command palette edit 6301 appeared after Ctrl+Shift+P'
                $null = Send-UiaInput -Driver $driver -Keys '{ESC}'
                $paletteGone = Wait-UiaElementGone -Driver $driver -AutomationId '6301' -FromElement $main -TimeoutMs 3000
                $iterAssertions++
                if (-not $paletteGone) {
                    $iterFailed++
                    $iterChecks['palette-closed'] = 'FAIL: command palette edit 6301 still present after Escape; close not verified'
                    throw "Command palette edit 6301 still present after Escape; close not verified"
                }
                $iterChecks['palette-closed'] = 'PASS: command palette edit 6301 disappeared after Escape'
                $tSw.Stop()
                $iterPhaseMs['command-palette-ms'] = [Math]::Round($tSw.Elapsed.TotalMilliseconds, 2)

                # Transition 2: dual pane enable (proved 2 panes) -> disable (proved 1 pane)
                $tSw = [System.Diagnostics.Stopwatch]::StartNew()
                $dualResult = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'dual pane' -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane'
                $iterAssertions++
                if (-not $dualResult.Pass) {
                    $iterFailed++
                    $iterChecks['dual-pane-command'] = "FAIL: dual pane enable command failed: $($dualResult.Detail)"
                    throw "Dual pane command failed: $($dualResult.Detail)"
                }
                $iterChecks['dual-pane-command'] = 'PASS: Toggle Dual Pane enable command dispatched'
                $pane0On = $false
                $pane1On = $false
                try { $null = Find-UiaElement -Driver $driver -AutomationId 'Pane.0' -FromElement $main -TimeoutMs 2000; $pane0On = $true } catch { }
                try { $null = Find-UiaElement -Driver $driver -AutomationId 'Pane.1' -FromElement $main -TimeoutMs 2000; $pane1On = $true } catch { }
                $enabledPaneCount = 0
                if ($pane0On) { $enabledPaneCount++ }
                if ($pane1On) { $enabledPaneCount++ }
                $iterAssertions++
                if (($enabledPaneCount -ne 2) -or (-not $pane0On) -or (-not $pane1On)) {
                    $iterFailed++
                    $iterChecks['dual-pane-enabled'] = "FAIL: dual-pane enable postcondition not met: pane count=$enabledPaneCount (expected 2), Pane.0 present=$pane0On, Pane.1 present=$pane1On"
                    throw "Dual-pane enable postcondition failed: pane count=$enabledPaneCount (expected 2), Pane.0 present=$pane0On, Pane.1 present=$pane1On"
                }
                $iterChecks['dual-pane-enabled'] = 'PASS: pane count=2 with Pane.0 and Pane.1 both present after enable'
                $dualOffResult = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'dual pane' -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane'
                $iterAssertions++
                if (-not $dualOffResult.Pass) {
                    $iterFailed++
                    $iterChecks['single-pane-command'] = "FAIL: dual pane disable command failed: $($dualOffResult.Detail)"
                    throw "Dual pane disable command failed: $($dualOffResult.Detail)"
                }
                $iterChecks['single-pane-command'] = 'PASS: Toggle Dual Pane disable command dispatched'
                $pane0Back = $false
                $pane1StillThere = $true
                try { $null = Find-UiaElement -Driver $driver -AutomationId 'Pane.0' -FromElement $main -TimeoutMs 2000; $pane0Back = $true } catch { }
                try { $null = Find-UiaElement -Driver $driver -AutomationId 'Pane.1' -FromElement $main -TimeoutMs 800; $pane1StillThere = $true } catch { $pane1StillThere = $false }
                $iterAssertions++
                if ((-not $pane0Back) -or ($pane1StillThere)) {
                    $iterFailed++
                    $iterChecks['single-pane-restored'] = "FAIL: single-pane restoration postcondition not met: Pane.0 present=$pane0Back (expected True), Pane.1 present=$pane1StillThere (expected False)"
                    throw "Single-pane restoration failed: Pane.0 present=$pane0Back (expected True), Pane.1 present=$pane1StillThere (expected False)"
                }
                $iterChecks['single-pane-restored'] = 'PASS: pane count=1 with Pane.0 present and Pane.1 absent after disable'
                $tSw.Stop()
                $iterPhaseMs['dual-pane-ms'] = [Math]::Round($tSw.Elapsed.TotalMilliseconds, 2)

                # Transition 3: Search opened (proved) -> Filters disclosure (proved) -> closed (proved)
                $tSw = [System.Diagnostics.Stopwatch]::StartNew()
                $searchResult = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'search' -ExpectedNames @('Filters') -ExpectedCommandName 'Search' -ExpectedCommandId 'search.focus'
                $iterAssertions++
                if (-not $searchResult.Pass) {
                    $iterFailed++
                    $iterChecks['search-opened'] = "FAIL: Search did not open via palette: $($searchResult.Detail)"
                    throw "Search panel did not open via palette: $($searchResult.Detail)"
                }
                $searchSurface = $false
                try { $null = Find-UiaElement -Driver $driver -AutomationId 'Search.Results' -FromElement $main -TimeoutMs 2000; $searchSurface = $true } catch { }
                $iterAssertions++
                if (-not $searchSurface) {
                    $iterFailed++
                    $iterChecks['search-opened'] = 'FAIL: Search command dispatched but Search.Results surface did not appear'
                    throw "Search command dispatched but Search.Results surface did not appear"
                }
                $iterChecks['search-opened'] = 'PASS: Search opened via palette with Search.Results surface present'
                $filtersBtn = $null
                try { $filtersBtn = Find-UiaElement -Driver $driver -AutomationId 'Search.Filters' -FromElement $main -TimeoutMs 2000 } catch { }
                if ($null -eq $filtersBtn) {
                    try { $filtersBtn = Find-UiaElement -Driver $driver -Name 'Filters' -FromElement $main -TimeoutMs 2000 } catch { }
                }
                $iterAssertions++
                if ($null -eq $filtersBtn) {
                    $iterFailed++
                    $iterChecks['filters-disclosure'] = "FAIL: 'Filters' disclosure not found after Search opened"
                    throw "'Filters' disclosure not found after Search opened"
                }
                $iterChecks['filters-disclosure'] = "PASS: 'Filters' disclosure located after Search opened"
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
                try { $null = Find-UiaElement -Driver $driver -Name 'Clear history' -FromElement $main -TimeoutMs 1500; $clearFound = $true } catch { }
                try { $null = Find-UiaElement -Driver $driver -Name 'Ascending' -FromElement $main -TimeoutMs 1500; $ascFound = $true } catch { }
                $iterAssertions++
                if ((-not $clearFound) -or (-not $ascFound)) {
                    $iterFailed++
                    $iterChecks['filters-contents'] = "FAIL: expanded 'Filters' row missing expected contents: 'Clear history' present=$clearFound, 'Ascending' present=$ascFound"
                    throw "Expanded 'Filters' row missing expected contents: 'Clear history' present=$clearFound, 'Ascending' present=$ascFound"
                }
                $iterChecks['filters-contents'] = "PASS: expanded 'Filters' row exposes 'Clear history' and 'Ascending'"
                $searchClose = Close-UiaSearchPanelIfOpen -Driver $driver -Main $main -ProcessId $appPid
                $searchGone = $false
                try { $null = Find-UiaElement -Driver $driver -AutomationId 'Search.Results' -FromElement $main -TimeoutMs 800 } catch { $searchGone = $true }
                $iterAssertions++
                if ((-not $searchClose.Pass) -or (-not $searchGone)) {
                    $iterFailed++
                    $iterChecks['search-closed'] = "FAIL: Search panel close not verified: closer Pass=$($searchClose.Pass) ($($searchClose.Detail)), Search.Results absent=$searchGone"
                    throw "Search panel close not verified: closer Pass=$($searchClose.Pass), Search.Results absent=$searchGone"
                }
                $iterChecks['search-closed'] = 'PASS: Search closed with Search.Results absent and no input-blocking popups'
                $tSw.Stop()
                $iterPhaseMs['search-filters-ms'] = [Math]::Round($tSw.Elapsed.TotalMilliseconds, 2)
            } catch {
                if ($null -eq $iterError) { $iterError = $_.Exception.Message }
            }

            $iterSw.Stop()
            $elapsedMs = [Math]::Round($iterSw.Elapsed.TotalMilliseconds, 2)
            $durations.Add($elapsedMs)
            $totalAssertions += $iterAssertions
            $totalFailedAssertions += $iterFailed

            $ledgerPass = ($null -eq $iterError)
            $ledgerFailText = ''
            if (-not $ledgerPass) { $ledgerFailText = $iterError }
            $iterationLedger.Add([pscustomobject][ordered]@{
                iteration = $i
                pass = $ledgerPass
                assertions = $iterAssertions
                failedAssertions = $iterFailed
                durationMs = $elapsedMs
                failedCheck = $ledgerFailText
                phases = $iterPhaseMs
            })
            $semanticPostconditions = $iterChecks

            if ($ledgerPass) {
                $passedCount++
                if ($i % 10 -eq 0 -or $i -eq $Iterations) {
                    Write-Host "  Iteration $i/$Iterations PASS (${elapsedMs}ms, ${iterAssertions} assertions)" -ForegroundColor Green
                }
            } else {
                $failedCount++
                Write-Host "  Iteration $i/$Iterations FAIL: $iterError (${elapsedMs}ms, ${iterFailed}/${iterAssertions} assertions failed)" -ForegroundColor Red
                if ($null -eq $firstFailure) {
                    $firstFailure = [pscustomobject]@{
                        iteration = $i
                        error = $iterError
                        exitCode = 0
                        stdoutTail = ""
                        stderrTail = ""
                        assertions = $iterAssertions
                        failedAssertions = $iterFailed
                    }
                }
                if (-not $ContinueOnFailure) { break }
            }
        }
    } finally {
        $rapidTeardownSw = [System.Diagnostics.Stopwatch]::StartNew()
        if ($ctx) {
            $ctxCleanup = Remove-UiaScenarioContext -Context $ctx -LogFn $quietLog
            $ownedIds = @()
            try { $ownedIds = @($ctx.OwnedProcesses | ForEach-Object { [int]$_.Id }) } catch { }
            $liveOrphans = @()
            foreach ($ownedId in $ownedIds) {
                if (Get-Process -Id $ownedId -ErrorAction SilentlyContinue) { $liveOrphans += $ownedId }
            }
            $cleanupResidue = @()
            if ($ctxCleanup) { $cleanupResidue = @($ctxCleanup.Residue) }
            foreach ($orphanId in $liveOrphans) { $cleanupResidue += "orphan-pid=$orphanId" }
            $cleanupPass = ([bool]$ctxCleanup.Pass) -and ($liveOrphans.Count -eq 0)
            $cleanupStatus = 'FAIL'
            if ($cleanupPass) { $cleanupStatus = 'PASS' }
            $cleanupResult = [ordered]@{
                pass = $cleanupPass
                status = $cleanupStatus
                method = 'Remove-UiaScenarioContext'
                residue = $cleanupResidue
                orphanCount = $liveOrphans.Count
                orphanPids = @($liveOrphans)
            }
        } else {
            $cleanupResult['method'] = 'context-never-established'
        }
        $rapidTeardownSw.Stop()
        $rapidTeardownMs = [Math]::Round($rapidTeardownSw.Elapsed.TotalMilliseconds, 2)
        $phaseDurations['setup-ms'] = $rapidSetupMs
        $phaseDurations['teardown-ms'] = $rapidTeardownMs
    }
} elseif ($Scenario -eq 'core-smoke') {
    for ($i = 1; $i -le $Iterations; $i++) {
        $iterSw = [System.Diagnostics.Stopwatch]::StartNew()
        $smoke = Invoke-UiaCoreSmoke -Exe $targetExe -RepoRoot $RepoRoot -ScenarioId "core-smoke-$i"
        $iterSw.Stop()
        $elapsedMs = [Math]::Round($iterSw.Elapsed.TotalMilliseconds, 2)
        $durations.Add($elapsedMs)

        if ($smoke.Failed -eq 0) {
            $passedCount++
            Write-Host "  Iteration $i/$Iterations PASS: $($smoke.Passed)/$($smoke.Checks.Count) checks (${elapsedMs}ms)" -ForegroundColor Green
        } else {
            $failedCount++
            $failedChecks = @($smoke.Checks | Where-Object { -not $_.Pass } | ForEach-Object { "$($_.Name): $($_.Detail)" }) -join '; '
            Write-Host "  Iteration $i/$Iterations FAIL: $failedChecks (${elapsedMs}ms)" -ForegroundColor Red
            if ($null -eq $firstFailure) {
                $firstFailure = [pscustomobject]@{
                    iteration = $i
                    error = $failedChecks
                    exitCode = 0
                    stdoutTail = ""
                    stderrTail = ""
                }
            }
            if (-not $ContinueOnFailure) { break }
        }
    }
}
elseif ($Scenario -eq 'visual-matrix') {
    for ($i = 1; $i -le $Iterations; $i++) {
        $iterSw = [System.Diagnostics.Stopwatch]::StartNew()
        $matrix = Invoke-UiaVisualMatrix -Exe $targetExe -RepoRoot $RepoRoot -ScenarioId "visual-matrix-$i"
        $iterSw.Stop()
        $elapsedMs = [Math]::Round($iterSw.Elapsed.TotalMilliseconds, 2)
        $durations.Add($elapsedMs)
        $totalAssertions += $matrix.Checks.Count
        $iterFailedChecks = @($matrix.Checks | Where-Object { -not $_.Pass })
        $totalFailedAssertions += $iterFailedChecks.Count
        $iterationLedger.Add([pscustomobject][ordered]@{
            iteration = $i
            pass = ($matrix.Failed -eq 0)
            assertions = $matrix.Checks.Count
            failedAssertions = $iterFailedChecks.Count
            durationMs = $elapsedMs
            failedCheck = (($iterFailedChecks | ForEach-Object { "$($_.Name): $($_.Detail)" }) -join '; ')
            phases = [pscustomobject]$matrix.Detail
        })
        $semanticPostconditions = [pscustomobject]$matrix.Detail

        if ($matrix.Failed -eq 0) {
            $passedCount++
            Write-Host "  Iteration $i/$Iterations PASS: $($matrix.Passed)/$($matrix.Checks.Count) checks (${elapsedMs}ms)" -ForegroundColor Green
        } elseif (-not [string]::IsNullOrWhiteSpace([string]$matrix.Detail.storageBlocked)) {
            # Environmentally blocked (engine scope absent): skipped, never
            # counted as passed and never hidden as a failure either.
            $skippedCount++
            Write-Host "  Iteration $i/$Iterations BLOCKED: $($matrix.Detail.storageBlocked) (${elapsedMs}ms)" -ForegroundColor Yellow
            if ($null -eq $firstFailure) {
                $firstFailure = [pscustomobject]@{
                    iteration = $i
                    error = [string]$matrix.Detail.storageBlocked
                    exitCode = $null
                    stdoutTail = ""
                    stderrTail = ""
                }
            }
            if (-not $ContinueOnFailure) { break }
        } else {
            $failedCount++
            $failedChecks = @($matrix.Checks | Where-Object { -not $_.Pass } | ForEach-Object { "$($_.Name): $($_.Detail)" }) -join '; '
            Write-Host "  Iteration $i/$Iterations FAIL: $failedChecks (${elapsedMs}ms)" -ForegroundColor Red
            if ($null -eq $firstFailure) {
                $firstFailure = [pscustomobject]@{
                    iteration = $i
                    error = $failedChecks
                    exitCode = 0
                    stdoutTail = ""
                    stderrTail = ""
                }
            }
            if (-not $ContinueOnFailure) { break }
        }
}
}

$endUtc = [DateTime]::UtcNow
$completed = $passedCount + $failedCount + $timeoutCount + $skippedCount

$minMs = $(if ($durations.Count -gt 0) { ($durations | Measure-Object -Minimum).Minimum } else { 0.0 })
$maxMs = $(if ($durations.Count -gt 0) { ($durations | Measure-Object -Maximum).Maximum } else { 0.0 })
$medianMs = Get-Percentile -Values $durations.ToArray() -Percentile 0.50
$p95Ms = Get-Percentile -Values $durations.ToArray() -Percentile 0.95
$disposition = 'FAIL'
if ($failedCount -gt 0) {
    $disposition = 'FAIL'
} elseif ($timeoutCount -gt 0) {
    $disposition = 'TIMEOUT'
} elseif ($passedCount -eq $Iterations) {
    $disposition = 'PASS'
} elseif ($skippedCount -gt 0) {
    # Environmentally blocked iterations (never passed, never failed).
    $disposition = 'BLOCKED_ENVIRONMENT'
}
# Cleanup gate: overall PASS requires a clean cleanup census. A cleanup
# failure forces FAIL and is never hidden behind iteration counts.
if (($cleanupResult.pass -ne $true) -and ($disposition -eq 'PASS')) {
    $disposition = 'FAIL'
    $cleanupDetail = "Cleanup census failed (method=$($cleanupResult.method), orphans=$($cleanupResult.orphanCount), residue=$($cleanupResult.residue -join ','))"
    if ($null -eq $firstFailure) {
        $firstFailure = [pscustomobject]@{
            iteration = 0
            error = $cleanupDetail
            exitCode = $null
            stdoutTail = ""
            stderrTail = ""
        }
    }
    Write-Host "  $cleanupDetail" -ForegroundColor Red
}
# Requested-but-unrealized display dimensions can never PASS. Requested state
# was not observed on this host: honest BLOCKED_ENVIRONMENT, never assumed.
if ((@($unrealizedDims).Count -gt 0) -and ($disposition -eq 'PASS')) {
    $disposition = 'BLOCKED_ENVIRONMENT'
    $matrixDetail = "Requested display dimensions not realized on this host: $((@($unrealizedDims)) -join '; ')"
    if ($null -eq $firstFailure) {
        $firstFailure = [pscustomobject]@{
            iteration = 0
            error = $matrixDetail
            exitCode = $null
            stdoutTail = ""
            stderrTail = ""
        }
    }
    Write-Host "  $matrixDetail" -ForegroundColor Yellow
}
# Proven-capabilities gate: the advertised set describes what executed
# iterations actually proved. A visual-matrix run with zero fully-passed
# iterations proved no end-to-end storage contract, so storage-parent-percent
# (and details-pane, whose reservation proof lives in the same iterations)
# must not appear on the artifact. Matches the non-interactive precedent of
# capabilities=[] on BLOCKED_ENVIRONMENT.
if (($Scenario -eq 'visual-matrix') -and ($passedCount -eq 0)) {
    $runnerCapabilities = @($runnerCapabilities | Where-Object { $_ -notin @('details-pane', 'storage-parent-percent') })
    Write-Host "  No fully-passed visual-matrix iteration: withholding details-pane/storage-parent-percent from advertised capabilities" -ForegroundColor Yellow
}

$artifactHashes = [ordered]@{}

if ($MarkdownOutputPath) {
    $parentMd = Split-Path -Parent $MarkdownOutputPath
    if ($parentMd -and -not (Test-Path -LiteralPath $parentMd)) { New-Item -ItemType Directory -Path $parentMd -Force | Out-Null }
    $md = @"
# UIA Long-Run Certification: $Scenario

- **Category**: $testCategory
- **Disposition**: **$disposition**
- **Commit**: ``$headSha`` (tree: ``$treeSha``, exact: $exactTreeBound)
- **Requested Iterations**: $Iterations
- **Completed Iterations**: $completed
- **Passed**: $passedCount
- **Failed**: $failedCount
- **Timed Out**: $timeoutCount
- **Min / Median / P95 / Max**: ${minMs}ms / ${medianMs}ms / ${p95Ms}ms / ${maxMs}ms
"@
    Set-Content -LiteralPath $MarkdownOutputPath -Value $md -Encoding utf8NoBOM
    $mdBytes = [IO.File]::ReadAllBytes($MarkdownOutputPath)
    $mdHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($mdBytes)).Replace('-', '').ToLowerInvariant()
    $artifactHashes[([IO.Path]::GetFileName($MarkdownOutputPath))] = $mdHash
}

$resultObject = [ordered]@{
    schemaVersion = 1
    testId = "ffuia_$Scenario"
    category = $testCategory
    command = $commandStr
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    executableHash = $executableHash
    environment = [ordered]@{
        os = [Environment]::OSVersion.ToString()
        architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        computer = "<local-host>"
        powershell = $PSVersionTable.PSVersion.ToString()
    }
    startUtc = $startUtc.ToString('o')
    endUtc = $endUtc.ToString('o')
    requestedIterations = $Iterations
    completedIterations = $completed
    passed = $passedCount
    failed = $failedCount
    timedOut = $timeoutCount
    skipped = $skippedCount
    totalIterations = $Iterations
    iterationLedger = @($iterationLedger)
    phaseDurations = $phaseDurations
    assertionCounts = [ordered]@{
        total = $totalAssertions
        failed = $totalFailedAssertions
    }
    semanticPostconditions = $semanticPostconditions
    displayMatrix = @($displayMatrix)
    unrealizedDimensions = @($unrealizedDims)
    capabilities = @($runnerCapabilities)
    cleanup = $cleanupResult
    minDurationMs = [Math]::Round($minMs, 2)
    medianDurationMs = [Math]::Round($medianMs, 2)
    p95DurationMs = [Math]::Round($p95Ms, 2)
    maxDurationMs = [Math]::Round($maxMs, 2)
    firstFailure = $firstFailure
    artifactHashes = $artifactHashes
    disposition = $disposition
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

# Authoritative sidecar checksum
$finalFileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($finalFileBytes)).Replace('-', '').ToLowerInvariant()
$sidecarPath = "$OutputPath.sha256"
Set-Content -LiteralPath $sidecarPath -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM

$fgColor = $(if ($disposition -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host "UIA long-run completed: $passedCount/$Iterations passed. Disposition: $disposition" -ForegroundColor $fgColor
Write-Host "Timing: min=${minMs}ms, median=${medianMs}ms, p95=${p95Ms}ms, max=${maxMs}ms"
Write-Host "Artifact written: $OutputPath (SHA-256: $finalSha256)"

return [pscustomobject]$resultObject
