<#
Explicit interactive diagnostic; NOT registered with portable CTest.
Run in a fresh disposable client: pwsh -NoProfile -File <this> -OutputPath <json>
Creates only owned raw Win32 controls, observes real UIA and own capture, then
uses normal WM_CLOSE/join. No FastFiles, input injection, or termination.
#>
param([Parameter(Mandatory)][string]$OutputPath, [switch]$ColdStart)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::UserInteractive -or (Get-Process -Id $PID).SessionId -eq 0) {
    throw 'Explicit native diagnostic requires an interactive Windows desktop.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Import-Module (Join-Path $repo 'verify/uia-driver/UiaDriver.psm1') -Force
$module = Get-Module UiaDriver
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type -Path (Join-Path $PSScriptRoot 'NativeStandardFixture.cs')
$clientDirectory = [IO.Path]::GetDirectoryName([System.Windows.Automation.AutomationElement].Assembly.Location)
Add-Type -ReferencedAssemblies (Join-Path $clientDirectory 'UIAutomationClient.dll'),(Join-Path $clientDirectory 'UIAutomationTypes.dll'),(Join-Path $clientDirectory 'WindowsBase.dll') -Path (Join-Path $PSScriptRoot 'NativeNameObserver.cs')
function Assert([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw "Native diagnostic failed: $Description" }
}
function Read-Control([string]$Label, [IntPtr]$Hwnd, [string]$ClassName) {
    $ownerPid = [uint32]0
    $threadId = [FfStandardFixture]::GetWindowThreadProcessId($Hwnd, [ref]$ownerPid)
    Assert ($ownerPid -eq $PID -and $threadId -ne 0 -and [FfStandardFixture]::IsWindow($Hwnd)) 'positive fixture HWND/PID binding'
    Assert ([FfStandardFixture]::ClassOf($Hwnd) -ceq $ClassName) 'positive fixture class binding'
    $element = [System.Windows.Automation.AutomationElement]::FromHandle($Hwnd)
    [pscustomobject]@{
        Label = $Label; Hwnd = $Hwnd.ToInt64(); Pid = $ownerPid; ThreadId = $threadId; Class = $ClassName
        ControlType = $element.Current.ControlType.ProgrammaticName; FrameworkId = $element.Current.FrameworkId
        IsKeyboardFocusable = $element.Current.IsKeyboardFocusable
        Patterns = @($element.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName })
    }
}
function Assert-StandardControls($records) {
    $check = @($records | Where-Object Class -CEQ Button)[0]
    $combo = @($records | Where-Object Class -CEQ ComboBox)[0]
    Assert ($check.ControlType -ceq 'ControlType.CheckBox' -and $check.Patterns -contains 'TogglePatternIdentifiers.Pattern') 'genuine checkbox Toggle pattern'
    Assert ($combo.ControlType -ceq 'ControlType.ComboBox' -and $combo.Patterns -contains 'SelectionPatternIdentifiers.Pattern' -and $combo.Patterns -contains 'ExpandCollapsePatternIdentifiers.Pattern') 'genuine combo Selection and ExpandCollapse patterns'
    Assert ($check.IsKeyboardFocusable -and $combo.IsKeyboardFocusable) 'genuine standard focusable property'
}
$result = [ordered]@{ StartedUtc = [DateTime]::UtcNow.ToString('o'); ClientPid = $PID; PowerShell = $PSVersionTable.PSVersion.ToString(); ColdStart = [bool]$ColdStart; Status = 'FAIL' }
$result.SourceHashes = @('verify/uia-driver/internal/Common.ps1', 'verify/uia-driver/internal/Input.ps1', 'verify/uia-driver/tests/NativeStandardFixture.cs', 'verify/uia-driver/tests/NativeNameObserver.cs', 'verify/uia-driver/tests/Test-NativeStandardControls.ps1') | ForEach-Object {
    @{ Path = $_; Sha256 = (Get-FileHash -LiteralPath (Join-Path $repo $_) -Algorithm SHA256).Hash }
}
try {
    if ($ColdStart) {
        # No HWND/AutomationElement read before the real driver's initializer.
        $coldOutput = @(& $module { Initialize-UiaManagedApi })
        Assert ($coldOutput.Count -eq 0) 'cold initialization emits no output'
    }
    [FfStandardFixture]::Start()
    $result.Before = @((Read-Control checkbox ([FfStandardFixture]::Check) Button); (Read-Control combo ([FfStandardFixture]::Combo) ComboBox))
    $result.NameBefore = [FfNativeNameObserver]::Observe([FfStandardFixture]::Window, 'Owned fixture title before bootstrap')
    $output = @(& $module { Initialize-UiaManagedApi })
    Assert ($output.Count -eq 0) 'initialization emits no output'
    $result.AfterSame = @((Read-Control checkbox ([FfStandardFixture]::Check) Button); (Read-Control combo ([FfStandardFixture]::Combo) ComboBox))
    Assert-StandardControls $result.AfterSame
    [void][FfStandardFixture]::SendMessageW([FfStandardFixture]::Window, 0x8001, [IntPtr]::Zero, [IntPtr]::Zero)
    $result.AfterFresh = @((Read-Control freshCheckbox ([FfStandardFixture]::FreshCheck) Button); (Read-Control freshCombo ([FfStandardFixture]::FreshCombo) ComboBox))
    Assert-StandardControls $result.AfterFresh
    $result.NameAfter = [FfNativeNameObserver]::Observe([FfStandardFixture]::Window, 'Owned fixture title after bootstrap')
    Assert ($result.NameAfter -match '^count=[1-9][0-9]*;latest=30005:Owned fixture title after bootstrap;current=Owned fixture title after bootstrap$') 'genuine title Name event observed'
    $key = 'FastFiles.UiaStandardProxy.' + [System.Windows.Automation.AutomationElement].Assembly.ManifestModule.ModuleVersionId.ToString('D')
    $marker = [AppDomain]::CurrentDomain.GetData($key)
    $result.Bootstrap = $marker.Clone()
    Import-Module (Join-Path $repo 'verify/uia-driver/UiaDriver.psm1') -Force
    $module = Get-Module UiaDriver
    $reloadOutput = @(& $module { Initialize-UiaManagedApi })
    $result.ReloadReusedMarker = [object]::ReferenceEquals($marker, [AppDomain]::CurrentDomain.GetData($key))
    Assert ($reloadOutput.Count -eq 0 -and $result.ReloadReusedMarker) 'reload reuses marker without registration/output'
    Assert (-not [FfNativeInput]::ThreadHasCapture([IntPtr]::Zero)) 'zero handle fails closed'
    Assert (-not [FfNativeInput]::ThreadHasCapture([FfStandardFixture]::Window)) 'initial source thread has no capture'
    [void][FfStandardFixture]::SendMessageW([FfStandardFixture]::Window, 0x8002, [IntPtr]::Zero, [IntPtr]::Zero)
    $ownerPid = [uint32]0
    $ownerTid = [FfStandardFixture]::GetWindowThreadProcessId([FfStandardFixture]::Window, [ref]$ownerPid)
    $info = [FfNativeInput+GUITHREADINFO]::new()
    $info.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($info)
    $lookup = [FfNativeInput]::GetGUIThreadInfo($ownerTid, [ref]$info)
    Assert ($ownerPid -eq $PID -and $lookup -and $info.hwndCapture -eq [FfStandardFixture]::Check) 'own STA captured exact owned checkbox'
    $oldInfo = [FfNativeInput+GUITHREADINFO]::new(); $oldInfo.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($oldInfo)
    $oldLookup = [FfNativeInput]::GetGUIThreadInfo($ownerPid, [ref]$oldInfo)
    $result.Capture = @{ OwnerPid = $ownerPid; OwnerTid = $ownerTid; ActualCaptureHwnd = $info.hwndCapture.ToInt64(); LegacyPidLookupSucceeded = $oldLookup; LegacyObservedCapture = ($oldLookup -and $oldInfo.hwndCapture -ne [IntPtr]::Zero); ParentObserved = [FfNativeInput]::ThreadHasCapture([FfStandardFixture]::Window); SiblingObserved = [FfNativeInput]::ThreadHasCapture([FfStandardFixture]::Combo) }
    Assert ($result.Capture.ParentObserved -and $result.Capture.SiblingObserved) 'any capture on source thread is observed from parent and sibling'
    [void][FfStandardFixture]::SendMessageW([FfStandardFixture]::Window, 0x8003, [IntPtr]::Zero, [IntPtr]::Zero)
    $result.Capture.ReleasedObserved = [FfNativeInput]::ThreadHasCapture([FfStandardFixture]::Window)
    Assert (-not $result.Capture.ReleasedObserved) 'normal release clears observed capture'
    $result.Status = 'PASS'
} catch { $result.Error = $_.Exception.ToString() } finally {
    $result.ClosedNormally = [FfStandardFixture]::Close()
    $result.WindowStillExists = [FfStandardFixture]::IsWindow([FfStandardFixture]::Window)
    $result.DestroyedHandleCapture = [FfNativeInput]::ThreadHasCapture([FfStandardFixture]::Window)
    if (-not $result.ClosedNormally -or $result.WindowStillExists -or $result.DestroyedHandleCapture) { $result.Status = 'FAIL' }
    $result.EndedUtc = [DateTime]::UtcNow.ToString('o')
    $parent = Split-Path -Parent ([IO.Path]::GetFullPath($OutputPath))
    [void][IO.Directory]::CreateDirectory($parent)
    $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $OutputPath -Encoding utf8
}
$result | ConvertTo-Json -Depth 12
if ($result.Status -ne 'PASS') { exit 1 }
