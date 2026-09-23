<#
    .SYNOPSIS
        FastFiles hardening campaign Phase 6: DPI/display/accessibility
        certification MATRIX harness (detection now, live cells owner-sequenced).

    .DESCRIPTION
        Detection-first harness. The default invocation launches nothing,
        mutates no host setting, and reads no prior run artifacts: it observes
        host display facts via verify/certification/HostDisplayCapabilities.psm1
        and resolves every requested cell to exactly one of
        PASS | FAIL | HOST_UNSUPPORTED | NOT_RUN.

        Default (detection-only) output is the deliverable for this phase: a
        requested-vs-executed matrix where every cell is NOT_RUN (deferred,
        owner-sequenced live execution) or HOST_UNSUPPORTED (prerequisite
        unobservable or absent on this host). Default mode NEVER reports PASS
        or FAIL; unsupported/not-run are never folded into PASS.

        -Execute (implemented, owner-run later) drives the visual catalog per
        cell for cells whose prerequisites are satisfied: theme/dpi/topology
        cells delegate to the canonical capability
        Invoke-UiVisualValidationCapability with the cell's mapped
        VisualScenario; window-state and monitor-transition cells perform real
        Win32 transitions (ShowWindow / Set-UiaWindowPosition) against a live
        app window and capture the DWM frame with VisualCapture.psm1. Only
        -Execute can produce PASS or FAIL.

        Display-setting changes (scale percent, High Contrast, monitor
        topology) are USER-SESSION actions: this harness only observes them.
        Each cell carries OwnerAction text stating exactly what the owner must
        do in the interactive session before re-running with -Execute.

        Evidence (always under allowlisted verify/runs/display-matrix/<stamp>/,
        write-only): matrix.json (machine-readable: schema, headSha, facts,
        per-cell {status, reason, evidenceRef}), summary.md (human table),
        host-facts.json, plus per-cell subdirectories under -Execute.

        Reuse map (no duplicated machinery):
          HostDisplayCapabilities.psm1 Get-HostDisplayFacts /
            Resolve-DisplayCellStatus ... detection + four-status resolution.
          VisualCapture.psm1 Test-VisualDpiMatch ... requested-vs-observed DPI
            gate for dpi-* cells (DPI is compared, never claimed).
          VisualCapture.psm1 Get-VisualGitState / New-VisualCaptureRun /
            Capture-VisualWindow / Add-VisualScenarioResult /
            Write-VisualCaptureRun ... provenance + DWM capture path.
          VisualValidation.psm1 Invoke-UiVisualValidationCapability (-Execute
            catalog delegation) + Test-UiVisualValidationAvailability
            (interactive preflight).
          UiaDriver.psm1 Get-UiaDriverAvailability, New-UiaDriver,
            Find-UiaElement, Set-UiaForeground, Test-UiaForegroundWindow,
            Set-UiaWindowPosition, Wait-UiaStableRect ... live actuation.
          Compare-VisualBaseline.ps1 + BaselineManifest.psm1 ... follow-up
            regression comparison of written captures (not run here).

    .PARAMETER Execute
        Owner-sequenced live execution. Without it, detection-only matrix.

    .PARAMETER CellIds
        Optional subset of cell ids. Default: all cells. Unknown ids throw.

    .PARAMETER Probe
        Test-only injectable detection facts (see HostDisplayCapabilities.psm1).
        Absent keys perform live detection.

    .PARAMETER OutputRoot
        Evidence directory. Default: verify/runs/display-matrix/<stamp>/.
#>
[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $OutputRoot = '',
    [string[]] $CellIds = @(),
    [hashtable] $Probe,
    [switch] $Execute
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'HostDisplayCapabilities.psm1') -Force -DisableNameChecking

$script:IsWindowsPlatform = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows)

function Get-DisplayMatrixGitHead {
    param([string] $Repo)
    try {
        $head = ((& git -C $Repo rev-parse HEAD 2>$null) | Out-String).Trim().ToLowerInvariant()
        if ($head -match '^[0-9a-f]{40}$') { return $head }
    } catch { }
    return 'unbound'
}

function Get-DisplayMatrixExe {
    param([string] $Repo)
    foreach ($rel in @('build\debug-diagnostic\src\ui\FastFiles.exe', 'build\debug\src\ui\FastFiles.exe')) {
        $candidate = Join-Path $Repo $rel
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    return $null
}

function Get-DisplayMatrixSessionKind {
    $sessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
    if ($sessionId -eq 0) { return 'session-0' }
    if ([Environment]::UserInteractive) { return 'console' }
    return 'services'
}

function Invoke-DisplayMatrixCatalogCell {
    param($Cell, $Facts, [string] $Repo, [string] $CellDir)
    $artifactsDir = $CellDir
    New-Item -ItemType Directory -Path $artifactsDir -Force | Out-Null
    Import-Module (Join-Path $Repo 'verify\capabilities\windows-ui-visual-validation\VisualValidation.psm1') -Force -DisableNameChecking
    Import-Module (Join-Path $Repo 'verify\visual\VisualCapture.psm1') -Force -DisableNameChecking
    $capability = Invoke-UiVisualValidationCapability -RunContext $null -Fingerprint $null `
        -ArtifactsDir $artifactsDir `
        -Options @{ RepoRoot = $Repo; VisualScenario = $Cell.VisualScenario }
    $sub = @($capability.SubResults | Where-Object { [string]$_.id -eq [string]$Cell.VisualScenario }) | Select-Object -First 1
    if ($null -eq $sub) {
        return [pscustomobject]@{
            Status = 'FAIL'; Reason = "catalog returned no subresult for scenario '$($Cell.VisualScenario)'"; EvidenceRef = $null
        }
    }
    if ([string]$sub.status -eq 'SKIPPED') {
        return [pscustomobject]@{
            Status = 'HOST_UNSUPPORTED'
            Reason = "catalog-blocked on this host ($([string]$sub.reason): $([string]$sub.detail))"
            EvidenceRef = $null
        }
    }
    if ([string]$sub.status -ne 'PASS' -or [string]$capability.Status -ne 'PASS') {
        $detail = if ([string]$sub.reason) { [string]$sub.reason } else { [string]$capability.Reason }
        return [pscustomobject]@{
            Status = 'FAIL'; Reason = "catalog scenario '$($Cell.VisualScenario)' failed: $detail"; EvidenceRef = $null
        }
    }
    if ([int]$Cell.RequestedDpi -gt 0) {
        $manifestPath = Join-Path $artifactsDir 'visual-capture\manifest.json'
        if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
            return [pscustomobject]@{
                Status = 'FAIL'; Reason = "catalog passed but wrote no manifest for DPI gate: $manifestPath"; EvidenceRef = $null
            }
        }
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $record = @($manifest.captures | Where-Object { [string]$_.scenarioId -eq [string]$Cell.VisualScenario }) | Select-Object -First 1
        if ($null -eq $record -or $null -eq $record.observedDpi) {
            return [pscustomobject]@{
                Status = 'FAIL'; Reason = "catalog passed but recorded no observed DPI for '$($Cell.VisualScenario)'"; EvidenceRef = $null
            }
        }
        $dpiCheck = Test-VisualDpiMatch -RequestedPercent ([int]$Cell.RequestedDpi) -ObservedDpi ([int]$record.observedDpi)
        if (-not [bool]$dpiCheck.Match) {
            return [pscustomobject]@{
                Status = 'FAIL'
                Reason = "dpi-mismatch: cell requests $($Cell.RequestedDpi)% (expected $($dpiCheck.ExpectedDpi) device DPI) but the live window observed $($dpiCheck.ObservedDpi)"
                EvidenceRef = $null
            }
        }
    }
    return [pscustomobject]@{
        Status = 'PASS'
        Reason = "catalog scenario '$($Cell.VisualScenario)' passed its semantic post-condition with DWM capture"
        EvidenceRef = ("cell-" + $Cell.Id + "/visual-capture/manifest.json")
    }
}

function Invoke-DisplayMatrixTransitionCell {
    param($Cell, $Facts, [string] $Repo, [string] $CellDir, [string] $Exe)
    if (-not $script:IsWindowsPlatform) {
        return [pscustomobject]@{
            Status = 'HOST_UNSUPPORTED'; Reason = 'windows-required for Win32 window-state transitions'; EvidenceRef = $null
        }
    }
    if (-not ('FfDisplayMatrixWindow' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FfDisplayMatrixWindow
{
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")]
    public static extern bool IsIconic(IntPtr hWnd);
    public const int SW_MINIMIZE = 6;
    public const int SW_RESTORE = 9;
    public const int SW_MAXIMIZE = 3;
}
'@
    }
    Import-Module (Join-Path $Repo 'verify\uia-driver\UiaDriver.psm1') -Force -DisableNameChecking
    Import-Module (Join-Path $Repo 'verify\visual\VisualCapture.psm1') -Force -DisableNameChecking
    $driver = New-UiaDriver -TimeoutMs 5000 -PollIntervalMs 100 -MaxTreeDepth 24 -MaxTreeWidth 160
    $existingEngineIds = @(Get-Process -Name FastFilesEngine -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
    $process = $null
    $transition = [ordered]@{ cell = $Cell.Id }
    try {
        $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe -Parent) -PassThru
        $main = $null
        $deadline = [Diagnostics.Stopwatch]::StartNew()
        while ($deadline.ElapsedMilliseconds -lt 25000 -and $null -eq $main) {
            try { $main = Find-UiaElement -Driver $driver -ClassName 'FastFilesMainWindow' -TimeoutMs 1000 } catch { $main = $null }
            if ($null -eq $main) { Start-Sleep -Milliseconds 250 }
        }
        if ($null -eq $main) {
            return [pscustomobject]@{ Status = 'FAIL'; Reason = 'FastFiles main window was not discoverable within 25 seconds'; EvidenceRef = $null }
        }
        $null = Set-UiaForeground -Driver $driver -Element $main
        if (-not (Test-UiaForegroundWindow -Driver $driver -Element $main)) {
            return [pscustomobject]@{ Status = 'FAIL'; Reason = 'FastFiles did not acquire foreground ownership before the window-state transition'; EvidenceRef = $null }
        }
        $hwnd = [IntPtr]$main.Current.NativeWindowHandle
        if ($hwnd -eq [IntPtr]::Zero) {
            return [pscustomobject]@{ Status = 'FAIL'; Reason = 'main window has no HWND'; EvidenceRef = $null }
        }
        $primary = @($Facts.Monitors | Where-Object { [bool]$_.Primary }) | Select-Object -First 1
        if ($null -eq $primary) { $primary = @($Facts.Monitors) | Select-Object -First 1 }
        $before = Get-VisualWindowBounds -Element $main
        $transition.before = [ordered]@{ x = $before.X; y = $before.Y; width = $before.Width; height = $before.Height }
        switch ([string]$Cell.Id) {
            'window-maximize' {
                $null = [FfDisplayMatrixWindow]::ShowWindow($hwnd, [FfDisplayMatrixWindow]::SW_MAXIMIZE)
                if (-not (Wait-UiaStableRect -Driver $driver -Element $main -StableReads 3 -IntervalMs 150 -TimeoutMs 4000)) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = 'maximized window geometry did not settle'; EvidenceRef = $null }
                }
                $after = Get-VisualWindowBounds -Element $main
                if ($null -ne $primary -and [int]$after.Width -lt ([int]$primary.Bounds.Width - 16)) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = "maximized width $($after.Width) is narrower than the primary monitor ($($primary.Bounds.Width))"; EvidenceRef = $null }
                }
                $transition.after = [ordered]@{ x = $after.X; y = $after.Y; width = $after.Width; height = $after.Height }
                $null = [FfDisplayMatrixWindow]::ShowWindow($hwnd, [FfDisplayMatrixWindow]::SW_RESTORE)
            }
            'window-restore' {
                $null = [FfDisplayMatrixWindow]::ShowWindow($hwnd, [FfDisplayMatrixWindow]::SW_MAXIMIZE)
                Start-Sleep -Milliseconds 400
                $null = [FfDisplayMatrixWindow]::ShowWindow($hwnd, [FfDisplayMatrixWindow]::SW_RESTORE)
                if (-not (Wait-UiaStableRect -Driver $driver -Element $main -StableReads 3 -IntervalMs 150 -TimeoutMs 4000)) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = 'restored window geometry did not settle'; EvidenceRef = $null }
                }
                if ([FfDisplayMatrixWindow]::IsIconic($hwnd)) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = 'window is still minimized after restore'; EvidenceRef = $null }
                }
                $after = Get-VisualWindowBounds -Element $main
                $transition.after = [ordered]@{ x = $after.X; y = $after.Y; width = $after.Width; height = $after.Height }
            }
            'window-minimize' {
                $null = [FfDisplayMatrixWindow]::ShowWindow($hwnd, [FfDisplayMatrixWindow]::SW_MINIMIZE)
                $minimized = $false
                $poll = [Diagnostics.Stopwatch]::StartNew()
                while ($poll.ElapsedMilliseconds -lt 3000) {
                    if ([FfDisplayMatrixWindow]::IsIconic($hwnd)) { $minimized = $true; break }
                    Start-Sleep -Milliseconds 150
                }
                if (-not $minimized) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = 'window did not reach the minimized state'; EvidenceRef = $null }
                }
                $transition.minimized = $true
                $null = [FfDisplayMatrixWindow]::ShowWindow($hwnd, [FfDisplayMatrixWindow]::SW_RESTORE)
                if (-not (Wait-UiaStableRect -Driver $driver -Element $main -StableReads 3 -IntervalMs 150 -TimeoutMs 4000)) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = 'post-restore window geometry did not settle'; EvidenceRef = $null }
                }
                $after = Get-VisualWindowBounds -Element $main
                $transition.after = [ordered]@{ x = $after.X; y = $after.Y; width = $after.Width; height = $after.Height }
            }
            'window-resize' {
                $position = Set-UiaWindowPosition -Driver $driver -Element $main -X 40 -Y 40 -Width 720 -Height 800
                if (-not [bool]$position.ok) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = 'SetWindowPos failed for the narrow resize'; EvidenceRef = $null }
                }
                if (-not (Wait-UiaStableRect -Driver $driver -Element $main -StableReads 3 -IntervalMs 150 -TimeoutMs 4000)) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = 'resized window geometry did not settle'; EvidenceRef = $null }
                }
                $after = Get-VisualWindowBounds -Element $main
                if ([Math]::Abs([int]$after.Width - 720) -gt 8) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = "DWM width $($after.Width) differs from requested 720 after bounded frame adjustment"; EvidenceRef = $null }
                }
                $transition.after = [ordered]@{ x = $after.X; y = $after.Y; width = $after.Width; height = $after.Height }
            }
            'monitor-transition' {
                $target = @($Facts.Monitors | Where-Object { -not [bool]$_.Primary }) | Select-Object -First 1
                if ($null -eq $target -and @($Facts.Monitors).Count -ge 2) { $target = @($Facts.Monitors)[1] }
                if ($null -eq $target) {
                    return [pscustomobject]@{ Status = 'HOST_UNSUPPORTED'; Reason = 'no second monitor observed for the cross-monitor transition'; EvidenceRef = $null }
                }
                $position = Set-UiaWindowPosition -Driver $driver -Element $main `
                    -X ([int]$target.Bounds.X + 40) -Y ([int]$target.Bounds.Y + 40) -Width 1280 -Height 800
                if (-not [bool]$position.ok) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = 'SetWindowPos failed for the cross-monitor move'; EvidenceRef = $null }
                }
                if (-not (Wait-UiaStableRect -Driver $driver -Element $main -StableReads 3 -IntervalMs 150 -TimeoutMs 4000)) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = 'transitioned window geometry did not settle'; EvidenceRef = $null }
                }
                $after = Get-VisualWindowBounds -Element $main
                if ([int]$after.X -lt [int]$target.Bounds.X -or [int]$after.X -ge ([int]$target.Bounds.X + [int]$target.Bounds.Width)) {
                    return [pscustomobject]@{ Status = 'FAIL'; Reason = "window X $($after.X) is outside the target monitor (origin $($target.Bounds.X), width $($target.Bounds.Width))"; EvidenceRef = $null }
                }
                $transition.after = [ordered]@{ x = $after.X; y = $after.Y; width = $after.Width; height = $after.Height }
                $transition.targetMonitor = [string]$target.Id
            }
            default {
                return [pscustomobject]@{ Status = 'FAIL'; Reason = "transition executor has no actuation for cell '$($Cell.Id)'"; EvidenceRef = $null }
            }
        }
        try { $null = Reset-UiaModifierKeys -Driver $driver } catch { }
        $appSha = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant()
        $scalePercent = if ($Facts.SystemDpiObservable -and [int]$Facts.SystemDpiPercent -gt 0) { [int]$Facts.SystemDpiPercent } else { 100 }
        $run = New-VisualCaptureRun -Root $CellDir -ApplicationSha $appSha -HostId 'local-interactive-windows' -RepoRoot $Repo
        $identity = Get-UiaElementIdentity -Driver $driver -Element $main
        if ([string]$identity.Name -ne 'FastFiles' -or [string]$identity.ClassName -ne 'FastFilesMainWindow') {
            return [pscustomobject]@{ Status = 'FAIL'; Reason = 'main window identity mismatch after the transition'; EvidenceRef = $null }
        }
        $capture = Capture-VisualWindow -Run $run -Element $main -ScenarioId $Cell.Id -AssertionPass $true
        if ([int]$capture.Bounds.Dpi -le 0) {
            return [pscustomobject]@{ Status = 'FAIL'; Reason = 'captured window reported no observed device DPI'; EvidenceRef = $null }
        }
        $record = Add-VisualScenarioResult -Run $run -ScenarioId $Cell.Id -ExpectedState $Cell.Title `
            -FixtureId 'none' -Theme 'Light' -Width ([int]$capture.Bounds.Width) -PaneMode 'single' `
            -DpiScalePercent $scalePercent -ObservedDpi ([int]$capture.Bounds.Dpi) -Status 'PASS' `
            -AssertionPass $true -AssertionEvidence ([pscustomobject]@{ transition = $transition }) `
            -ScreenshotPath $capture.Path -ImageHash $capture.Hash -ViewportHeight ([int]$capture.Bounds.Height)
        $written = Write-VisualCaptureRun -Run $run
        $relativeManifest = ([System.IO.Path]::GetRelativePath($CellDir, $written.Manifest) -replace '\\', '/')
        return [pscustomobject]@{
            Status = 'PASS'
            Reason = "transition verified (before $($transition.before.width)x$($transition.before.height) -> after $($transition.after.width)x$($transition.after.height)) with DWM capture"
            EvidenceRef = ("cell-" + $Cell.Id + "/" + $relativeManifest)
        }
    } catch {
        return [pscustomobject]@{ Status = 'FAIL'; Reason = $_.Exception.Message; EvidenceRef = $null }
    } finally {
        if ($null -ne $process) { try { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } catch { } }
        Get-Process -Name FastFilesEngine -ErrorAction SilentlyContinue |
            Where-Object { $existingEngineIds -notcontains $_.Id } |
            ForEach-Object { try { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue } catch { } }
    }
}

function Invoke-DisplayMatrixLiveCell {
    param($Cell, $Facts, [string] $Repo, [string] $RunDir)
    $cellDir = Join-Path $RunDir ("cell-" + $Cell.Id)
    New-Item -ItemType Directory -Path $cellDir -Force | Out-Null
    try {
        $exe = Get-DisplayMatrixExe -Repo $Repo
        if (-not $exe) {
            return [pscustomobject]@{
                Status = 'NOT_RUN'
                Reason = 'deferred-visual-executable-missing (build the debug-diagnostic UI first, then re-run with -Execute)'
                EvidenceRef = $null
            }
        }
        Import-Module (Join-Path $Repo 'verify\uia-driver\UiaDriver.psm1') -Force -DisableNameChecking
        $driverProbe = Get-UiaDriverAvailability
        if (-not [bool]$driverProbe.Available) {
            return [pscustomobject]@{
                Status = 'HOST_UNSUPPORTED'
                Reason = "uia-driver-unavailable ($([string]$driverProbe.Reason))"
                EvidenceRef = $null
            }
        }
        Import-Module (Join-Path $Repo 'verify\capabilities\windows-ui-visual-validation\VisualValidation.psm1') -Force -DisableNameChecking
        $sessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
        $fingerprint = [pscustomobject]@{ SessionId = $sessionId; SessionKind = (Get-DisplayMatrixSessionKind) }
        $availability = Test-UiVisualValidationAvailability -Fingerprint $fingerprint
        if (-not [bool]$availability.Available) {
            return [pscustomobject]@{
                Status = 'HOST_UNSUPPORTED'
                Reason = "interactive-context-unavailable ($([string]$availability.Reason))"
                EvidenceRef = $null
            }
        }
        if ([string]$Cell.Category -eq 'window-state' -or [string]$Cell.Id -eq 'monitor-transition') {
            return (Invoke-DisplayMatrixTransitionCell -Cell $Cell -Facts $Facts -Repo $Repo -CellDir $cellDir -Exe $exe)
        }
        return (Invoke-DisplayMatrixCatalogCell -Cell $Cell -Facts $Facts -Repo $Repo -CellDir $cellDir)
    } catch {
        return [pscustomobject]@{ Status = 'FAIL'; Reason = $_.Exception.Message; EvidenceRef = $null }
    }
}

$repo = [IO.Path]::GetFullPath($RepoRoot)
$stamp = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + ([guid]::NewGuid().ToString('N').Substring(0, 8))
$runDir = if ($OutputRoot) { [IO.Path]::GetFullPath($OutputRoot) } else { Join-Path $repo "verify/runs/display-matrix/$stamp" }
New-Item -ItemType Directory -Path $runDir -Force | Out-Null

$facts = Get-HostDisplayFacts -Probe $Probe -RepoRoot $repo
$allCells = @(Get-DisplayMatrixCells)
if ($CellIds -and $CellIds.Count -gt 0) {
    $unknown = @()
    foreach ($candidateId in @($CellIds)) {
        if (@($allCells | Where-Object { [string]$_.Id -eq [string]$candidateId }).Count -eq 0) { $unknown += [string]$candidateId }
    }
    if ($unknown.Count -gt 0) { throw "unknown display cell id(s): $($unknown -join ', ')" }
    $requested = @($allCells | Where-Object { $CellIds -contains $_.Id })
} else {
    $requested = @($allCells)
}

$headSha = Get-DisplayMatrixGitHead -Repo $repo
$rows = [System.Collections.Generic.List[object]]::new()
foreach ($cell in $requested) {
    $resolution = Resolve-DisplayCellStatus -Cell $cell -Facts $facts
    if ($Execute -and [string]$resolution.Status -eq 'NOT_RUN') {
        $resolution = Invoke-DisplayMatrixLiveCell -Cell $cell -Facts $facts -Repo $repo -RunDir $runDir
    }
    $evidenceRef = if ($resolution.EvidenceRef) { [string]$resolution.EvidenceRef } else { 'matrix.json' }
    [void]$rows.Add([ordered]@{
        id = [string]$cell.Id
        title = [string]$cell.Title
        category = [string]$cell.Category
        requestedDpi = [int]$cell.RequestedDpi
        visualScenario = [string]$cell.VisualScenario
        status = [string]$resolution.Status
        reason = [string]$resolution.Reason
        evidenceRef = $evidenceRef
        ownerAction = [string]$cell.OwnerAction
    })
}

$counts = [ordered]@{
    PASS = @($rows | Where-Object { $_['status'] -eq 'PASS' } | ForEach-Object { $_ }).Count
    FAIL = @($rows | Where-Object { $_['status'] -eq 'FAIL' } | ForEach-Object { $_ }).Count
    HOST_UNSUPPORTED = @($rows | Where-Object { $_['status'] -eq 'HOST_UNSUPPORTED' } | ForEach-Object { $_ }).Count
    NOT_RUN = @($rows | Where-Object { $_['status'] -eq 'NOT_RUN' } | ForEach-Object { $_ }).Count
}
$executed = if ($Execute) { ([int]$counts.PASS + [int]$counts.FAIL) } else { 0 }

$matrix = [ordered]@{
    schema = 'fastfiles.display-matrix.v1'
    schemaVersion = 1
    stamp = $stamp
    headSha = $headSha
    observedUtc = ([DateTime]::UtcNow.ToString('o'))
    mode = if ($Execute) { 'live-execution' } else { 'detection-only' }
    facts = $facts
    requestedCount = @($rows).Count
    executedCount = $executed
    summary = $counts
    cells = @($rows)
    exitCode = if ([int]$counts.FAIL -gt 0) { 1 } else { 0 }
}

$matrixPath = Join-Path $runDir 'matrix.json'
$factsPath = Join-Path $runDir 'host-facts.json'
$summaryPath = Join-Path $runDir 'summary.md'
$matrix | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $matrixPath -Encoding utf8
$facts | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $factsPath -Encoding utf8

$lines = [System.Collections.Generic.List[string]]::new()
[void]$lines.Add('# FastFiles display certification matrix (Phase 6)')
[void]$lines.Add('')
[void]$lines.Add("- Mode: $($matrix.mode) | requested $($matrix.requestedCount) | executed $($matrix.executedCount)")
[void]$lines.Add("- Host: $($facts.MonitorCount) monitor(s), system DPI $($facts.SystemDpiPercent)% (observed=$($facts.SystemDpiObservable)), theme $($facts.ThemeName), High Contrast $($facts.HighContrast)")
[void]$lines.Add("- Head SHA: $headSha")
[void]$lines.Add('')
[void]$lines.Add('| Requested cell | Status | Reason | Evidence |')
[void]$lines.Add('| --- | --- | --- | --- |')
foreach ($row in $rows) {
    $reason = ([string]$row['reason'] -replace '\|', '\\|')
    [void]$lines.Add("| $($row['id']) | $($row['status']) | $reason | $($row['evidenceRef']) |")
}
if (-not $Execute) {
    [void]$lines.Add('')
    [void]$lines.Add('Detection-only run: every cell is NOT_RUN (deferred, owner-sequenced live execution) or HOST_UNSUPPORTED (prerequisite unobservable or absent). Display-scale, High Contrast, and topology changes are user-session actions; see each cell''s OwnerAction in matrix.json, then re-run with -Execute.')
}
$lines | Set-Content -LiteralPath $summaryPath -Encoding utf8

Write-Host "display-matrix [$($matrix.mode)]: requested $($matrix.requestedCount), executed $($matrix.executedCount) | PASS $($counts.PASS) / FAIL $($counts.FAIL) / HOST_UNSUPPORTED $($counts.HOST_UNSUPPORTED) / NOT_RUN $($counts.NOT_RUN)"
foreach ($row in $rows) {
    Write-Host "  $($row['id']) -> $($row['status']) ($($row['reason']))"
}
Write-Host "evidence: $runDir"

return [pscustomobject]@{
    RunDir = $runDir
    MatrixPath = $matrixPath
    SummaryPath = $summaryPath
    Mode = $matrix.mode
    Cells = @($rows)
    Summary = $counts
    ExitCode = $matrix.exitCode
}
