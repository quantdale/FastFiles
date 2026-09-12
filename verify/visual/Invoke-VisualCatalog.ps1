<#
    Interactive visual catalog for FastFiles product workspaces.

    Launches the debug-diagnostic UI in a disposable sandbox, drives Home,
    browse, Fast Search, Storage Analysis, Settings, and the command palette
    through the existing UIA helpers, then captures the live DWM frame with
    VisualCapture.psm1 after a semantic post-condition. PrintWindow is never
    used. Production signing is out of scope.

    Usage:
      pwsh ./verify/visual/Invoke-VisualCatalog.ps1
      pwsh ./verify/visual/Invoke-VisualCatalog.ps1 -OutputRoot D:\temp\ff-visual
      pwsh ./verify/visual/Invoke-VisualCatalog.ps1 -Gallery  # capture gallery matrix
#>
[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $ExeRel = 'build\debug-diagnostic\src\ui\FastFiles.exe',
    [string] $OutputRoot = '',
    [switch] $Gallery
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath($RepoRoot)
$exe = Join-Path $repo $ExeRel
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "VISUAL_CATALOG_EXE_MISSING: $exe"
}

if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repo 'openspec\changes\final-product-completion-and-production-readiness\evidence\visual-catalog'
}

Import-Module (Join-Path $repo 'verify\uia-driver\UiaDriver.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'verify\capabilities\windows-ui-automation-validation\UiAutomationValidation.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'verify\capabilities\windows-ui-automation-validation\UiAutomationWin32.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $repo 'verify\visual\VisualCapture.psm1') -Force -DisableNameChecking

if (-not ('FfVisualWindowPos' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FfVisualWindowPos
{
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);
    public const uint SWP_NOZORDER = 0x0004;
    public const uint SWP_SHOWWINDOW = 0x0040;
}
'@
}

if (-not ('FfVisualSettingsNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class FfVisualSettingsNative
{
    [DllImport("user32.dll", EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    public const uint CB_SETCURSEL = 0x014E;
}
'@
}

function Write-CatalogLog {
    param([string] $Message)
    Write-Host $Message
}

function Get-CatalogAppSha {
    param([string] $Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Set-CatalogWindowSize {
    param($Main, [int] $X, [int] $Y, [int] $Width, [int] $Height)
    $hwnd = [IntPtr]$Main.Current.NativeWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { throw 'main window has no HWND' }
    $flags = [FfVisualWindowPos]::SWP_NOZORDER -bor [FfVisualWindowPos]::SWP_SHOWWINDOW
    if (-not [FfVisualWindowPos]::SetWindowPos($hwnd, [IntPtr]::Zero, $X, $Y, $Width, $Height, $flags)) {
        throw "SetWindowPos failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
}

function Get-CatalogRedactions {
    param($Driver, $Main, [int] $ProcessId)
    $rects = [System.Collections.Generic.List[object]]::new()
    $seen = @{}
    $addElementRedaction = {
        param($Element)
        try {
            $br = Get-UiaElementProperty -Driver $Driver -Element $Element -Name 'BoundingRectangle'
            if ($br -and $br.width -gt 0 -and $br.height -gt 0) {
                $key = "$($br.x),$($br.y),$($br.width),$($br.height)"
                if (-not $seen.ContainsKey($key)) {
                    $seen[$key] = $true
                    [void]$rects.Add($br)
                }
            }
        } catch { }
    }
    try {
        $chrome = $null
        foreach ($child in @(Get-UiaChildren -Driver $Driver -Element $Main)) {
            $identity = Get-UiaElementIdentity -Driver $Driver -Element $child
            if ($identity.ClassName -eq 'FastFilesNavigationChrome') { $chrome = $child; break }
        }
        if ($chrome) {
            foreach ($child in @(Get-UiaChildren -Driver $Driver -Element $chrome)) {
                $identity = Get-UiaElementIdentity -Driver $Driver -Element $child
                if ($identity.ClassName -eq 'Edit') {
                    & $addElementRedaction $child
                }
            }
        }
    } catch { }
    # Home and status surfaces can render recent or active filesystem paths as
    # semantic Text elements outside the navigation chrome. Redact every
    # UIA-visible drive/UNC path so committed visual evidence never captures a
    # developer's account name or private folder structure.
    foreach ($pattern in @('*:\*', '\\*\*')) {
        try {
            foreach ($element in @(Find-UiaElement -Driver $Driver -NameLike $pattern -FromElement $Main -TimeoutMs 1000 -All)) {
                & $addElementRedaction $element
            }
        } catch { }
    }
    try {
        foreach ($element in @(Find-UiaElement -Driver $Driver -ControlType 'ListItem' -FromElement $Main -TimeoutMs 1000 -All)) {
            $helpText = ''
            try { $helpText = [string](Get-UiaElementProperty -Driver $Driver -Element $element -Name 'HelpText') } catch { }
            if ($helpText -like '*:\*' -or $helpText -like '\\*\*') {
                & $addElementRedaction $element
            }
        }
    } catch { }
    return @($rects.ToArray())
}

# Mean luminance of a capture (sparse sampling). Used to prove a theme flip
# actually changed pixels instead of asserting the same window title twice.
function Get-CatalogMeanBrightness {
    param([Parameter(Mandatory)][string]$Path)
    Add-Type -AssemblyName System.Drawing
    $bmp = [System.Drawing.Bitmap]::new($Path)
    try {
        $sum = 0.0
        $count = 0
        for ($y = 0; $y -lt $bmp.Height; $y += 16) {
            for ($x = 0; $x -lt $bmp.Width; $x += 16) {
                $px = $bmp.GetPixel($x, $y)
                $sum += (0.299 * $px.R + 0.587 * $px.G + 0.114 * $px.B)
                $count++
            }
        }
        if ($count -eq 0) { return 0.0 }
        return ($sum / $count)
    } finally { $bmp.Dispose() }
}

# Unregistered probe frame: measures current window pixels without claiming
# a SeenHashes slot (real captures register their own hashes afterwards).
# Returns mean luminance, or -1 when the probe fails.
function Get-GalleryProbeMean {
    param($Element, [Parameter(Mandatory)][string]$OutputRoot)
    $probePath = Join-Path $OutputRoot ('gallery-probe-'+[Guid]::NewGuid().ToString('N')+'.tmp.png')
    try {
        $probeBounds = Get-VisualWindowBounds -Element $Element
        Add-Type -AssemblyName System.Drawing
        $probeBmp = [System.Drawing.Bitmap]::new($probeBounds.Width, $probeBounds.Height,
            [System.Drawing.Imaging.PixelFormat]::Format32bppPArgb)
        try {
            $probeGfx = [System.Drawing.Graphics]::FromImage($probeBmp)
            try {
                $probeGfx.CopyFromScreen($probeBounds.X, $probeBounds.Y, 0, 0,
                    [System.Drawing.Size]::new($probeBounds.Width, $probeBounds.Height),
                    [System.Drawing.CopyPixelOperation]::SourceCopy)
            } finally { $probeGfx.Dispose() }
            $probeBmp.Save($probePath, [System.Drawing.Imaging.ImageFormat]::Png)
        } finally { $probeBmp.Dispose() }
        return Get-CatalogMeanBrightness -Path $probePath
    } catch { return -1.0 }
    finally { Remove-Item -LiteralPath $probePath -Force -ErrorAction SilentlyContinue }
}

function Test-CatalogNamed {
    param($Driver, $Main, [int] $ProcessId, [string[]] $Names = @(), [string[]] $AutomationIds = @())
    $found = @()
    $missing = @()
    foreach ($name in @($Names)) {
        try {
            $null = Find-UiaElement -Driver $Driver -Name $name -FromElement $Main -TimeoutMs 1200
            $found += $name
        } catch {
            try {
                $null = Find-UiaElement -Driver $Driver -Name $name -ProcessId $ProcessId -TimeoutMs 800
                $found += $name
            } catch { $missing += $name }
        }
    }
    foreach ($id in @($AutomationIds)) {
        try {
            $null = Find-UiaElement -Driver $Driver -AutomationId $id -FromElement $Main -TimeoutMs 1200
            $found += $id
        } catch {
            try {
                $null = Find-UiaElement -Driver $Driver -AutomationId $id -ProcessId $ProcessId -TimeoutMs 800
                $found += $id
            } catch { $missing += $id }
        }
    }
    return [pscustomobject]@{
        Pass = ($missing.Count -eq 0 -and ($found.Count -gt 0 -or ($Names.Count -eq 0 -and $AutomationIds.Count -eq 0)))
        Found = $found
        Missing = $missing
    }
}

function Set-CatalogTheme {
    param($Driver, [int] $ProcessId, [ValidateSet('Light', 'Dark')][string] $Theme)
    $settings = $null
    try { $settings = Find-UiaElement -Driver $Driver -ClassName 'FastFilesSettingsDialog' -ProcessId $ProcessId -TimeoutMs 2500 } catch { }
    if (-not $settings) { throw 'settings dialog did not appear while selecting a visual theme' }
    $combos = @(Find-UiaElement -Driver $Driver -ClassName 'ComboBox' -FromElement $settings -TimeoutMs 2000 -All)
    if ($combos.Count -lt 1) { throw 'settings theme combo was not discoverable' }
    $index = if ($Theme -eq 'Dark') { 1 } else { 0 }
    $comboHwnd = [IntPtr]$combos[0].Current.NativeWindowHandle
    $selectionResult = if ($comboHwnd -ne [IntPtr]::Zero) {
        [FfVisualSettingsNative]::SendMessage($comboHwnd, [FfVisualSettingsNative]::CB_SETCURSEL,
                                              [IntPtr]$index, [IntPtr]::Zero).ToInt64()
    } else { -1 }
    # The Settings dialog's theme combo intentionally has no child control id;
    # its SaveCurrentPage path reads CB_GETCURSEL on OK, so a direct selection
    # message is the semantic native-control operation available here.
    if ($comboHwnd -eq [IntPtr]::Zero -or $selectionResult -lt 0) {
        throw "could not select $Theme in the settings theme combo"
    }
    $ok = Find-UiaElement -Driver $Driver -AutomationId '1' -FromElement $settings -TimeoutMs 1500
    if (-not $ok) { throw 'settings OK button was not discoverable while selecting a visual theme' }
    $null = Invoke-UiaElementAction -Driver $Driver -Element $ok
    $closed = Wait-UiaElementGone -Driver $Driver -ClassName 'FastFilesSettingsDialog' -ProcessId $ProcessId -TimeoutMs 3000 -PollIntervalMs 200
    if (-not $closed) { throw "settings dialog did not close after selecting $Theme" }
    Start-Sleep -Milliseconds 500
}

# Reads the semantic keyboard/selection state of Pane.0's terminal (deepest)
# column: every visible row with its UIA focus + SelectionItem state. Uses
# only exported driver primitives (direct-child walks, never a cached
# subtree) so stale providers cannot fake a transition.
function Get-CatalogTerminalRowState {
    param($Driver, $Main)
    $result = [pscustomobject]@{
        ColumnFound = $false
        Rows = @()
    }
    try {
        $group = $null
        foreach ($child in @(Get-UiaChildren -Driver $Driver -Element $Main)) {
            $identity = Get-UiaElementIdentity -Driver $Driver -Element $child
            if ($identity.AutomationId -eq 'ColumnView') { $group = $child; break }
        }
        if (-not $group) { return $result }
        foreach ($pane in @(Get-UiaChildren -Driver $Driver -Element $group)) {
            $paneIdentity = Get-UiaElementIdentity -Driver $Driver -Element $pane
            if ($paneIdentity.AutomationId -ne 'Pane.0') { continue }
            $lists = @()
            foreach ($list in @(Get-UiaChildren -Driver $Driver -Element $pane)) {
                $listIdentity = Get-UiaElementIdentity -Driver $Driver -Element $list
                if ($listIdentity.ControlType -eq 'List') { $lists += $list }
            }
            if ($lists.Count -eq 0) { return $result }
            $terminal = $lists[$lists.Count - 1]
            $rows = @()
            foreach ($item in @(Get-UiaChildren -Driver $Driver -Element $terminal)) {
                $itemIdentity = $null
                $focused = $false
                $selected = $false
                try { $itemIdentity = Get-UiaElementIdentity -Driver $Driver -Element $item } catch { }
                try { $focused = [bool](Get-UiaElementProperty -Driver $Driver -Element $item -Name 'HasKeyboardFocus') } catch { }
                try { $selected = [bool](Invoke-UiaPattern -Driver $Driver -Element $item -PatternName 'SelectionItem' -Method 'GetSelection') } catch { }
                $rows += [pscustomobject]@{
                    Name = if ($itemIdentity) { [string]$itemIdentity.Name } else { '' }
                    Focused = $focused
                    Selected = $selected
                }
            }
            $result.ColumnFound = $true
            $result.Rows = @($rows)
            return $result
        }
    } catch { }
    return $result
}

# keyboard-focus-forensics FF-VIS-FOCUS-001: honest keyboard-focus scenario.
# The old code sent {DOWN}, slept 300 ms, and hardcoded Pass=$true, so a
# stranded-focus no-op captured a pixel-identical image and only the
# duplicate-image guard told the truth. This flow proves the semantic
# transition first (before -> input -> bounded poll -> after) and returns
# Pass=$false with diagnostics when no row takes focus; the capture then
# records FAIL instead of a fake PASS. Pixel change is enforced separately
# by the duplicate-image guard (a real focus move must change pixels).
function Invoke-CatalogKeyboardFocus {
    param($Driver, $Main, [int] $ProcessId)
    if (-not (Test-UiaForegroundWindow -Driver $Driver -Element $Main)) {
        return [pscustomobject]@{
            Pass = $false
            Detail = 'FastFiles did not own the foreground before keyboard input; input delivery cannot be guaranteed'
        }
    }
    $populated = Wait-UiaCondition -Driver $Driver -TimeoutMs 15000 -PollIntervalMs 500 `
        -Description 'terminal browse column shows at least two selectable rows' -ScriptBlock {
        $state = Get-CatalogTerminalRowState -Driver $Driver -Main $Main
        if (-not $state.ColumnFound) { return $false }
        $real = @($state.Rows | Where-Object { $_.Name -ne '' -and $_.Name -ne 'Refreshing…' })
        return ($real.Count -ge 2)
    }
    if (-not $populated) {
        $state = Get-CatalogTerminalRowState -Driver $Driver -Main $Main
        return [pscustomobject]@{
            Pass = $false
            Detail = ('terminal column never populated with selectable rows; columnFound=' + $state.ColumnFound + '; rows=' + $state.Rows.Count)
        }
    }
    $before = Get-CatalogTerminalRowState -Driver $Driver -Main $Main
    $beforeFocused = @($before.Rows | Where-Object { $_.Focused } | ForEach-Object { $_.Name })
    $null = Send-UiaInput -Driver $Driver -Keys '{DOWN}'
    $moved = Wait-UiaCondition -Driver $Driver -TimeoutMs 5000 -PollIntervalMs 200 `
        -Description 'a terminal-column row reports keyboard focus after DOWN' -ScriptBlock {
        $state = Get-CatalogTerminalRowState -Driver $Driver -Main $Main
        return (@($state.Rows | Where-Object { $_.Focused }).Count -gt 0)
    }
    $after = Get-CatalogTerminalRowState -Driver $Driver -Main $Main
    $afterFocused = @($after.Rows | Where-Object { $_.Focused })
    $ok = [bool]$moved -and ($afterFocused.Count -eq 1) -and [bool]$afterFocused[0].Selected `
        -and (-not [string]::IsNullOrEmpty($afterFocused[0].Name))
    if (-not $ok) {
        return [pscustomobject]@{
            Pass = $false
            Detail = ('DOWN produced no valid focus transition; before=[' + ($beforeFocused -join ',') + ']; focusedCount=' + $afterFocused.Count)
            Before = @($beforeFocused)
            After = @($afterFocused)
        }
    }
    return [pscustomobject]@{
        Pass = $true
        Detail = ('keyboard focus moved to row ''' + $afterFocused[0].Name + ''' (selected) via DOWN')
        Before = @($beforeFocused)
        After = @($afterFocused[0].Name)
    }
}

function Invoke-CatalogCapture {
    param(
        $Run, $Driver, $Main, [int] $ProcessId,
        [string] $ScenarioId, [string] $ExpectedState, [string] $Theme, [int] $Width,
        [string] $PaneMode, $Assertion, $CaptureElement
    )
    $element = if ($CaptureElement) { $CaptureElement } else { $Main }
    if ($PaneMode -eq 'dialog') {
        try { $null = Set-UiaForeground -Driver $Driver -Element $element } catch { }
    } else {
        Invoke-UiaActivateAppWindow -Driver $Driver -Main $Main
        if (-not (Test-UiaForegroundWindow -Driver $Driver -Element $Main)) {
            $null = Add-VisualScenarioResult -Run $Run -ScenarioId $ScenarioId -ExpectedState $ExpectedState `
                -FixtureId 'sandbox-home' -Theme $Theme -Width $Width -PaneMode $PaneMode -DpiScale 100 `
                -Status 'FAIL' -Reason 'FastFiles was not the foreground window at capture time' -AssertionPass $false `
                -AssertionEvidence $Assertion
            return
        }
    }
    $null = Wait-UiaStableGeometry -Driver $Driver -Element $element -TimeoutMs 4000
    $privacy = Get-CatalogRedactions -Driver $Driver -Main $Main -ProcessId $ProcessId
    if (-not $Assertion.Pass) {
        $null = Add-VisualScenarioResult -Run $Run -ScenarioId $ScenarioId -ExpectedState $ExpectedState `
            -FixtureId 'sandbox-home' -Theme $Theme -Width $Width -PaneMode $PaneMode -DpiScale 100 `
            -Status 'FAIL' -Reason ($Assertion | ConvertTo-Json -Compress) -AssertionPass $false `
            -AssertionEvidence $Assertion
        return
    }
    try {
        $shot = Capture-VisualWindow -Run $Run -Element $element -ScenarioId $ScenarioId -AssertionPass $true `
            -PrivacyTheme $Theme -PrivacyRectangles $privacy
        # Never fabricate DPI: an unobserved value stays 0 so the scenario
        # guard records a truthful failure instead of a bogus 96-DPI PASS.
        $observedDpi = if ($shot.Bounds -and $shot.Bounds.Dpi) { [int]$shot.Bounds.Dpi } else { 0 }
        $dpiCheck = Test-VisualDpiMatch -RequestedPercent 100 -ObservedDpi $observedDpi
        if ($dpiCheck.Known -and -not $dpiCheck.Match) {
            Write-CatalogLog ("DPI_MISMATCH: scenario '$ScenarioId' requested 100% (~$($dpiCheck.ExpectedDpi) DPI) but observed $($dpiCheck.ObservedDpi) DPI")
        }
        $null = Add-VisualScenarioResult -Run $Run -ScenarioId $ScenarioId -ExpectedState $ExpectedState `
            -FixtureId 'sandbox-home' -Theme $Theme -Width $Width -PaneMode $PaneMode -DpiScalePercent 100 -ObservedDpi $observedDpi `
            -Status 'PASS' -AssertionPass $true -AssertionEvidence $Assertion `
            -ScreenshotPath $shot.Path -ImageHash $shot.Hash -PrivacyRedactions $shot.PrivacyRedactions
    } catch {
        $null = Add-VisualScenarioResult -Run $Run -ScenarioId $ScenarioId -ExpectedState $ExpectedState `
            -FixtureId 'sandbox-home' -Theme $Theme -Width $Width -PaneMode $PaneMode -DpiScale 100 `
            -Status 'FAIL' -Reason $_.Exception.Message -AssertionPass $false `
            -AssertionEvidence $Assertion
    }
}

$avail = Get-UiaDriverAvailability
if (-not $avail.Available) {
    Write-CatalogLog "VISUAL_CATALOG_SKIPPED: $($avail.Reason)"
    exit 2
}

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$appSha = Get-CatalogAppSha -Path $exe
$run = New-VisualCaptureRun -Root $OutputRoot -ApplicationSha $appSha
$log = { param($m) Write-CatalogLog $m }
$ctx = $null
$exit = 1
try {
    $ctx = New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId 'visual-catalog' -LogFn $log
    if (-not $ctx -or -not $ctx.Main) {
        throw 'main window did not appear'
    }
    $driver = $ctx.Driver
    $main = $ctx.Main
    $appPid = [int]$ctx.Launched.Id
    Invoke-UiaActivateAppWindow -Driver $driver -Main $main
    $null = Keep-UiaWindowTopmost -Driver $driver -Element $main -Topmost $true
    Set-CatalogWindowSize -Main $main -X 80 -Y 60 -Width 1280 -Height 800
    $null = Keep-UiaWindowTopmost -Driver $driver -Element $main -Topmost $true
    $null = Set-UiaForeground -Driver $driver -Element $main
    $null = Wait-UiaStableGeometry -Driver $driver -Element $main -TimeoutMs 5000

    $null = Invoke-UiaOpenSettings -Driver $driver -Main $main -ProcessId $appPid
    Set-CatalogTheme -Driver $driver -ProcessId $appPid -Theme 'Light'
    $null = Set-UiaForeground -Driver $driver -Element $main

    $columnViewReady = $false
    for ($i = 0; $i -lt 20 -and -not $columnViewReady; $i++) {
        try {
            $null = Find-UiaElement -Driver $driver -AutomationId 'ColumnView' -FromElement $main -TimeoutMs 500
            $columnViewReady = $true
        } catch {
            Start-Sleep -Milliseconds 250
        }
    }
    # Startup may intentionally restore This PC from the user's workspace
    # setting. Establish Home through the real command before capturing it;
    # accepting a merely-live ColumnView here would produce a false-green
    # "Home default" image.
    $homeCmd = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'home' `
        -ExpectedNames @('Home') -ExpectedCommandName 'Home' -ExpectedCommandId 'navigation.home'
    $homeSurface = Test-CatalogNamed -Driver $driver -Main $main -ProcessId $appPid -Names @('1: Home') -AutomationIds @('Pane.0.Home')
    $homeAssert = [pscustomobject]@{
        Pass = [bool]$homeCmd.Pass -and [bool]$homeSurface.Pass
        Command = $homeCmd
        Surface = $homeSurface
    }
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'home-default' -ExpectedState 'Home surface activated through the real Home command' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' -Assertion $homeAssert

    $browseFolder = Join-Path $ctx.FixtureRoot 'CatalogBrowse'
    New-Item -ItemType Directory -Path $browseFolder -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $browseFolder 'readme.txt') -Value 'visual catalog fixture' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $browseFolder 'Documentation_日本語.txt') -Value '日本語のドキュメント' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $browseFolder 'تقرير_عربي_2026.pdf') -Value 'Arabic RTL document fixture' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $browseFolder 'Accents_Français_Élégant.md') -Value 'French accents fixture' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $browseFolder 'Emoji_✨_Report_🚀.log') -Value 'Emoji filename fixture' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $browseFolder 'LongName_Visual_Regression_Testing_Direct2D_Ellipsis_Handling.json') -Value '{}' -Encoding utf8
    $emptySubfolder = Join-Path $browseFolder 'EmptyFolder'
    New-Item -ItemType Directory -Path $emptySubfolder -Force | Out-Null

    $null = Set-UiaForeground -Driver $driver -Element $main
    $nav = Invoke-UiaAddressBarNavigate -Driver $driver -Main $main -ProcessId $appPid -Path $browseFolder -ExpectedLeaf 'CatalogBrowse'
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'browse-column-view' -ExpectedState 'Column View on an international fixture folder with details pane open' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' -Assertion ([pscustomobject]@{ Pass = [bool]$nav.Pass; Detail = $nav.Detail })

    # Keyboard navigation & row focus state (FF-VIS-FOCUS-001): the assertion
    # proves the semantic focus transition; a hardcoded Pass would hide the
    # stranded-focus no-op this scenario once shipped.
    $focusAssert = Invoke-CatalogKeyboardFocus -Driver $driver -Main $main -ProcessId $appPid
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'browse-keyboard-focus' -ExpectedState 'Column View with keyboard-focused item row and selection focus ring' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' -Assertion $focusAssert

    # Empty folder state
    $navEmpty = Invoke-UiaAddressBarNavigate -Driver $driver -Main $main -ProcessId $appPid -Path $emptySubfolder -ExpectedLeaf 'EmptyFolder'
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'browse-empty-state' -ExpectedState 'Column View empty directory placeholder state' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' -Assertion ([pscustomobject]@{ Pass = [bool]$navEmpty.Pass; Detail = $navEmpty.Detail })
    Set-CatalogWindowSize -Main $main -X 80 -Y 60 -Width 1600 -Height 800
    $wideHome = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'home' `
        -ExpectedNames @('Home') -ExpectedCommandName 'Home' -ExpectedCommandId 'navigation.home'
    $wideHomeSurface = Test-CatalogNamed -Driver $driver -Main $main -ProcessId $appPid -Names @('1: Home') -AutomationIds @('Pane.0.Home')
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'home-wide' -ExpectedState 'Home remains usable at 1600px width' `
        -Theme 'Light' -Width 1600 -PaneMode 'single' `
        -Assertion ([pscustomobject]@{ Pass = [bool]$wideHome.Pass -and [bool]$wideHomeSurface.Pass; Command = $wideHome; Surface = $wideHomeSurface })

    Set-CatalogWindowSize -Main $main -X 80 -Y 60 -Width 1280 -Height 800
    $navAgain = Invoke-UiaAddressBarNavigate -Driver $driver -Main $main -ProcessId $appPid -Path $browseFolder -ExpectedLeaf 'CatalogBrowse'
    $detailsOff = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'details' `
        -ExpectedNames @() -ExpectedCommandName 'Toggle Details Pane' -ExpectedCommandId 'view.toggle-details-pane'
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'details-closed' -ExpectedState 'Column View with the details pane closed' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' `
        -Assertion ([pscustomobject]@{ Pass = [bool]$navAgain.Pass -and [bool]$detailsOff.Pass; Navigation = $navAgain; Command = $detailsOff })
    $detailsOn = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'details' `
        -ExpectedNames @() -ExpectedCommandName 'Toggle Details Pane' -ExpectedCommandId 'view.toggle-details-pane'

    $dual = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'dual pane' `
        -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane'
    # Pane presence is asserted against the pane group's direct children: a
    # full-tree DFS for 'Pane.1' is O(whole-window tree) and reliably times out
    # once two panes of columns are loaded, while the UIA contract under test is
    # exactly "the group exposes both pane roots as children".
    $dualControl = $null
    $paneRootIds = @()
    try {
        $dualControl = Find-UiaElement -Driver $driver -AutomationId 'ColumnView' -FromElement $main -TimeoutMs 4000
        foreach ($child in @(Get-UiaChildren -Driver $driver -Element $dualControl)) {
            $paneRootIds += (Get-UiaElementIdentity -Driver $driver -Element $child).AutomationId
        }
    } catch { }
    $dualSurface = [pscustomobject]@{
        Pass = ($paneRootIds -contains 'Pane.0') -and ($paneRootIds -contains 'Pane.1')
        Found = $paneRootIds
        Missing = @(@('Pane.0', 'Pane.1') | Where-Object { $paneRootIds -notcontains $_ })
    }
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'dual-pane' -ExpectedState 'Dual-pane Column View with two semantic pane roots' `
        -Theme 'Light' -Width 1280 -PaneMode 'dual' `
        -Assertion ([pscustomobject]@{ Pass = [bool]$dual.Pass -and [bool]$dualSurface.Pass; Command = $dual; Surface = $dualSurface })
    $single = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'dual pane' `
        -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane'

    $homeCmd = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'home' `
        -ExpectedNames @('Home') -ExpectedCommandName 'Home' -ExpectedCommandId 'navigation.home'
    $homeSurface = Test-CatalogNamed -Driver $driver -Main $main -ProcessId $appPid -Names @('1: Home') -AutomationIds @('Pane.0.Home')
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'home-from-command' -ExpectedState 'Home restored through the command palette' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' `
        -Assertion ([pscustomobject]@{ Pass = [bool]$homeCmd.Pass -and [bool]$homeSurface.Pass; Command = $homeCmd; Surface = $homeSurface })

    $search = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'search' `
        -ExpectedNames @('Filters') -ExpectedCommandName 'Search' -ExpectedCommandId 'search.focus'
    $searchUi = Test-CatalogNamed -Driver $driver -Main $main -ProcessId $appPid -Names @('Filters') -AutomationIds @('Search.Results', '7101')
    $searchAssert = [pscustomobject]@{
        Pass = [bool]$search.Pass
        Detail = $search.Detail
        Ui = $searchUi
    }
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'fast-search-idle' -ExpectedState 'Fast Search workspace with idle empty state; Filters present; no Filters expansion' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' -Assertion $searchAssert

    $filtersButton = $null
    $filterState = $null
    $filtersExpanded = $false
    try {
        $filtersButton = Find-UiaElement -Driver $driver -AutomationId 'Search.Filters' -FromElement $main -TimeoutMs 2000
        $filterState = Invoke-UiaPattern -Driver $driver -Element $filtersButton -PatternName 'ExpandCollapse' -Method 'GetState'
        if ($filterState -eq 'Collapsed') {
            $null = Invoke-UiaPattern -Driver $driver -Element $filtersButton -PatternName 'ExpandCollapse' -Method 'Expand'
            Start-Sleep -Milliseconds 300
            $filtersButton = Find-UiaElement -Driver $driver -AutomationId 'Search.Filters' -FromElement $main -TimeoutMs 2000
            $filterState = Invoke-UiaPattern -Driver $driver -Element $filtersButton -PatternName 'ExpandCollapse' -Method 'GetState'
        }
        $filtersExpanded = $filterState -eq 'Expanded'
    } catch { }
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'search-filters-expanded' -ExpectedState 'Fast Search with the Filters disclosure expanded' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' `
        -Assertion ([pscustomobject]@{ Pass = $filtersExpanded; Detail = "ExpandCollapse state='$filterState'" })
    # Query with no matching results to capture empty search state. The search
    # workspace must still be open here: the earlier ordering closed it first,
    # which made the query edit undiscoverable and skipped this scenario in
    # every catalog run.
    $searchEdit = $null
    try { $searchEdit = Find-UiaElement -Driver $driver -AutomationId '7101' -FromElement $main -TimeoutMs 2000 } catch { }
    if ($searchEdit) {
        $sentinel = 'zzzz_no_matching_files_query_9999'
        $applied = Set-UiaElementValue -Driver $driver -Element $searchEdit -Value $sentinel
        if (-not $applied.ok) {
            # A synthetic value can be ignored by an edit that only reacts to
            # real edits; fall back to actual typing, never to a hardcoded pass.
            try { $searchEdit.SetFocus() } catch { }
            $null = Send-UiaText -Driver $driver -Text $sentinel
        }
        Start-Sleep -Milliseconds 600
        $queryNow = $null
        try { $queryNow = Get-UiaElementTextValue -Driver $driver -Element $searchEdit } catch { }
        $resultsEl = $null
        try { $resultsEl = Find-UiaElement -Driver $driver -AutomationId 'Search.Results' -FromElement $main -TimeoutMs 2000 } catch { }
        $resultChildren = 0
        if ($resultsEl) { $resultChildren = @(Get-UiaChildren -Driver $driver -Element $resultsEl).Count }
        # Honest scope: the query holds the sentinel and the result set is
        # empty. Degraded-mode search has zero candidates in this disposable
        # sandbox, so "a non-empty query produced zero rows" is the strongest
        # semantic contract available without product-side status exposure.
        Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
            -ScenarioId 'search-no-results' -ExpectedState 'Fast Search with zero results and empty search state' `
            -Theme 'Light' -Width 1280 -PaneMode 'single' `
            -Assertion ([pscustomobject]@{ Pass = (([string]$queryNow -eq $sentinel) -and $null -ne $resultsEl -and $resultChildren -eq 0)
                                           Detail = "query='$queryNow' resultsElement=$($null -ne $resultsEl) children=$resultChildren" })
    } else {
        # Never silently drop a scenario: a missing search edit is an
        # explicit SKIPPED record, not an invisible coverage hole.
        $null = Add-VisualScenarioResult -Run $run -ScenarioId 'search-no-results' `
            -ExpectedState 'Fast Search with zero results and empty search state' `
            -FixtureId 'sandbox-home' -Theme 'Light' -Width 1280 -PaneMode 'single' `
            -Status 'SKIPPED' -Reason 'search query edit 7101 was not discoverable' -AssertionPass $false
    }
    $null = Close-UiaSearchPanelIfOpen -Driver $driver -Main $main -ProcessId $appPid
    $storage = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'analyze' `
        -ExpectedNames @('Overview') -ExpectedCommandName 'Analyze Storage' -ExpectedCommandId 'storage.analyze'
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'storage-overview' -ExpectedState 'Storage Analysis workspace with Overview mode' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' `
        -Assertion ([pscustomobject]@{ Pass = [bool]$storage.Pass; Detail = $storage.Detail })

    $settings = Invoke-UiaOpenSettings -Driver $driver -Main $main -ProcessId $appPid
    $settingsEl = $null
    try { $settingsEl = Find-UiaElement -Driver $driver -ClassName 'FastFilesSettingsDialog' -ProcessId $appPid -TimeoutMs 2000 } catch { }
    $settingsAssert = [pscustomobject]@{ Pass = [bool]$settings.Pass -and $null -ne $settingsEl; Detail = $settings.Detail }
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'settings-general' -ExpectedState 'Settings dialog on the General page' `
        -Theme 'Light' -Width 620 -PaneMode 'dialog' -Assertion $settingsAssert -CaptureElement $settingsEl
    $null = Invoke-UiaCloseSettingsDialog -Driver $driver -Main $main

    Invoke-UiaActivateAppWindow -Driver $driver -Main $main
    $null = Send-UiaInput -Driver $driver -Keys '{CTRL}{SHIFT}p'
    Start-Sleep -Milliseconds 800
    $paletteEdit = $null
    try { $paletteEdit = Find-UiaElement -Driver $driver -AutomationId '6301' -FromElement $main -TimeoutMs 3000 } catch { }
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'command-palette' -ExpectedState 'Command palette edit 6301 is visible over the shell' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' `
        -Assertion ([pscustomobject]@{ Pass = $null -ne $paletteEdit; Detail = $(if ($paletteEdit) { 'palette edit present' } else { 'palette edit missing' }) })
    $null = Send-UiaInput -Driver $driver -Keys '{ESC}'
    Start-Sleep -Milliseconds 400

    Set-CatalogWindowSize -Main $main -X 80 -Y 60 -Width 800 -Height 720
    $null = Wait-UiaStableGeometry -Driver $driver -Element $main -TimeoutMs 4000
    $narrowHome = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'home' `
        -ExpectedNames @('Home') -ExpectedCommandName 'Home' -ExpectedCommandId 'navigation.home'
    $narrowSurface = Test-CatalogNamed -Driver $driver -Main $main -ProcessId $appPid -Names @('1: Home') -AutomationIds @('Pane.0.Home')
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'home-narrow' -ExpectedState 'Home remains usable at 800px width' `
        -Theme 'Light' -Width 800 -PaneMode 'single' `
        -Assertion ([pscustomobject]@{ Pass = [bool]$narrowHome.Pass -and [bool]$narrowSurface.Pass; Command = $narrowHome; Surface = $narrowSurface })

    Set-CatalogWindowSize -Main $main -X 80 -Y 60 -Width 1280 -Height 800
    $null = Invoke-UiaOpenSettings -Driver $driver -Main $main -ProcessId $appPid
    Set-CatalogTheme -Driver $driver -ProcessId $appPid -Theme 'Dark'
    Invoke-UiaActivateAppWindow -Driver $driver -Main $main
    $null = Wait-UiaStableGeometry -Driver $driver -Element $main -TimeoutMs 4000
    $darkHome = Test-CatalogNamed -Driver $driver -Main $main -ProcessId $appPid -Names @('1: Home') -AutomationIds @('Pane.0.Home')
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'home-dark' -ExpectedState 'Home after F9 theme toggle' `
        -Theme 'Dark' -Width 1280 -PaneMode 'single' -Assertion $darkHome

    # Gallery matrix (when -Gallery): capture real gallery windows via --gallery flag
    if ($Gallery) {
        Write-CatalogLog 'GALLERY: launching FastFiles with --gallery for real visual capture'
        $galleryCtx = $null
        try {
            $galleryCtx = New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId 'visual-gallery' -LogFn $log -ArgumentList @('--gallery')
            if ($galleryCtx -and $galleryCtx.Main) {
                $galleryMain = $galleryCtx.Main
                $galleryPid = $galleryCtx.Launched.Id
                $galleryDriver = $galleryCtx.Driver

                # 1. Size gallery window to standard 1280x800. A fresh
                # --gallery profile starts dark on this host, so normalize by
                # MEASUREMENT: probe, toggle with F9 only when needed, and
                # label each capture by its observed brightness. A capture is
                # never labeled by assumption.
                Set-CatalogWindowSize -Main $galleryMain -X 80 -Y 60 -Width 1280 -Height 800
                Invoke-UiaActivateAppWindow -Driver $galleryDriver -Main $galleryMain
                $null = Wait-UiaStableGeometry -Driver $galleryDriver -Element $galleryMain -TimeoutMs 4000

                $titleProp = Get-UiaElementProperty -Driver $galleryDriver -Element $galleryMain -Name 'Name'
                $isGallery = $titleProp -like '*Gallery*'
                $launchMean = Get-GalleryProbeMean -Element $galleryMain -OutputRoot $OutputRoot
                if ($launchMean -ge 0 -and $launchMean -lt 128.0) {
                    Write-CatalogLog "GALLERY: launch state is dark (mean $([Math]::Round($launchMean,1))); toggling to light"
                    $null = Send-UiaInput -Driver $galleryDriver -Keys '{F9}'
                    Start-Sleep -Milliseconds 600
                    Invoke-UiaActivateAppWindow -Driver $galleryDriver -Main $galleryMain
                    $null = Wait-UiaStableGeometry -Driver $galleryDriver -Element $galleryMain -TimeoutMs 4000
                }
                $lightMean = Get-GalleryProbeMean -Element $galleryMain -OutputRoot $OutputRoot
                $lightIsLight = ($lightMean -gt 128.0)
                $galleryAssertLight = [pscustomobject]@{
                    Pass = [bool]$isGallery -and $lightIsLight
                    Detail = "window title='$titleProp'; measured mean=$([Math]::Round($lightMean,1)) (light requires >128)"
                }
                Invoke-CatalogCapture -Run $run -Driver $galleryDriver -Main $galleryMain -ProcessId $galleryPid `
                    -ScenarioId 'gallery-light' -ExpectedState 'Component gallery light (real Direct2D capture)' `
                    -Theme 'Light' -Width 1280 -PaneMode 'single' -Assertion $galleryAssertLight

                # 2. Toggle to Dark theme using F9 shortcut and prove the flip
                # changed pixels: the dark frame must be strictly darker than
                # the measured light frame, otherwise the toggle did not take
                # effect and a dark PASS would be fabricated.
                $null = Send-UiaInput -Driver $galleryDriver -Keys '{F9}'
                Start-Sleep -Milliseconds 600
                Invoke-UiaActivateAppWindow -Driver $galleryDriver -Main $galleryMain
                $null = Wait-UiaStableGeometry -Driver $galleryDriver -Element $galleryMain -TimeoutMs 4000

                $darkMean = Get-GalleryProbeMean -Element $galleryMain -OutputRoot $OutputRoot
                $darkFlipped = ($lightMean -gt 128.0 -and $darkMean -ge 0 -and $darkMean -lt ($lightMean * 0.9))
                $galleryAssertDark = [pscustomobject]@{
                    Pass = [bool]$isGallery -and $darkFlipped
                    Detail = "window title='$titleProp' after F9 dark toggle; lightMean=$([Math]::Round($lightMean,1)) darkMean=$([Math]::Round($darkMean,1))"
                }
                Invoke-CatalogCapture -Run $run -Driver $galleryDriver -Main $galleryMain -ProcessId $galleryPid `
                    -ScenarioId 'gallery-dark' -ExpectedState 'Component gallery dark (real Direct2D capture)' `
                    -Theme 'Dark' -Width 1280 -PaneMode 'single' -Assertion $galleryAssertDark

                # 3. Geometry variants from the shared GallerySizeVariants matrix:
                # narrow (800x720, single column) and wide (1600x800) in light.
                $null = Send-UiaInput -Driver $galleryDriver -Keys '{F9}'
                Start-Sleep -Milliseconds 600
                Invoke-UiaActivateAppWindow -Driver $galleryDriver -Main $galleryMain
                Set-CatalogWindowSize -Main $galleryMain -X 80 -Y 60 -Width 800 -Height 720
                $null = Wait-UiaStableGeometry -Driver $galleryDriver -Element $galleryMain -TimeoutMs 4000
                $narrowTitle = Get-UiaElementProperty -Driver $galleryDriver -Element $galleryMain -Name 'Name'
                $narrowMean = Get-GalleryProbeMean -Element $galleryMain -OutputRoot $OutputRoot
                Invoke-CatalogCapture -Run $run -Driver $galleryDriver -Main $galleryMain -ProcessId $galleryPid `
                    -ScenarioId 'gallery-narrow' -ExpectedState 'Component gallery narrow single-column (real Direct2D capture)' `
                    -Theme 'Light' -Width 800 -PaneMode 'single' `
                    -Assertion ([pscustomobject]@{ Pass = ($narrowTitle -like '*Gallery*') -and ($narrowMean -gt 128.0); Detail = "window title='$narrowTitle' at 800px; mean=$([Math]::Round($narrowMean,1))" })
                Set-CatalogWindowSize -Main $galleryMain -X 80 -Y 60 -Width 1600 -Height 800
                $null = Wait-UiaStableGeometry -Driver $galleryDriver -Element $galleryMain -TimeoutMs 4000
                $wideTitle = Get-UiaElementProperty -Driver $galleryDriver -Element $galleryMain -Name 'Name'
                $wideMean = Get-GalleryProbeMean -Element $galleryMain -OutputRoot $OutputRoot
                Invoke-CatalogCapture -Run $run -Driver $galleryDriver -Main $galleryMain -ProcessId $galleryPid `
                    -ScenarioId 'gallery-wide' -ExpectedState 'Component gallery wide two-column (real Direct2D capture)' `
                    -Theme 'Light' -Width 1600 -PaneMode 'single' `
                    -Assertion ([pscustomobject]@{ Pass = ($wideTitle -like '*Gallery*') -and ($wideMean -gt 128.0); Detail = "window title='$wideTitle' at 1600px; mean=$([Math]::Round($wideMean,1))" })
            } else {
                throw 'failed to launch gallery window with --gallery'
            }
        } catch {
            Write-CatalogLog "GALLERY_CAPTURE_ERROR: $($_.Exception.Message)"
            $null = Add-VisualScenarioResult -Run $run -ScenarioId 'gallery-error' -ExpectedState 'gallery launch and capture' `
                -FixtureId 'gallery' -Theme 'Light' -Width 1280 -PaneMode 'single' `
                -Status 'FAIL' -Reason $_.Exception.Message -AssertionPass $false
        } finally {
            if ($galleryCtx) {
                $null = Remove-UiaScenarioContext -Context $galleryCtx -LogFn $log
            }
        }
    }
    # Explicit coverage matrix: every intended scenario classifies as TESTED /
    # FAILED / SKIPPED / BLOCKED_ENVIRONMENT / SUPPORTED_NOT_SELECTED /
    # REQUIRED_BUT_UNAVAILABLE / NOT_APPLICABLE. Observed evidence only: a
    # supported-but-untested configuration is never reported as tested.
    $totalRecords = $run.Records.Count
    $passCount = @($run.Records | Where-Object { $_.status -eq 'PASS' }).Count
    $failCount = @($run.Records | Where-Object { $_.status -eq 'FAIL' }).Count
    $skipCount = @($run.Records | Where-Object { $_.status -eq 'SKIPPED' -or $_.status -eq 'BLOCKED_ENVIRONMENT' }).Count
    $uniqueThemes = @($run.Records | ForEach-Object { $_.theme } | Select-Object -Unique)
    $uniqueWidths = @($run.Records | ForEach-Object { $_.width } | Select-Object -Unique)
    $testedIds = @($run.Records | Where-Object { $_.status -eq 'PASS' } | ForEach-Object { $_.scenarioId })
    $failedIds = @($run.Records | Where-Object { $_.status -eq 'FAIL' } | ForEach-Object { $_.scenarioId })
    $skippedIds = @($run.Records | Where-Object { $_.status -eq 'SKIPPED' -or $_.status -eq 'BLOCKED_ENVIRONMENT' } | ForEach-Object { $_.scenarioId })
    Write-CatalogLog '=== VISUAL SCENARIO COVERAGE MATRIX ==='
    Write-CatalogLog "Total scenarios recorded: $totalRecords (Passed: $passCount, Failed: $failCount, Skipped/Blocked: $skipCount)"
    Write-CatalogLog "Themes: $($uniqueThemes -join ', ') | Viewport widths: $($uniqueWidths -join ', ')"
    Write-CatalogLog "Surfaces covered: Home, ColumnView, DualPane, Details, Search, Storage, Settings, Palette, Gallery"
    Write-CatalogLog "COVERAGE TESTED ($($testedIds.Count)): $($testedIds -join ', ')"
    if ($failedIds.Count -gt 0) { Write-CatalogLog "COVERAGE FAILED ($($failedIds.Count)): $($failedIds -join ', ')" }
    if ($skippedIds.Count -gt 0) { Write-CatalogLog "COVERAGE SKIPPED/BLOCKED ($($skippedIds.Count)): $($skippedIds -join ', ')" }
    if (-not $Gallery) {
        Write-CatalogLog 'COVERAGE SUPPORTED_NOT_SELECTED: gallery-light, gallery-dark, gallery-narrow, gallery-wide (re-run with -Gallery)'
    }
    Write-CatalogLog 'COVERAGE REQUIRED_BUT_UNAVAILABLE: dpi-125/150/200 live (host exposes 96 DPI only), engine-backed rows/previews/file-operations (require engine-served snapshot data)'
    Write-CatalogLog 'COVERAGE NOT_APPLICABLE: ACL-mutation access-denied, destructive confirmations, settings reset (never run by policy: no user-system mutation)'
    $exit = 0
} catch {
    Write-CatalogLog "VISUAL_CATALOG_ERROR: $($_.Exception.Message)"
    $null = Add-VisualScenarioResult -Run $run -ScenarioId 'catalog-lifecycle' -ExpectedState 'sandbox launch and drive' `
        -FixtureId 'sandbox-home' -Theme 'Light' -Width 1280 -PaneMode 'single' `
        -Status 'FAIL' -Reason $_.Exception.Message -AssertionPass $false
    $exit = 1
} finally {
    if ($ctx) {
        $null = Remove-UiaScenarioContext -Context $ctx -LogFn $log
    }
    $written = Write-VisualCaptureRun -Run $run
    Write-CatalogLog "catalog manifest: $($written.Manifest)"
    Write-CatalogLog "catalog summary: $($written.Summary)"
}

$failed = @($run.Records | Where-Object { $_.status -eq 'FAIL' }).Count
if ($failed -gt 0 -and $exit -eq 0) { $exit = 1 }
exit $exit
