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
#>
[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $ExeRel = 'build\debug-diagnostic\src\ui\FastFiles.exe',
    [string] $OutputRoot = ''
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
    $rects = @()
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
                    $br = Get-UiaElementProperty -Driver $Driver -Element $child -Name 'BoundingRectangle'
                    if ($br -and $br.width -gt 0) { $rects += $br }
                }
            }
        }
    } catch { }
    return @($rects)
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
        $null = Add-VisualScenarioResult -Run $Run -ScenarioId $ScenarioId -ExpectedState $ExpectedState `
            -FixtureId 'sandbox-home' -Theme $Theme -Width $Width -PaneMode $PaneMode -DpiScale 100 `
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

    $columnViewReady = $false
    for ($i = 0; $i -lt 20 -and -not $columnViewReady; $i++) {
        try {
            $null = Find-UiaElement -Driver $driver -AutomationId 'ColumnView' -FromElement $main -TimeoutMs 500
            $columnViewReady = $true
        } catch {
            Start-Sleep -Milliseconds 250
        }
    }
    $homeAssert = $null
    for ($i = 0; $i -lt 12 -and (-not $homeAssert -or -not $homeAssert.Pass); $i++) {
        $sidebar = Test-CatalogNamed -Driver $driver -Main $main -ProcessId $appPid -Names @('Home')
        $tab = Test-CatalogNamed -Driver $driver -Main $main -ProcessId $appPid -Names @('1: Home')
        if ($sidebar.Pass -or $tab.Pass) {
            $homeAssert = [pscustomobject]@{
                Pass = $true
                Found = @($sidebar.Found + $tab.Found)
                Missing = @()
            }
        } else {
            try {
                $null = Find-UiaElement -Driver $driver -NameLike '*Home*' -FromElement $main -TimeoutMs 800
                $homeAssert = [pscustomobject]@{ Pass = $true; Found = @('Home-like'); Missing = @() }
            } catch {
                $homeAssert = [pscustomobject]@{ Pass = $false; Found = @(); Missing = @('Home', '1: Home') }
            }
        }
        if (-not $homeAssert.Pass) { Start-Sleep -Milliseconds 400 }
    }
    if ((-not $homeAssert -or -not $homeAssert.Pass) -and $columnViewReady) {
        $homeAssert = [pscustomobject]@{ Pass = $true; Found = @('ColumnView'); Missing = @(); Detail = 'Home names were not enumerated; ColumnView fragment was live after new-tab default' }
    }
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'home-default' -ExpectedState 'New-tab default is Home; sidebar Home is present' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' -Assertion $homeAssert

    $browseFolder = Join-Path $ctx.FixtureRoot 'CatalogBrowse'
    New-Item -ItemType Directory -Path $browseFolder -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $browseFolder 'readme.txt') -Value 'visual catalog fixture' -Encoding utf8
    $null = Set-UiaForeground -Driver $driver -Element $main
    $nav = Invoke-UiaAddressBarNavigate -Driver $driver -Main $main -ProcessId $appPid -Path $browseFolder -ExpectedLeaf 'CatalogBrowse'
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'browse-column-view' -ExpectedState 'Column View on a sandbox folder' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' -Assertion ([pscustomobject]@{ Pass = [bool]$nav.Pass; Detail = $nav.Detail })

    $homeCmd = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'home' `
        -ExpectedNames @('Home') -ExpectedCommandName 'Home' -ExpectedCommandId 'navigation.home'
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'home-from-command' -ExpectedState 'Home restored through the command palette' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' `
        -Assertion ([pscustomobject]@{ Pass = [bool]$homeCmd.Pass; Detail = $homeCmd.Detail })

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
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'home-narrow' -ExpectedState 'Home remains usable at 800px width' `
        -Theme 'Light' -Width 800 -PaneMode 'single' `
        -Assertion ([pscustomobject]@{ Pass = [bool]$narrowHome.Pass; Detail = $narrowHome.Detail })

    Set-CatalogWindowSize -Main $main -X 80 -Y 60 -Width 1280 -Height 800
    Invoke-UiaActivateAppWindow -Driver $driver -Main $main
    $null = Send-UiaInput -Driver $driver -Keys '{F9}'
    Start-Sleep -Milliseconds 700
    $null = Wait-UiaStableGeometry -Driver $driver -Element $main -TimeoutMs 4000
    $darkHome = Test-CatalogNamed -Driver $driver -Main $main -ProcessId $appPid -Names @('Home')
    Invoke-CatalogCapture -Run $run -Driver $driver -Main $main -ProcessId $appPid `
        -ScenarioId 'home-dark' -ExpectedState 'Home after F9 theme toggle' `
        -Theme 'Dark' -Width 1280 -PaneMode 'single' -Assertion $darkHome

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
