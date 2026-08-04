<#
    zero-touch-autonomous-engineering (uia-driver): keyboard and pointer input via
    user32 SendInput. Used only when a needed interaction has no exposed UIA pattern
    (see Invoke-UiaElementAction). The driver records that the fallback was used and
    never pixel-verifies results. The C# wrapper is compiled once by Add-Type.
#>

if (-not ('FfNativeInput' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class FfNativeInput
{
    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

    [DllImport("user32.dll")]
    public static extern int GetSystemMetrics(int nIndex);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT
    {
        public uint type;
        public InputUnion u;
    }

    [StructLayout(LayoutKind.Explicit)]
    public struct InputUnion
    {
        [FieldOffset(0)] public KEYBDINPUT ki;
        [FieldOffset(0)] public MOUSEINPUT mi;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT
    {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT
    {
        public int dx;
        public int dy;
        public uint mouseData;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    public const uint INPUT_KEYBOARD = 1;
    public const uint INPUT_MOUSE = 0;
    public const uint KEYEVENTF_KEYUP = 0x0002;
    public const uint KEYEVENTF_EXTENDEDKEY = 0x0001;
    public const uint KEYEVENTF_UNICODE = 0x0004;
    public const uint MOUSEEVENTF_ABSOLUTE = 0x8000;
    public const uint MOUSEEVENTF_MOVE = 0x0001;
    public const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    public const uint MOUSEEVENTF_LEFTUP = 0x0004;
    public const uint MOUSEEVENTF_RIGHTDOWN = 0x0008;
    public const uint MOUSEEVENTF_RIGHTUP = 0x0010;

    public static bool KeyDown(ushort vk)
    {
        INPUT[] input = new INPUT[1];
        input[0].type = INPUT_KEYBOARD;
        input[0].u.ki.wVk = vk;
        input[0].u.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        return SendInput(1, input, Marshal.SizeOf(typeof(INPUT))) == 1;
    }

    public static bool KeyUp(ushort vk)
    {
        INPUT[] input = new INPUT[1];
        input[0].type = INPUT_KEYBOARD;
        input[0].u.ki.wVk = vk;
        input[0].u.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
        return SendInput(1, input, Marshal.SizeOf(typeof(INPUT))) == 1;
    }

    public static int TypeText(string text)
    {
        int sent = 0;
        foreach (char c in text)
        {
            INPUT[] input = new INPUT[2];
            input[0].type = INPUT_KEYBOARD;
            input[0].u.ki.wScan = (ushort)c;
            input[0].u.ki.dwFlags = KEYEVENTF_UNICODE;
            input[1].type = INPUT_KEYBOARD;
            input[1].u.ki.wScan = (ushort)c;
            input[1].u.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            if (SendInput(2, input, Marshal.SizeOf(typeof(INPUT))) == 2) sent++;
        }
        return sent;
    }

    public static bool MouseMove(int x, int y)
    {
        int sw = GetSystemMetrics(0);
        int sh = GetSystemMetrics(1);
        if (sw <= 0 || sh <= 0) return false;
        INPUT[] input = new INPUT[1];
        input[0].type = INPUT_MOUSE;
        input[0].u.mi.dx = (x * 65535) / (sw - 1);
        input[0].u.mi.dy = (y * 65535) / (sh - 1);
        input[0].u.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
        return SendInput(1, input, Marshal.SizeOf(typeof(INPUT))) == 1;
    }

    public static bool MouseButton(bool left, bool down)
    {
        INPUT[] input = new INPUT[1];
        input[0].type = INPUT_MOUSE;
        input[0].u.mi.dwFlags = left
            ? (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP)
            : (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
        return SendInput(1, input, Marshal.SizeOf(typeof(INPUT))) == 1;
    }
}
'@
}

function Get-UiaClickablePoint {
    param($Driver, $Element)
    if (Test-UiaMockElement $Element) {
        return Get-UiaMockClickablePoint -Driver $Driver -Element $Element
    }
    Initialize-UiaManagedApi
    $x = 0; $y = 0
    try {
        $ok = $Element.GetClickablePoint([ref]$x, [ref]$y)
        if (-not $ok) { return $null }
        return [pscustomobject]@{ x = [int]$x; y = [int]$y }
    } catch {
        return $null
    }
}

function Get-UiaVk {
    param([string]$Token)
    $upper = $Token.ToUpperInvariant()
    if ($script:UiaVkMap.ContainsKey($upper)) { return [uint16]$script:UiaVkMap[$upper] }
    if ($upper.Length -eq 1 -and [char]::IsLetterOrDigit($upper[0])) {
        if ([char]::IsDigit($upper[0])) { return [uint16]([int][char]$upper[0]) }
        return [uint16][int][char]$upper[0]
    }
    return 0
}

function Set-UiaForeground {
    param($Driver, $Element)
    if (Test-UiaMockElement $Element) { return $false }
    Initialize-UiaManagedApi
    try {
        $hwnd = [IntPtr]$Element.Current.NativeWindowHandle
        if ($hwnd -eq [IntPtr]::Zero) { return $false }
        # Windows foreground-lock rules let a background process lose the fight for
        # foreground ownership, which makes SendInput-based input land in the wrong
        # window (flaky). Robust sequence: restore minimized windows, set foreground,
        # tap ALT once (releases the foreground lock per user32 docs), retry, and
        # poll-verify GetForegroundWindow == target before returning.
        for ($i = 0; $i -lt 5; $i++) {
            $null = [FfNativeInput]::ShowWindow($hwnd, 9)      # SW_RESTORE (unminimize)
            $null = [FfNativeInput]::SetForegroundWindow($hwnd)
            $null = [FfNativeInput]::KeyDown(0x12)             # ALT down/up releases the lock
            $null = [FfNativeInput]::KeyUp(0x12)
            $null = [FfNativeInput]::SetForegroundWindow($hwnd)
            Start-Sleep -Milliseconds 200
            if ([FfNativeInput]::GetForegroundWindow() -eq $hwnd) { return $true }
        }
        return $false
    } catch {
        return $false
    }
}

function Send-UiaInput {
    param($Driver, [Parameter(Mandatory)][string] $Keys, [switch] $NoSend)
    $seq = [System.Collections.ArrayList]::new()
    $push = {
        param($vk, $down)
        if ($vk -gt 0) { [void]$seq.Add([pscustomobject]@{ vk = $vk; down = $down }) }
    }
    $tokens = [regex]::Matches($Keys, '\{[A-Za-z0-9]+\}|[a-zA-Z0-9\s\.\-]')
    $mods = [ordered]@{ ctrl = $false; alt = $false; shift = $false }
    $pushMods = {
        param($down)
        if ($down) {
            if ($mods.ctrl) { & $push 0x11 $true }
            if ($mods.alt) { & $push 0x12 $true }
            if ($mods.shift) { & $push 0x10 $true }
        } else {
            if ($mods.shift) { & $push 0x10 $false }
            if ($mods.alt) { & $push 0x12 $false }
            if ($mods.ctrl) { & $push 0x11 $false }
        }
    }
    foreach ($m in $tokens) {
        $token = $m.Value
        if ($token.StartsWith('{')) {
            $name = $token.Trim('{', '}')
            if ($name -eq 'CTRL' -or $name -eq 'ALT' -or $name -eq 'SHIFT') {
                $mods[$name.ToLowerInvariant()] = $true
                continue
            }
            $vk = Get-UiaVk -Token $name
            if ($vk -eq 0) { throw "Unknown key token '$token'" }
            & $pushMods $true
            & $push $vk $true
            & $push $vk $false
            & $pushMods $false
            $mods.ctrl = $false; $mods.alt = $false; $mods.shift = $false
            continue
        }
        & $pushMods $true
        foreach ($ch in $token.ToCharArray()) {
            $vk = [uint16][int][char]::ToUpperInvariant($ch)
            & $push $vk $true
            & $push $vk $false
        }
        & $pushMods $false
        $mods.ctrl = $false; $mods.alt = $false; $mods.shift = $false
    }
    if (-not $NoSend) {
        foreach ($item in $seq) {
            if ($item.down) { [void][FfNativeInput]::KeyDown($item.vk) } else { [void][FfNativeInput]::KeyUp($item.vk) }
        }
    }
    return [pscustomobject]@{ used = 'SendInput'; fallback = $true; keys = $Keys; dryRun = $NoSend; sequence = @($seq) }
}

function Send-UiaText {
    param($Driver, [Parameter(Mandatory)][string] $Text, [switch] $NoSend)
    if (-not $NoSend) {
        [void][FfNativeInput]::TypeText($Text)
    }
    return [pscustomobject]@{ used = 'SendInput'; fallback = $true; kind = 'unicode-text'; chars = $Text.Length; dryRun = $NoSend }
}

function Send-UiaMouseInput {
    param($Driver, [int] $X, [int] $Y, [ValidateSet('Left', 'Right')][string] $Button = 'Left', [ValidateSet('Click', 'Down', 'Up')][string] $Action = 'Click', [switch] $NoSend)
    if (-not $NoSend) {
        $null = [FfNativeInput]::MouseMove($X, $Y)
        $isLeft = ($Button -eq 'Left')
        if ($Action -in @('Click', 'Down')) { $null = [FfNativeInput]::MouseButton($isLeft, $true) }
        if ($Action -in @('Click', 'Up')) { $null = [FfNativeInput]::MouseButton($isLeft, $false) }
    }
    return [pscustomobject]@{ used = 'SendInput'; fallback = $true; action = $Action; x = $X; y = $Y; dryRun = $NoSend }
}

