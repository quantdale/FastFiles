[CmdletBinding()]
param(
    [string]$RepoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path,
    [Parameter(Mandatory)][string]$ArtifactsDir
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath $RepoRoot).Path
$artifacts=[IO.Path]::GetFullPath($ArtifactsDir)
if(Test-Path -LiteralPath $artifacts) {throw 'Fresh evidence directory required; existing artifacts preserved.'}
$null=New-Item -ItemType Directory -Path $artifacts
Import-Module (Join-Path $repo 'verify/core/Provenance.psm1') -Force -DisableNameChecking
$gate=Invoke-ProvenanceGate -RepoRoot $repo -Binaries @(Get-DefaultCertificationBinaries -RepoRoot $repo -BuildDirectory (Join-Path $repo 'build/debug-diagnostic')) -Mode exact-tree -Stage palette-visible -PassThru
$gate | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $artifacts 'provenance.json')
if(-not $gate.ok) {throw $gate.message}
Import-Module (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'verify/visual/VisualCapture.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'verify/uia-driver/UiaDriver.psm1') -Force -DisableNameChecking
$cap=Get-Module UiAutomationValidation
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FfPaletteMeasure {
 [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@
$run=New-VisualCaptureRun -Root (Join-Path $artifacts 'images') -ApplicationSha (Get-FileHash (Join-Path $repo 'build/debug-diagnostic/src/ui/FastFiles.exe') -Algorithm SHA256).Hash.ToLowerInvariant() -RepoRoot $repo
$observations=@();$cleanups=@();$failure=$null;$ctx=$null
function Check($condition,[string]$message) {if(-not $condition) {throw $message};Write-Host ('PASS: '+$message)}
function State { & $cap {param($c) Get-UiaPaletteSemanticState -Driver $c.Driver -Main $c.Main -ProcessId $c.Launched.Id} $ctx }
function Input([string]$keys) {
    Check (Test-UiaForegroundWindow -Driver $ctx.Driver -Element $ctx.Main) 'owned foreground before keyboard input'
    $null=Send-UiaInput -Driver $ctx.Driver -Keys $keys
}
function Controls {
    $edit=Wait-UiaDirectChild -Driver $ctx.Driver -AutomationId 6301 -ClassName Edit -ProcessId $ctx.Launched.Id -Parent $ctx.Main -TimeoutMs 4000
    $list=Wait-UiaDirectChild -Driver $ctx.Driver -AutomationId 6302 -ClassName ListBox -ProcessId $ctx.Launched.Id -Parent $ctx.Main -TimeoutMs 4000
    return @{edit=$edit;list=$list}
}
function Observe([string]$id,[int]$expectedRows=0,[string]$expectedDuplicate='') {
    $controls=Controls;$state=State
    Check ($state.discoverySucceeded -and $state.editPresent -and $state.listPresent -and $state.editHasKeyboardFocus) "$id real edit/list and query keyboard focus"
    $edit=$controls.edit.Current.BoundingRectangle;$list=$controls.list.Current.BoundingRectangle;$window=$ctx.Main.Current.BoundingRectangle
    $row=[FfPaletteMeasure]::SendMessageW([IntPtr]$controls.list.Current.NativeWindowHandle,0x1A1,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
    Check ($row -gt 0 -and $list.Height -gt 0 -and ([int]$list.Height % $row) -eq 0) "$id complete native rows"
    if($expectedRows) {Check ($list.Height -eq ($row*$expectedRows)) "$id content-sized list"}
    Check ($window.Contains($edit) -and $window.Contains($list) -and $list.Top -ge $edit.Bottom) "$id reachable non-overlapping UIA bounds"
    $duplicate=@{}
    if($expectedDuplicate) {
        $duplicate=@{ExpectedDuplicateOf=$expectedDuplicate;IntentionalDuplicateReason='Live resize intentionally returns to the same singleton query, window size and geometry observed earlier.'}
    }
    $shot=Capture-VisualWindow -Run $run -Element $ctx.Main -ScenarioId $id -AssertionPass $true @duplicate
    Check ($shot.Bounds.Dpi -gt 0) "$id measured screenshot DPI"
    $script:observations+=@{id=$id;state=$state;edit=@{x=$edit.X;y=$edit.Y;width=$edit.Width;height=$edit.Height};list=@{x=$list.X;y=$list.Y;width=$list.Width;height=$list.Height};row=$row;screenshot=$shot}
    return $edit.Y
}
function Query([string]$text) {
    $state=State
    Check $state.editHasKeyboardFocus 'query owns focus before replacing text'
    Input '{CTRL}a'
    if($text) {$null=Send-UiaText -Driver $ctx.Driver -Text $text} else {Input '{BACKSPACE}'}
    $ready=Wait-UiaCondition -Driver $ctx.Driver -TimeoutMs 4000 -PollIntervalMs 150 -Description 'exact live query' -ScriptBlock {
        $controls=Controls
        return (Get-UiaElementTextValue -Driver $ctx.Driver -Element $controls.edit) -eq $text
    }
    Check $ready 'native edit retains exact query'
}
try {
 foreach($theme in @('Light','Dark')) {foreach($density in @('Compact','Comfortable')) {
    $sandbox=& $cap { New-UiaSessionSandbox -ScenarioId compact-palette -InvocationId ([guid]::NewGuid().ToString('N')) }
    $settings=Join-Path $sandbox.SessionRoot 'LocalAppData/FastFiles'
    $null=New-Item -ItemType Directory -Path $settings -Force
    @{schemaVersion=1;appearance=@{theme=$theme;density=$density}} | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $settings 'settings.json') -Encoding utf8NoBOM
    $ctx=New-UiaScenarioContext -Exe (Join-Path $repo 'build/debug-diagnostic/src/ui/FastFiles.exe') -RepoRoot $repo -ScenarioId "palette-$theme-$density" -SessionRoot $sandbox.SessionRoot -LogFn {param($m) Write-Host $m}
    foreach($width in @(620,1008,1600)) {
        Check (Set-UiaWindowPosition -Driver $ctx.Driver -Element $ctx.Main -X 30 -Y 30 -Width $width -Height 700).ok 'resize owned window'
        Check (& $cap {param($c) Ensure-UiaInputForeground -Driver $c.Driver -Main $c.Main -Attempts 2} $ctx) 'activate owned window'
        Input '{CTRL}{SHIFT}p'
        $anchor=Observe "$theme-$density-$width-many"
        Query 'Cancel File Operations'
        Check ((State).listItems.Count -eq 1 -and (State).selectedName -eq 'Cancel File Operations') 'idle cancel is one discoverable native result'
        Check ((Observe "$theme-$density-$width-single" 1) -eq $anchor) 'filter preserves query position'
        Query 'zzzz-no-such-fastfiles-command'
        Check ((State).listItems.Count -eq 1 -and (State).listItems[0] -eq 'No matching commands') 'no-match native recovery row'
        Check ((Observe "$theme-$density-$width-none" 1) -eq $anchor) 'no-match preserves query position'
        Input '{ENTER}'
        Check ((State).editPresent -and (State).listItems[0] -eq 'No matching commands') 'no-match Enter is inert'
        Query ''
        Check ((State).listItems.Count -gt 1) 'cleared query restores commands'
        Input '{DOWN}'
        Check ((State).selectedIndex -eq 1 -and (State).editHasKeyboardFocus) 'Down selects second result while retaining query focus'
        Input '{UP}'
        Check ((State).selectedIndex -eq 0) 'Up restores first result'
        if($width -eq 1008) {
            Query 'Cancel File Operations'
            Check (Set-UiaWindowPosition -Driver $ctx.Driver -Element $ctx.Main -X 30 -Y 30 -Width 620 -Height 700).ok 'resize with live query open'
            $null=Observe "$theme-$density-open-resize-narrow" 1 "$theme-$density-620-single"
            Check ((State).selectedName -eq 'Cancel File Operations' -and
                   (Get-UiaElementTextValue -Driver $ctx.Driver -Element (Controls).edit) -eq 'Cancel File Operations') 'live resize retains query and selection'
            Check (Set-UiaWindowPosition -Driver $ctx.Driver -Element $ctx.Main -X 30 -Y 30 -Width 1008 -Height 700).ok 'restore live query window size'
            Check ((Observe "$theme-$density-open-resize-restored" 1 "$theme-$density-1008-single") -eq $anchor) 'restored live size returns query anchor'
        }
        Input '{ESC}'
        $closed=State
        Check ($closed.discoverySucceeded -and -not $closed.editPresent -and -not $closed.listPresent) 'Escape closes actual native controls'
    }
    $homeCommandResult=& $cap {param($c) Invoke-UiaPaletteCommand -Driver $c.Driver -Main $c.Main -ProcessId $c.Launched.Id -Query Home -ExpectedNames @() -ExpectedCommandName Home -ExpectedCommandId navigation.home} $ctx
    Check $homeCommandResult.Pass 'enabled Home command dispatches through actual keyboard palette'
    $cleanup=Remove-UiaScenarioContext -Context $ctx -ArtifactsDir $artifacts -LogFn {param($m) Write-Host $m};$cleanups+= $cleanup;$ctx=$null
    Check $cleanup.Pass 'normal isolated profile cleanup'
 }}
} catch {$failure=$_.Exception.Message;Write-Host ('FAIL: '+$failure)}
finally {
 if($ctx) {$cleanups+=Remove-UiaScenarioContext -Context $ctx -ArtifactsDir $artifacts -LogFn {param($m) Write-Host $m}}
 @{failure=$failure;observations=$observations;cleanups=$cleanups} | ConvertTo-Json -Depth 25 | Set-Content (Join-Path $artifacts 'result.json') -Encoding utf8NoBOM
 Write-Host "Artifacts: $artifacts"
}
if($failure -or @($cleanups | Where-Object {-not $_.Pass}).Count) {exit 1}
