# column-uia-identity-dispatch: real owner-window focus/reveal qualification.
# Run with other UI sessions and session-pipe CTests stopped. Captures stay local.
[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path,
    [ValidateSet('normal', 'narrow-dual')][string] $Mode = 'normal',
    [string] $ArtifactsDir = ''
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
if (-not $ArtifactsDir) {
    $ArtifactsDir = Join-Path $repo ('build/column-focus-visibility/' + [guid]::NewGuid().ToString('N'))
}
$artifacts = [IO.Path]::GetFullPath($ArtifactsDir)
if (Test-Path -LiteralPath $artifacts) { throw 'A fresh artifact directory is required; existing files are preserved.' }
New-Item -ItemType Directory -Path $artifacts | Out-Null
$exe = Join-Path $repo 'build/debug-diagnostic/src/ui/FastFiles.exe'
Import-Module (Join-Path $repo 'verify/core/Provenance.psm1') -Force -DisableNameChecking
$binaries = @(Get-DefaultCertificationBinaries -RepoRoot $repo -BuildDirectory (Join-Path $repo 'build/debug-diagnostic'))
$gate = Invoke-ProvenanceGate -RepoRoot $repo -Binaries $binaries -Mode 'exact-tree' -Stage 'column-focus-preflight' -PassThru
$gate.evidence | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $artifacts 'provenance.json') -Encoding utf8NoBOM
if (-not $gate.ok) { throw $gate.message }
Import-Module (Join-Path $repo 'verify/uia-driver/UiaDriver.psm1') -Force -DisableNameChecking
$cap = Import-Module (Join-Path $repo 'verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1') -Force -DisableNameChecking -PassThru
Import-Module (Join-Path $repo 'verify/visual/VisualCapture.psm1') -Force -DisableNameChecking

function Wait-VisibleRow {
    param($Item, $Pane, [switch] $RequireFocus)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $state = $Item.Current
        $row = $state.BoundingRectangle
        $clip = $Pane.Current.BoundingRectangle
        $visible = -not $state.IsOffscreen -and $row.Width -gt 0 -and $row.Height -gt 0 -and
            $row.Top -ge $clip.Top - 1 -and $row.Bottom -le $clip.Bottom + 1 -and
            $row.Left -ge $clip.Left - 1 -and $row.Left -lt $clip.Right -and
            (($row.Width -gt $clip.Width -and [Math]::Abs($row.Left - $clip.Left) -le 1) -or
             ($row.Width -le $clip.Width -and $row.Right -le $clip.Right + 1)) -and
            (-not $RequireFocus -or $state.HasKeyboardFocus)
        if (-not $visible) { Start-Sleep -Milliseconds 50 }
    } while (-not $visible -and $watch.ElapsedMilliseconds -lt 5000)
    if (-not $visible) { throw "Row is outside its pane: row=$row; pane=$clip; focus=$($state.HasKeyboardFocus)" }
    return [pscustomobject]@{ row = "$row"; pane = "$clip"; hasKeyboardFocus = $state.HasKeyboardFocus; offscreen = $state.IsOffscreen }
}

if (-not ('FfColumnFocusWindow' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class FfColumnFocusWindow {
 [DllImport("user32.dll", SetLastError=true)]
 public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
}
$fixture = Join-Path $artifacts 'fixture'
New-Item -ItemType Directory -Path (Join-Path $fixture 'Project') -Force | Out-Null
'owned synthetic fixture' | Set-Content (Join-Path $fixture '.fastfiles-focus-fixture')
'focused synthetic file' | Set-Content (Join-Path $fixture 'Project/report.txt')
'context file' | Set-Content (Join-Path $fixture 'Project/notes.txt')
$sandbox = & $cap { New-UiaSessionSandbox -ScenarioId 'column-focus-visibility' -InvocationId ([guid]::NewGuid().ToString('N')) }
$stateDir = Join-Path $sandbox.SessionRoot 'LocalAppData/FastFiles'
New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
@{version=1;lastLocation=$fixture;lastLocationIsHome=$false} | ConvertTo-Json |
    Set-Content (Join-Path $stateDir 'workspace-state.json') -Encoding utf8NoBOM
$result = [ordered]@{ status = 'FAIL'; mode = $Mode; observations = @(); screenshot = $null; cleanup = $null; detail = '' }
$ctx = $null
try {
    $ctx = New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId 'column-focus-visibility' -SessionRoot $sandbox.SessionRoot -LogFn { param($m) Write-Host $m }
    $folder = Find-UiaElement -Driver $ctx.Driver -Name 'Project' -ControlType ListItem -FromElement $ctx.Main -TimeoutMs 8000
    Invoke-UiaPattern -Driver $ctx.Driver -Element $folder -PatternName Invoke -Method Invoke | Out-Null
    if ($Mode -eq 'narrow-dual') {
        $dual = & $cap { param($c) Invoke-UiaPaletteCommand -Driver $c.Driver -Main $c.Main -ProcessId $c.Launched.Id -Query 'dual pane' -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane' } $ctx
        if (-not $dual.Pass) { throw 'Dual-pane command was not accepted' }
        if (-not [FfColumnFocusWindow]::SetWindowPos([IntPtr]$ctx.Main.Current.NativeWindowHandle, [IntPtr]::Zero, 40, 32, 620, 780, 0x44)) { throw 'Owned window resize failed' }
    }
    $pane = Find-UiaElement -Driver $ctx.Driver -AutomationId 'Pane.0' -FromElement $ctx.Main -TimeoutMs 5000
    $item = Find-UiaElement -Driver $ctx.Driver -Name 'report.txt' -ControlType ListItem -FromElement $pane -TimeoutMs 8000
    Invoke-UiaPattern -Driver $ctx.Driver -Element $item -PatternName SelectionItem -Method Select | Out-Null
    $null = Set-UiaForeground -Driver $ctx.Driver -Element $ctx.Main
    $ctx.Main.SetFocus()
    $browseGroup = Find-UiaElement -Driver $ctx.Driver -AutomationId 'ColumnView' -FromElement $ctx.Main -TimeoutMs 5000
    $browseGroup.SetFocus()
    $item.SetFocus()
    $result.observations += Wait-VisibleRow -Item $item -Pane $pane -RequireFocus
    if ($Mode -eq 'narrow-dual') {
        $otherPane = Find-UiaElement -Driver $ctx.Driver -AutomationId 'Pane.1' -FromElement $ctx.Main -TimeoutMs 5000
        $otherItem = Find-UiaElement -Driver $ctx.Driver -Name 'report.txt' -ControlType ListItem -FromElement $otherPane -TimeoutMs 5000
        Invoke-UiaPattern -Driver $ctx.Driver -Element $otherItem -PatternName ScrollItem -Method ScrollIntoView | Out-Null
        $result.observations += Wait-VisibleRow -Item $otherItem -Pane $otherPane
        if (-not $item.Current.HasKeyboardFocus -or $otherItem.Current.HasKeyboardFocus) { throw 'Inactive-pane scroll changed focused item' }
        $result.observations += Wait-VisibleRow -Item $item -Pane $pane -RequireFocus
    }
    $appHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    $run = New-VisualCaptureRun -Root $artifacts -ApplicationSha $appHash -ExecutableIdentity $gate.evidence.setIdentity -RepoRoot $repo
    $result.screenshot = Capture-VisualWindow -Run $run -Element $ctx.Main -ScenarioId ('column-focus-' + $Mode) -AssertionPass $true
    $result.status = 'PASS'
    $result.detail = if ($Mode -eq 'narrow-dual') {
        'Original row focus, physical visibility and inactive-pane preservation passed; screenshot requires actual visual review.'
    } else {
        'Original row focus and physical visibility passed; screenshot requires actual visual review.'
    }
} catch {
    $result.detail = $_.Exception.Message
} finally {
    if ($ctx) {
        $result.cleanup = Remove-UiaScenarioContext -Context $ctx -LogFn { param($m) Write-Host $m }
        if (-not $result.cleanup.Pass) { $result.status = 'FAIL'; $result.detail += '; teardown failed' }
    }
}
$result | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $artifacts 'result.json') -Encoding utf8NoBOM
$result | ConvertTo-Json -Depth 20
if ($result.status -eq 'PASS') { exit 0 }
exit 1
