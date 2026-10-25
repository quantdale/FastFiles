param([string] $InputPath = (Join-Path $PSScriptRoot '..\internal\Input.ps1'))

# Exercise the actual PowerShell orchestration with an in-memory native API
# adapter. No foreground window, key, pointer, process, or clipboard is touched.
& {
    if (-not ('FfForegroundInputStub' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
public static class FfForegroundInputStub {
    public static IntPtr Foreground;
    public static bool NormalAcquires, AltAcquires, AltTapped, MenuActive;
    public static int ForceCalls;
    public static List<string> Keys = new List<string>();
    public static void Reset() {
        Foreground = new IntPtr(99);
        NormalAcquires = AltAcquires = AltTapped = MenuActive = false;
        ForceCalls = 0;
        Keys.Clear();
    }
    public static IntPtr GetForegroundWindow() { return Foreground; }
    public static bool ForceForeground(IntPtr hwnd) {
        ++ForceCalls;
        return SetForegroundWindow(hwnd);
    }
    public static bool ShowWindow(IntPtr hwnd, int mode) { return true; }
    public static bool SetForegroundWindow(IntPtr hwnd) {
        if (NormalAcquires || (AltAcquires && AltTapped)) Foreground = hwnd;
        return Foreground == hwnd;
    }
    public static bool KeyDown(ushort key) { Keys.Add("down:" + key); return true; }
    public static bool KeyUp(ushort key) {
        Keys.Add("up:" + key);
        if (key == 18) AltTapped = true;
        if (key == 27) MenuActive = false;
        return true;
    }
    public static bool ThreadHasActiveMenu(IntPtr hwnd) { return MenuActive; }
}
'@
    }
    $tokens = $null
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile(
        [IO.Path]::GetFullPath($InputPath), [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count -gt 0) { throw 'Foreground helper source did not parse' }
    $functionAst = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Set-UiaForeground'
    }, $true)
    if (-not $functionAst) { throw 'Set-UiaForeground was not found in the actual input module' }
    # Substitute only the native dependency, retaining the production body.
    . ([scriptblock]::Create($functionAst.Extent.Text.Replace('[FfNativeInput]', '[FfForegroundInputStub]')))
    function Test-UiaMockElement { return $false }
    function Initialize-UiaManagedApi { }
    function Start-Sleep { param($Milliseconds) }
    $element = [pscustomobject]@{ Current = [pscustomobject]@{ NativeWindowHandle = 42 } }

    [FfForegroundInputStub]::Reset()
    [FfForegroundInputStub]::Foreground = [IntPtr]42
    Check (Set-UiaForeground -Element $element) 'an already foreground window succeeds'
    Check ([FfForegroundInputStub]::Keys.Count -eq 0 -and [FfForegroundInputStub]::ForceCalls -eq 0) 'an already foreground Search keeps its focus and receives no keys'

    [FfForegroundInputStub]::Reset()
    [FfForegroundInputStub]::NormalAcquires = $true
    Check (Set-UiaForeground -Element $element) 'ordinary foreground activation succeeds'
    Check ([FfForegroundInputStub]::Keys.Count -eq 0) 'ordinary activation injects neither Alt nor Escape'

    [FfForegroundInputStub]::Reset()
    [FfForegroundInputStub]::AltAcquires = $true
    Check (Set-UiaForeground -Element $element) 'Alt fallback can acquire a locked foreground'
    Check (([FfForegroundInputStub]::Keys -join ',') -eq 'down:18,up:18') 'Alt fallback without a menu never injects Escape'

    [FfForegroundInputStub]::Reset()
    [FfForegroundInputStub]::AltAcquires = $true
    [FfForegroundInputStub]::MenuActive = $true
    Check (Set-UiaForeground -Element $element) 'Alt fallback handles an observed target-owned menu'
    Check (([FfForegroundInputStub]::Keys -join ',') -eq 'down:18,up:18,down:27,up:27') 'Escape dismisses only the observed target-owned menu'

    [FfForegroundInputStub]::Reset()
    [FfForegroundInputStub]::MenuActive = $true
    Check (-not (Set-UiaForeground -Element $element)) 'an unreachable foreground reports failure'
    Check ([FfForegroundInputStub]::ForceCalls -eq 5) 'foreground acquisition attempts remain bounded'
    Check (-not [FfForegroundInputStub]::Keys.Contains('down:27')) 'failed acquisition never dismisses a foreign window menu'

    [FfForegroundInputStub]::Reset()
    $missingWindow = [pscustomobject]@{ Current = [pscustomobject]@{ NativeWindowHandle = 0 } }
    Check (-not (Set-UiaForeground -Element $missingWindow)) 'a missing native window is rejected'
    Check ([FfForegroundInputStub]::Keys.Count -eq 0) 'a missing native window receives no keys'
}
