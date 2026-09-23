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
    public static extern IntPtr GetCapture();

    [DllImport("user32.dll")]
    public static extern uint GetDoubleClickTime();

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

    [StructLayout(LayoutKind.Sequential)]
    public struct GUITHREADINFO
    {
        public int cbSize;
        public int flags;
        public IntPtr hwndActive;
        public IntPtr hwndFocus;
        public IntPtr hwndCapture;
        public IntPtr hwndMenuOwner;
        public IntPtr hwndMoveSize;
        public IntPtr hwndCaret;
        public RECT rcCaret;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int left;
        public int top;
        public int right;
        public int bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetGUIThreadInfo(uint idThread, ref GUITHREADINFO lpgui);

    // True when the thread owning hWnd currently holds mouse capture (an
    // active OLE DoDragDrop session keeps the source window captured). The
    // capture state is per-thread, so GetCapture on a foreign thread cannot
    // see it; GetGUIThreadInfo is the cross-thread query.
    public static bool ThreadHasCapture(IntPtr hWnd)
    {
        if (hWnd == IntPtr.Zero) return false;
        uint tid;
        GetWindowThreadProcessId(hWnd, out tid);
        if (tid == 0) return false;
        var info = new GUITHREADINFO { cbSize = System.Runtime.InteropServices.Marshal.SizeOf(typeof(GUITHREADINFO)) };
        if (!GetGUIThreadInfo(tid, ref info)) return false;
        return info.hwndCapture != IntPtr.Zero;
    }

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);

    [DllImport("user32.dll")]
    public static extern bool BringWindowToTop(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

    public const uint SWP_NOSIZE = 0x0001;
    public const uint SWP_NOMOVE = 0x0002;

    // Attach to the current foreground thread, pulse HWND_TOPMOST, then
    // SetForegroundWindow. A background automation host otherwise loses the
    // Win32 foreground lock to an interactive browser/document.
    public static bool ForceForeground(IntPtr hWnd)
    {
        if (hWnd == IntPtr.Zero) return false;
        IntPtr fg = GetForegroundWindow();
        uint fgPid = 0, ourPid = 0;
        uint fgTid = fg != IntPtr.Zero ? GetWindowThreadProcessId(fg, out fgPid) : 0;
        uint ourTid = GetWindowThreadProcessId(hWnd, out ourPid);
        bool attached = false;
        if (fgTid != 0 && ourTid != 0 && fgTid != ourTid)
            attached = AttachThreadInput(ourTid, fgTid, true);
        ShowWindow(hWnd, 9);
        SetWindowPos(hWnd, new IntPtr(-1), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        BringWindowToTop(hWnd);
        bool ok = SetForegroundWindow(hWnd);
        if (attached) AttachThreadInput(ourTid, fgTid, false);
        return ok || GetForegroundWindow() == hWnd;
    }

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
    public const uint MOUSEEVENTF_VIRTUALDESK = 0x4000;

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
        // hardening-campaign Phase 4: normalize against the VIRTUAL screen so
        // coordinates outside the primary monitor map correctly on
        // multi-monitor hosts; on a single display this is algebraically
        // identical to the old primary-only math (origin 0, width == screen).
        int vx = GetSystemMetrics(76);   // SM_XVIRTUALSCREEN
        int vy = GetSystemMetrics(77);   // SM_YVIRTUALSCREEN
        int vw = GetSystemMetrics(78);   // SM_CXVIRTUALSCREEN
        int vh = GetSystemMetrics(79);   // SM_CYVIRTUALSCREEN
        if (vw <= 1 || vh <= 1) return false;
        INPUT[] input = new INPUT[1];
        input[0].type = INPUT_MOUSE;
        input[0].u.mi.dx = ((x - vx) * 65535) / (vw - 1);
        input[0].u.mi.dy = ((y - vy) * 65535) / (vh - 1);
        input[0].u.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
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
        # poll-verify GetForegroundWindow == target before returning. The ALT tap
        # activates the window's menu bar on menu-bearing windows, which swallows
        # the next chord; ESC dismisses it.
        for ($i = 0; $i -lt 5; $i++) {
            $null = [FfNativeInput]::ForceForeground($hwnd)
            $null = [FfNativeInput]::ShowWindow($hwnd, 9)      # SW_RESTORE (unminimize)
            $null = [FfNativeInput]::SetForegroundWindow($hwnd)
            $null = [FfNativeInput]::KeyDown(0x12)             # ALT down/up releases the lock
            $null = [FfNativeInput]::KeyUp(0x12)
            $null = [FfNativeInput]::KeyDown(0x1B)             # ESC dismisses an activated menu
            $null = [FfNativeInput]::KeyUp(0x1B)
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
    param($Driver, [int] $X, [int] $Y, [ValidateSet('Left', 'Right')][string] $Button = 'Left', [ValidateSet('Click', 'Down', 'Up')][string] $Action = 'Click', [ValidateSet('', 'Ctrl', 'Shift', 'Alt')][string] $Modifier = '', [switch] $NoSend)
    if (-not $NoSend) {
        $vk = if ($Modifier -eq 'Ctrl') { 0x11 } elseif ($Modifier -eq 'Shift') { 0x10 } elseif ($Modifier -eq 'Alt') { 0x12 } else { 0 }
        if ($vk -ne 0) { [void][FfNativeInput]::KeyDown($vk) }
        $null = [FfNativeInput]::MouseMove($X, $Y)
        $isLeft = ($Button -eq 'Left')
        if ($Action -in @('Click', 'Down')) { $null = [FfNativeInput]::MouseButton($isLeft, $true) }
        if ($Action -in @('Click', 'Up')) { $null = [FfNativeInput]::MouseButton($isLeft, $false) }
        if ($vk -ne 0) { [void][FfNativeInput]::KeyUp($vk) }
    }
    return [pscustomobject]@{ used = 'SendInput'; fallback = $true; action = $Action; x = $X; y = $Y; modifier = $Modifier; dryRun = $NoSend }
}

function Send-UiaMouseDrag {
    <#
        Pointer drag from one screen point to another (Down -> stepped moves ->
        Up). Used by the drag scenarios with programmatic filesystem
        verification; the source/destination geometry always comes from UIA
        BoundingRectangles, never from pixels guessed by the caller.
        stabilize-live-uia-e2e: the initial pause after the button-down and a
        small arm move are required for OLE drag sources (FastFiles and
        Explorer) to enter DoDragDrop reliably with injected input.
    #>
    param($Driver, [int] $FromX, [int] $FromY, [int] $ToX, [int] $ToY,
          [int] $Steps = 20, [int] $StepDelayMs = 60, [switch] $NoSend)
    if (-not $NoSend) {
        try {
            $null = [FfNativeInput]::MouseMove($FromX, $FromY)
            Start-Sleep -Milliseconds 300
            $null = [FfNativeInput]::MouseButton($true, $true)
            Start-Sleep -Milliseconds 600
            # Small arm move so the source crosses its drag threshold; the pause
            # after the down must outlast the source's selection/repaint work or
            # the arm move is swallowed and the drag never starts.
            $armX = $FromX + [int](($ToX - $FromX) / 8)
            $armY = $FromY + [int](($ToY - $FromY) / 8)
            $null = [FfNativeInput]::MouseMove($armX, $armY)
            Start-Sleep -Milliseconds 600
            for ($i = 1; $i -le $Steps; $i++) {
                $x = $FromX + [int](($ToX - $FromX) * $i / $Steps)
                $y = $FromY + [int](($ToY - $FromY) * $i / $Steps)
                $null = [FfNativeInput]::MouseMove($x, $y)
                Start-Sleep -Milliseconds $StepDelayMs
            }
            Start-Sleep -Milliseconds 800
            $null = [FfNativeInput]::MouseButton($true, $false)
        } catch {
            # hardening-campaign Phase 4: input-state cleanup -- never leave
            # the physical button down if the sequence dies mid-drag; a stuck
            # button contaminates every subsequent input on the host.
            throw
        } finally {
            $null = [FfNativeInput]::MouseButton($true, $false)
        }
    }
    return [pscustomobject]@{ used = 'SendInput'; fallback = $true; kind = 'mouse-drag'; from = "$FromX,$FromY"; to = "$ToX,$ToY"; dryRun = $NoSend }
}
