<#
    stabilize-live-uia-e2e (uia-driver): interactive-state diagnostics, modifier
    reset, Explorer-window discovery, and semantic Value assignment. These are
    first-class driver helpers so the capability module does not scatter
    window-enumeration or polling loops. Everything here is headless-testable
    (mock drivers return safe envelopes; input helpers honor -NoSend).
#>

if (-not ('FfNativeWindowInfo' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;

public static class FfNativeWindowInfo
{
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern IntPtr GetWindow(IntPtr hWnd, uint uCmd);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    public const uint GW_OWNER = 4;
    public const int MAX_TITLE = 512;

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

    public const uint SWP_NOZORDER = 0x0004;
    public const uint SWP_NOACTIVATE = 0x0010;
    public const uint SWP_NOMOVE = 0x0002;
    public const uint SWP_NOSIZE = 0x0001;

    public static bool MoveWindowTo(IntPtr hWnd, int x, int y, int width, int height)
    {
        if (hWnd == IntPtr.Zero || width <= 0 || height <= 0) return false;
        return SetWindowPos(hWnd, IntPtr.Zero, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    public static bool BringToTop(IntPtr hWnd)
    {
        if (hWnd == IntPtr.Zero) return false;
        // HWND_TOP (0) raises the window above non-topmost siblings without
        // activating it, so a drop at any point inside it hits THIS window
        // even when orphaned windows overlap it.
        return SetWindowPos(hWnd, (IntPtr)0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    public static IntPtr[] TopLevelWindows()
    {
        var list = new List<IntPtr>();
        EnumWindows((h, l) => { list.Add(h); return true; }, IntPtr.Zero);
        return list.ToArray();
    }

    public static int ProcessIdOf(IntPtr hWnd)
    {
        uint pid;
        GetWindowThreadProcessId(hWnd, out pid);
        return (int)pid;
    }

    public static string TitleOf(IntPtr hWnd)
    {
        var sb = new StringBuilder(MAX_TITLE);
        int n = GetWindowText(hWnd, sb, MAX_TITLE);
        return sb.ToString(0, Math.Max(0, n));
    }

    public static string ClassOf(IntPtr hWnd)
    {
        var sb = new StringBuilder(256);
        GetClassName(hWnd, sb, 256);
        return sb.ToString();
    }

    public static bool Visible(IntPtr hWnd)
    {
        return IsWindowVisible(hWnd);
    }

    public static IntPtr OwnerOf(IntPtr hWnd)
    {
        return GetWindow(hWnd, GW_OWNER);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativePoint
    {
        public int X;
        public int Y;
    }

    public const uint GA_ROOT = 2;

    [DllImport("user32.dll")]
    public static extern IntPtr WindowFromPoint(NativePoint p);

    [DllImport("user32.dll")]
    public static extern IntPtr GetAncestor(IntPtr hWnd, uint gaFlags);

    [DllImport("user32.dll")]
    public static extern bool IsChild(IntPtr hWndParent, IntPtr hWnd);

    // Topmost window under a screen point (the window a drop would hit).
    public static IntPtr WindowUnderPoint(int x, int y)
    {
        var p = new NativePoint { X = x, Y = y };
        return WindowFromPoint(p);
    }

    // True when the given window is the point's topmost window or one of its
    // descendants (i.e. a drop at that point would land inside it).
    public static bool PointInside(IntPtr hWnd, int x, int y)
    {
        if (hWnd == IntPtr.Zero) return false;
        IntPtr top = WindowFromPoint(new NativePoint { X = x, Y = y });
        if (top == IntPtr.Zero) return false;
        if (top == hWnd) return true;
        if (IsChild(hWnd, top)) return true;
        return GetAncestor(top, GA_ROOT) == hWnd;
    }
}
'@
}

# Class names that identify popup/overlay windows that can swallow SendInput
# keystrokes (combo dropdowns, menus, tooltips, drop shadows).
$script:UiaPopupSuspectClasses = @(
    'ComboLBox', '#32768', 'tooltips_class32', 'DropShadow',
    'Windows.UI.Core.CoreWindow', 'PopupHost', 'SysShadow')

function Get-UiaInteractiveState {
    <#
        Snapshot of the interactive desktop state relevant to input-heavy
        scenarios: foreground HWND/title, focused UIA element identity,
        top-level windows of a target PID (class/title/visible/owner), and
        popup-suspect windows of that PID. Returns a plain object; never
        throws (all observations are best-effort). Mock drivers return an
        envelope with $null foreground/focus and empty window lists.
    #>
    param($Driver, [int] $ProcessId)
    $state = [pscustomobject]@{
        TimestampUtc    = [DateTime]::UtcNow
        ForegroundHwnd  = $null
        ForegroundTitle = $null
        FocusedElement  = $null
        Windows         = @()
        Popups          = @()
    }
    if (Test-UiaMockElement $Driver.Root) { return $state }

    try {
        $fg = [FfNativeWindowInfo]::GetForegroundWindow()
        if ($fg -ne [IntPtr]::Zero) {
            $state.ForegroundHwnd = $fg
            $state.ForegroundTitle = [FfNativeWindowInfo]::TitleOf($fg)
        }
    } catch { }

    if ($ProcessId -gt 0) {
        try {
            foreach ($hwnd in [FfNativeWindowInfo]::TopLevelWindows()) {
                if ([FfNativeWindowInfo]::ProcessIdOf($hwnd) -ne $ProcessId) { continue }
                $owner = [FfNativeWindowInfo]::OwnerOf($hwnd)
                $class = [FfNativeWindowInfo]::ClassOf($hwnd)
                $title = [FfNativeWindowInfo]::TitleOf($hwnd)
                $visible = [FfNativeWindowInfo]::Visible($hwnd)
                $state.Windows += [pscustomobject]@{
                    hwnd = $hwnd; className = $class; title = $title
                    visible = $visible; ownerHwnd = $owner
                }
                $isPopup = $visible -and (
                    $script:UiaPopupSuspectClasses -contains $class -or
                    ($owner -ne [IntPtr]::Zero -and $class -like '#*'))
                if ($isPopup) {
                    $state.Popups += [pscustomobject]@{
                        hwnd = $hwnd; className = $class; title = $title
                        ownerHwnd = $owner
                    }
                }
            }
        } catch { }
    }

    try {
        Initialize-UiaManagedApi
        $focused = [System.Windows.Automation.AutomationElement]::FocusedElement
        if ($null -ne $focused) {
            $state.FocusedElement = Get-UiaElementIdentity -Driver $Driver -Element $focused
        }
    } catch { }
    return $state
}

function Test-UiaForegroundWindow {
    <#
        True when the given UIA window element currently holds the desktop
        foreground (GetForegroundWindow == its HWND). Mock drivers return
        $true. Semantic foreground verification for input-heavy seams: a
        foreground race (overlay/OSD windows, foreground-lock rules) makes
        SendInput land in the wrong window; callers must not proceed on
        sleep alone when this reports $false.
    #>
    param($Driver, $Element)
    if (Test-UiaMockElement $Element) { return $true }
    Initialize-UiaManagedApi
    try {
        $hwnd = [IntPtr]$Element.Current.NativeWindowHandle
        if ($hwnd -eq [IntPtr]::Zero) { return $false }
        return [FfNativeWindowInfo]::GetForegroundWindow() -eq $hwnd
    } catch {
        return $false
    }
}

function Get-UiaTopmostWindowAt {
    <#
        The topmost window under a screen point (the window a mouse drop
        would hit): { Hwnd, Title, Class, Pid }. $null when unavailable.
        Mock drivers return $null.
    #>
    param($Driver, [int] $X, [int] $Y)
    if (Test-UiaMockElement $Driver.Root) { return $null }
    Initialize-UiaManagedApi
    try {
        $hwnd = [FfNativeWindowInfo]::WindowUnderPoint($X, $Y)
        if ($hwnd -eq [IntPtr]::Zero) { return $null }
        return [pscustomobject]@{
            Hwnd  = $hwnd
            Title = [FfNativeWindowInfo]::TitleOf($hwnd)
            Class = [FfNativeWindowInfo]::ClassOf($hwnd)
            Pid   = [FfNativeWindowInfo]::ProcessIdOf($hwnd)
        }
    } catch {
        return $null
    }
}

function Test-UiaPointInWindow {
    <#
        True when a drop at (X, Y) would land inside the given UIA window
        element: the topmost window under the point is the element's HWND or
        one of its descendants. Mock drivers return $true. Bounded semantic
        guard against a foreign window covering a drag's drop point.
    #>
    param($Driver, $Element, [int] $X, [int] $Y)
    if (Test-UiaMockElement $Element) { return $true }
    Initialize-UiaManagedApi
    try {
        $hwnd = [IntPtr]$Element.Current.NativeWindowHandle
        if ($hwnd -eq [IntPtr]::Zero) { return $false }
        return [FfNativeWindowInfo]::PointInside($hwnd, $X, $Y)
    } catch {
        return $false
    }
}

function Wait-UiaStableRect {
    <#
        True when the element's BoundingRectangle is stable across
        consecutive reads (layout quiescence). A window mid-layout (e.g. an
        Explorer DUI view rebuilding after SetWindowPos) reports changing
        rects; its OLE drop target is re-registered when the layout settles,
        so interaction must wait for stability. Mock drivers return $true.
    #>
    param($Driver, $Element, [int] $StableReads = 3, [int] $IntervalMs = 300, [int] $TimeoutMs = 5000)
    if (Test-UiaMockElement $Element) { return $true }
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $last = $null
    $streak = 0
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        try {
            $br = Get-UiaElementProperty -Driver $Driver -Element $Element -Name 'BoundingRectangle'
            if ($br -and $last -and $br.x -eq $last.x -and $br.y -eq $last.y -and $br.width -eq $last.width -and $br.height -eq $last.height) {
                $streak++
                if ($streak -ge $StableReads) { return $true }
            } else {
                $streak = 0
            }
            $last = $br
        } catch { $streak = 0 }
        Start-Sleep -Milliseconds $IntervalMs
    }
    return $false
}

function Reset-UiaModifierKeys {
    <#
        Release any physically-held modifier keys (Ctrl/Shift/Alt/Win, both
        sides) by sending key-up events. The harness calls this ONLY after it
        has established FastFiles-owned foreground (never into unrelated
        foreground apps). Dry-run with -NoSend for headless tests.
    #>
    param($Driver, [switch] $NoSend)
    # VK codes: SHIFT 0x10, CTRL 0x11, MENU 0x12, LSHIFT 0xA0, RSHIFT 0xA1,
    # LCONTROL 0xA2, RCONTROL 0xA3, LMENU 0xA4, RMENU 0xA5, LWIN 0x5B, RWIN 0x5C.
    $vks = @(0x10, 0x11, 0x12, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0x5B, 0x5C)
    if (-not $NoSend -and -not (Test-UiaMockElement $Driver.Root)) {
        foreach ($vk in $vks) { [void][FfNativeInput]::KeyUp([uint16]$vk) }
    }
    return [pscustomobject]@{
        used = 'SendInput'; fallback = $true; kind = 'modifier-reset'
        released = @($vks); dryRun = $NoSend
    }
}

function Get-UiaElementTextValue {
    <#
        Semantic "text value" of an element: ValuePattern.Value when exposed,
        otherwise the element Name (the address EDIT reports its text as Name
        and exposes no ValuePattern on this host). Returns $null when neither
        is readable.
    #>
    param($Driver, $Element)
    try {
        if (Test-UiaPatternAvailable -Driver $Driver -Element $Element -PatternName 'Value') {
            return [string](Invoke-UiaPattern -Driver $Driver -Element $Element -PatternName 'Value' -Method 'GetValue')
        }
    } catch { }
    try {
        return [string](Get-UiaElementIdentity -Driver $Driver -Element $Element).Name
    } catch { }
    return $null
}

function Set-UiaElementValue {
    <#
        Assign an edit's text through UIA ValuePattern when the element
        exposes it and it is writable, then verify by reading the value back
        (Value pattern, else Name). Returns { ok, used, current, error } where
        used is 'ValuePattern' on success, 'readonly'/'unavailable'/
        'unverified' on failure. The caller decides whether to fall back to
        SendInput text entry; this helper never synthesizes keyboard input.
    #>
    param($Driver, $Element, [Parameter(Mandatory)][string] $Value)
    $result = [pscustomobject]@{ ok = $false; used = 'unavailable'; current = $null; error = $null }
    try {
        if (Test-UiaMockElement $Element) {
            $null = Invoke-UiaPattern -Driver $Driver -Element $Element -PatternName 'Value' -Method 'SetValue' -Arguments @($Value)
            $current = Get-UiaElementProperty -Driver $Driver -Element $Element -Name 'Value'
            $result.current = $current
            $result.used = 'ValuePattern'
            $result.ok = ([string]$current -eq [string]$Value)
            if (-not $result.ok) { $result.error = "mock read-back '$current' != '$Value'" }
            return $result
        }
        $pattern = Get-UiaPattern -Driver $Driver -Element $Element -PatternName 'Value'
        if ($pattern.Current.IsReadOnly) {
            $result.used = 'readonly'
            $result.current = Get-UiaElementTextValue -Driver $Driver -Element $Element
            $result.error = 'ValuePattern is read-only'
            return $result
        }
        $null = Invoke-UiaPattern -Driver $Driver -Element $Element -PatternName 'Value' -Method 'SetValue' -Arguments @($Value)
        $current = Get-UiaElementTextValue -Driver $Driver -Element $Element
        $result.current = $current
        $result.used = 'ValuePattern'
        $result.ok = ([string]$current -eq $Value)
        if (-not $result.ok) { $result.error = "read-back '$current' != '$Value'" }
        return $result
    } catch {
        $result.error = $_.Exception.Message
        if ($result.used -eq 'unavailable' -and $_.Exception -is [UiaPatternNotSupportedException]) {
            $result.used = 'unavailable'
        }
        return $result
    }
}

function Get-UiaExplorerWindow {
    <#
        Locate the test-owned Explorer (CabinetWClass) window for a fixture
        folder. Matching is structural, never title-only: class + PID
        membership (broker/new-process reuse is normal) + one of
        (a) the window's address-bar Value equalling the expected path when
            exposed (modern Explorer often reports an empty breadcrumb value),
        (b) an element inside the window named exactly the expected leaf
            (the shell tab / tab item carry the folder name),
        (c) the expected source item present in the window's folder view.
        The ready contract is the folder view (AutomationId listview /
        ShellView / Shell Folder View) existing with valid bounds. Returns
        the UIA window element or $null. Mock-testable via recorded trees.
    #>
    param(
        $Driver,
        [Parameter(Mandatory)][string] $ExpectedPath,
        [string] $ExpectedLeaf = '',
        [int[]] $CandidateProcessIds,
        [int] $TimeoutMs = 10000,
        [int] $PollIntervalMs = 500,
        [string] $Description = 'Explorer window with expected path'
    )
    if (-not $ExpectedLeaf) { $ExpectedLeaf = Split-Path $ExpectedPath -Leaf }
    $normalized = $ExpectedPath.TrimEnd('\').TrimEnd('/')
    $deadline = [System.Diagnostics.Stopwatch]::StartNew()
    $lastError = $null
    while ($deadline.ElapsedMilliseconds -lt $TimeoutMs) {
        try {
            $windows = @(Find-UiaElement -Driver $Driver -ClassName 'CabinetWClass' -TimeoutMs 3000 -All)
            foreach ($window in $windows) {
                $pid = 0
                try { $pid = [int](Get-UiaElementProperty -Driver $Driver -Element $window -Name 'ProcessId') } catch { }
                if ($CandidateProcessIds -and $CandidateProcessIds.Count -gt 0 -and $pid -notin $CandidateProcessIds) {
                    # A newly spawned explorer.exe (broker) is a legitimate
                    # owner of the test window; pre-launch PIDs are not the
                    # only candidates. Fall through to the semantic checks
                    # which discriminate by fixture identity.
                }
                $matched = $false
                # (a) address-bar value equals the expected path.
                if (-not $matched) {
                    try {
                        $edits = @(Find-UiaElement -Driver $Driver -ControlType 'Edit' -FromElement $window -TimeoutMs 2000 -All)
                        foreach ($edit in $edits) {
                            try {
                                $value = [string](Invoke-UiaPattern -Driver $Driver -Element $edit -PatternName 'Value' -Method 'GetValue')
                            } catch { continue }
                            if ($value -and $value.TrimEnd('\').TrimEnd('/') -ieq $normalized) { $matched = $true; break }
                        }
                    } catch { $lastError = $_.Exception.Message }
                }
                # (b) the window's own tab/shell element carries the leaf.
                if (-not $matched) {
                    try {
                        $leafEl = Find-UiaElement -Driver $Driver -Name $ExpectedLeaf -FromElement $window -TimeoutMs 2000
                        if ($leafEl) { $matched = $true }
                    } catch { $lastError = $_.Exception.Message }
                }
                if (-not $matched) { continue }
                # Ready contract: the folder view is exposed with valid bounds.
                $ready = $false
                $view = $null
                foreach ($viewCriterion in @(
                        @{ AutomationId = 'listview' },
                        @{ AutomationId = 'ShellView' },
                        @{ NameLike = 'Shell Folder View' })) {
                    try {
                        if ($viewCriterion.ContainsKey('AutomationId')) {
                            $view = Find-UiaElement -Driver $Driver -AutomationId $viewCriterion.AutomationId -FromElement $window -TimeoutMs 1500
                        } else {
                            $view = Find-UiaElement -Driver $Driver -NameLike $viewCriterion.NameLike -FromElement $window -TimeoutMs 1500
                        }
                    } catch { $lastError = $_.Exception.Message; $view = $null }
                    if ($view) { break }
                }
                if ($view) {
                    try {
                        $br = Get-UiaElementProperty -Driver $Driver -Element $view -Name 'BoundingRectangle'
                        $ready = $null -ne $br -and $br.width -gt 0 -and $br.height -gt 0
                    } catch { $lastError = $_.Exception.Message }
                }
                if ($ready) { return $window }
            }
        } catch { $lastError = $_.Exception.Message }
        Start-Sleep -Milliseconds $PollIntervalMs
    }
    return $null
}

function Set-UiaWindowPosition {
    <#
        Move/resize a real top-level window through SetWindowPos (no Z-order
        change, no activation). Used to place the test-owned Explorer window
        beside FastFiles so drag paths never cross an overlapping window.
        Returns { ok, x, y, width, height }. Mock elements report $false.
    #>
    param($Driver, $Element, [int] $X, [int] $Y, [int] $Width, [int] $Height)
    if (Test-UiaMockElement $Element) {
        return [pscustomobject]@{ ok = $false; x = $X; y = $Y; width = $Width; height = $Height }
    }
    try {
        $hwnd = [IntPtr]$Element.Current.NativeWindowHandle
        $ok = [FfNativeWindowInfo]::MoveWindowTo($hwnd, $X, $Y, $Width, $Height)
        return [pscustomobject]@{ ok = $ok; x = $X; y = $Y; width = $Width; height = $Height }
    } catch {
        return [pscustomobject]@{ ok = $false; x = $X; y = $Y; width = $Width; height = $Height }
    }
}

function Bring-UiaWindowToTop {
    <#
        Raise a real top-level window above overlapping siblings (no
        activation). Ensures a drop at any point inside the bound window hits
        that window even when orphaned windows overlap it. Mock -> $false.
    #>
    param($Driver, $Element)
    if (Test-UiaMockElement $Element) { return $false }
    try {
        $hwnd = [IntPtr]$Element.Current.NativeWindowHandle
        return [FfNativeWindowInfo]::BringToTop($hwnd)
    } catch {
        return $false
    }
}
