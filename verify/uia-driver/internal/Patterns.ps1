<#
    zero-touch-autonomous-engineering (uia-driver): control pattern resolution and
    invocation. Live targets use the managed pattern classes (Invoke, SelectionItem,
    Selection, Scroll, Value, Window). Drag is not exposed by this host's
    UIAutomationClient, so it is reported unsupported and callers use the SendInput
    mouse fallback (recorded, never pixel-verified).     Mock elements resolve pattern
    availability from the recorded tree and record invocations.
#>

if (-not ('FfButtonDefault' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class FfButtonDefault
{
    public const uint BM_CLICK = 0x00F5;

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    public static bool IsButton(IntPtr hWnd)
    {
        if (hWnd == IntPtr.Zero) return false;
        StringBuilder sb = new StringBuilder(128);
        GetClassName(hWnd, sb, 128);
        string name = sb.ToString();
        return string.Equals(name, "Button", StringComparison.Ordinal)
            || name.StartsWith("Button", StringComparison.Ordinal);
    }

    public static bool Click(IntPtr hWnd)
    {
        return SendMessage(hWnd, BM_CLICK, IntPtr.Zero, IntPtr.Zero) != IntPtr.Zero;
    }
}
'@
}

function Get-UiaPattern {
    param($Driver, $Element, [Parameter(Mandatory)][string] $PatternName)
    if ($PatternName -notin $script:UiaPatternNames) {
        throw [UiaPatternNotSupportedException]::new("Unknown UIA pattern '$PatternName'", $PatternName)
    }
    if (Test-UiaMockElement $Element) {
        if (-not (Test-UiaMockPattern -Driver $Driver -Element $Element -PatternName $PatternName)) {
            throw [UiaPatternNotSupportedException]::new("Element does not expose $PatternName pattern", $PatternName)
        }
        return [pscustomobject]@{ _isMockPattern = $true; _driver = $Driver; _element = $Element; _patternName = $PatternName }
    }
    Initialize-UiaManagedApi
    try {
        switch ($PatternName) {
            'Invoke' { return $Element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern) }
            'SelectionItem' { return $Element.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern) }
            'Selection' { return $Element.GetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern) }
            'Scroll' { return $Element.GetCurrentPattern([System.Windows.Automation.ScrollPattern]::Pattern) }
            'Value' { return $Element.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern) }
            'Window' { return $Element.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern) }
            'Drag' {
                throw [UiaPatternNotSupportedException]::new(
                    "DragPattern is not exposed by this host's UIAutomationClient; use Send-UiaMouseInput with programmatic filesystem verification", 'Drag')
            }
        }
    } catch {
        if ($_.Exception -is [UiaPatternNotSupportedException]) { throw }
        throw [UiaPatternNotSupportedException]::new("Element does not expose $PatternName pattern: $($_.Exception.Message)", $PatternName)
    }
}

function Test-UiaPatternAvailable {
    param($Driver, $Element, [Parameter(Mandatory)][string] $PatternName)
    try {
        $null = Get-UiaPattern -Driver $Driver -Element $Element -PatternName $PatternName
        return $true
    } catch {
        return $false
    }
}

function Invoke-UiaPattern {
    param($Driver, $Element, [Parameter(Mandatory)][string] $PatternName, [string] $Method, [object[]] $Arguments)
    if (-not $Method) {
        switch ($PatternName) {
            'Invoke' { $Method = 'Invoke' }
            'SelectionItem' { $Method = 'Select' }
            'Scroll' { $Method = 'Scroll' }
            'Value' { $Method = 'SetValue' }
            'Window' { $Method = 'Close' }
            default { $Method = 'Invoke' }
        }
    }
    if (-not $Arguments) { $Arguments = @() }

    $pattern = Get-UiaPattern -Driver $Driver -Element $Element -PatternName $PatternName
    if ($pattern._isMockPattern) {
        $record = Get-UiaMockRecord -Driver $Driver -Element $Element
        if (-not $record.PSObject.Properties['patternCalls']) { $record | Add-Member -NotePropertyName patternCalls -NotePropertyValue ([System.Collections.ArrayList]::new()) }
        [void]$record.patternCalls.Add([pscustomobject]@{ pattern = $PatternName; method = $Method; args = @($Arguments) })
        foreach ($ev in @($Driver.Events)) {
            if (($PatternName -eq 'Invoke' -and $ev.eventName -eq 'Invoked') -or
                ($PatternName -eq 'SelectionItem' -and $Method -eq 'Select' -and $ev.eventName -eq 'SelectionChanged')) {
                $Driver.EventState[$ev.id] = $true
            }
        }
        return $true
    }

    switch ($PatternName) {
        'Invoke' {
            if ($Method -ne 'Invoke') { throw "Invoke pattern has no method '$Method'" }
            $pattern.Invoke(); return $true
        }
        'SelectionItem' {
            switch ($Method) {
                'Select' { $pattern.Select(); return $true }
                'AddToSelection' { $pattern.AddToSelection(); return $true }
                'RemoveFromSelection' { $pattern.RemoveFromSelection(); return $true }
                'GetSelection' { return $pattern.Current.IsSelected }
                default { throw "SelectionItem pattern has no method '$Method'" }
            }
        }
        'Selection' {
            switch ($Method) {
                'GetSelection' { return $pattern.GetSelection() }
                default { throw "Selection pattern has no method '$Method'" }
            }
        }
        'Scroll' {
            switch ($Method) {
                'Scroll' {
                    if ($Arguments.Count -lt 2) { throw 'Scroll requires two arguments (horizontal, vertical); use ScrollPattern.NoScroll (-1)' }
                    $pattern.Scroll([double]$Arguments[0], [double]$Arguments[1]); return $true
                }
                'SetScrollPercent' {
                    if ($Arguments.Count -lt 2) { throw 'SetScrollPercent requires two arguments (horizontalPercent, verticalPercent)' }
                    $pattern.SetScrollPercent([double]$Arguments[0], [double]$Arguments[1]); return $true
                }
                'ScrollHorizontal' { $pattern.ScrollHorizontal([double]$Arguments[0]); return $true }
                'ScrollVertical' { $pattern.ScrollVertical([double]$Arguments[0]); return $true }
                default { throw "Scroll pattern has no method '$Method'" }
            }
        }
        'Value' {
            switch ($Method) {
                'SetValue' {
                    if ($Arguments.Count -lt 1) { throw 'SetValue requires one argument (text)' }
                    $pattern.SetValue([string]$Arguments[0]); return $true
                }
                'GetValue' { return $pattern.Current.Value }
                default { throw "Value pattern has no method '$Method'" }
            }
        }
        'Window' {
            switch ($Method) {
                'Close' { $pattern.Close(); return $true }
                'WaitForInputIdle' {
                    if ($Arguments.Count -lt 1) { throw 'WaitForInputIdle requires one argument (milliseconds)' }
                    return $pattern.WaitForInputIdle([int]$Arguments[0])
                }
                default { throw "Window pattern has no method '$Method'" }
            }
        }
        default {
            throw "Pattern '$PatternName' cannot be invoked"
        }
    }
}

function Invoke-UiaElementAction {
    param($Driver, $Element, [ValidateSet('Click', 'Select')][string] $Action = 'Click', [switch] $NoSend)
    if (Test-UiaPatternAvailable -Driver $Driver -Element $Element -PatternName 'Invoke') {
        $null = Invoke-UiaPattern -Driver $Driver -Element $Element -PatternName 'Invoke'
        return [pscustomobject]@{ used = 'InvokePattern'; fallback = $false }
    }
    if ($Action -eq 'Select' -and (Test-UiaPatternAvailable -Driver $Driver -Element $Element -PatternName 'SelectionItem')) {
        $null = Invoke-UiaPattern -Driver $Driver -Element $Element -PatternName 'SelectionItem' -Method 'Select'
        return [pscustomobject]@{ used = 'SelectionItemPattern'; fallback = $false }
    }
    if (-not (Test-UiaMockElement $Element)) {
        try {
            $hwnd = [IntPtr]$Element.Current.NativeWindowHandle
            if ($hwnd -ne [IntPtr]::Zero -and [FfButtonDefault]::IsButton($hwnd)) {
                $null = [FfButtonDefault]::Click($hwnd)
                return [pscustomobject]@{ used = 'ButtonDefaultAction(BM_CLICK)'; fallback = $true }
            }
        } catch {
            # Not a real button HWND; fall through to clickable-point SendInput.
        }
    }
    $point = Get-UiaClickablePoint -Driver $Driver -Element $Element
    if (-not $point) {
        throw "Element exposes no actionable pattern and no clickable point: $(Format-UiaElementIdentity -Driver $Driver -Element $Element)"
    }
    $null = Send-UiaMouseInput -Driver $Driver -X $point.x -Y $point.y -Button 'Left' -Action 'Click' -NoSend:$NoSend
    return [pscustomobject]@{ used = 'SendInput'; fallback = $true }
}
