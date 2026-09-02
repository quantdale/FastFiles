<#
    Focused before/after capture for a single visual closure finding.

    Unlike the full catalog (per-run GUID session paths + UIA-driven privacy
    overpaint, both of which confound pixel comparison), this script browses a
    FIXED fixture path so the only variable between two runs is the product
    binary. Captures are written under verify/runs/ (gitignored execution
    evidence, never committed) with no redaction overpaint, so the raw pixels
    are directly comparable with Compare-VisualBaseline.ps1.

    Usage:
      pwsh ./verify/visual/Capture-ClosureState.ps1 -Exe <FastFiles.exe> -OutPng <path> -RunRoot <dir>
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Exe,
    [Parameter(Mandatory)][string] $OutPng,
    [Parameter(Mandatory)][string] $RunRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { throw "exe missing: $Exe" }

Import-Module (Join-Path $repo 'verify\uia-driver\UiaDriver.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'verify\capabilities\windows-ui-automation-validation\UiAutomationValidation.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'verify\capabilities\windows-ui-automation-validation\UiAutomationWin32.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $repo 'verify\visual\VisualCapture.psm1') -Force -DisableNameChecking

if (-not ('FfClosureWindowPos' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FfClosureWindowPos
{
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);
    public const uint SWP_NOZORDER = 0x0004;
    public const uint SWP_SHOWWINDOW = 0x0040;
}
'@
}

# Fixed fixture: identical chain on every run, no user name, no GUID.
$fixtureParent = Join-Path $repo 'build\closure-fixture'
$fixture = Join-Path $fixtureParent 'CatalogBrowse'
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Set-Content -LiteralPath (Join-Path $fixture 'readme.txt') -Value 'visual closure fixture' -Encoding utf8
Set-Content -LiteralPath (Join-Path $fixture 'notes.md') -Value '# closure fixture' -Encoding utf8

New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
$log = { param($m) Write-Host $m }
$ctx = $null
try {
    $ctx = New-UiaScenarioContext -Exe $Exe -RepoRoot $repo -ScenarioId 'visual-closure' -LogFn $log
    if (-not $ctx -or -not $ctx.Main) { throw 'main window did not appear' }
    $driver = $ctx.Driver
    $main = $ctx.Main
    $appPid = [int]$ctx.Launched.Id
    Invoke-UiaActivateAppWindow -Driver $driver -Main $main
    $null = Keep-UiaWindowTopmost -Driver $driver -Element $main -Topmost $true
    $hwnd = [IntPtr]$main.Current.NativeWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { throw 'main window has no HWND' }
    $flags = [FfClosureWindowPos]::SWP_NOZORDER -bor [FfClosureWindowPos]::SWP_SHOWWINDOW
    if (-not [FfClosureWindowPos]::SetWindowPos($hwnd, [IntPtr]::Zero, 80, 60, 1280, 800, $flags)) {
        throw 'SetWindowPos failed'
    }
    $null = Set-UiaForeground -Driver $driver -Element $main
    $null = Wait-UiaStableGeometry -Driver $driver -Element $main -TimeoutMs 5000

    $nav = Invoke-UiaAddressBarNavigate -Driver $driver -Main $main -ProcessId $appPid -Path $fixture -ExpectedLeaf 'CatalogBrowse'
    if (-not $nav.Pass) { throw "address-bar navigate failed: $($nav.Detail)" }
    $null = Wait-UiaStableGeometry -Driver $driver -Element $main -TimeoutMs 4000
    # Deterministic selection: the status strip summarizes the active selection.
    $null = Send-UiaInput -Driver $driver -Keys '{CTRL}a'
    Start-Sleep -Milliseconds 600
    $null = Wait-UiaStableGeometry -Driver $driver -Element $main -TimeoutMs 4000

    $run = New-VisualCaptureRun -Root $RunRoot -ApplicationSha (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant()
    $shot = Capture-VisualWindow -Run $run -Element $main -ScenarioId 'closure-state' -AssertionPass $true -PrivacyRectangles @()
    Copy-Item -LiteralPath $shot.Path -Destination $OutPng -Force
    Write-Host "closure capture: $OutPng hash=$($shot.Hash) bounds=$($shot.Bounds.Width)x$($shot.Bounds.Height) dpi=$($shot.Bounds.Dpi)"
} finally {
    if ($ctx) { $null = Remove-UiaScenarioContext -Context $ctx -LogFn $log }
}
