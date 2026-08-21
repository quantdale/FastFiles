# Zero-touch autonomous engineering (uia-driver): Win32 P/Invoke helpers for
# driving real Win32 controls that UIA expand/click/keyboard paths cannot
# drive (e.g. the owner-drawn search sort COMBOBOX whose dropdown popup is
# not UIA-discoverable). This module drives the native control directly
# through its HWND.
$script:UiaWin32ComDefined = $false
function Set-UiaComboSelectionHwnd {
    param([IntPtr] $MainWindowHandle, [int] $ControlId, [int] $Index)
    if (-not $script:UiaWin32ComDefined) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace FfUiaCom {
  public static class Win32 {
    [DllImport("user32.dll", CharSet=CharSet.Auto)]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    public static bool SetComboSelection(IntPtr parent, int ctrlId, int index) {
      IntPtr cb = GetDlgItem(parent, ctrlId);
      if (cb == IntPtr.Zero) return false;
      const uint CB_SETCURSEL = 0x014E;
      const uint WM_COMMAND = 0x0111;
      SendMessage(cb, CB_SETCURSEL, new IntPtr(index), IntPtr.Zero);
      IntPtr wp = new IntPtr((1 << 16) | (ctrlId & 0xFFFF));
      PostMessage(parent, WM_COMMAND, wp, cb);
      return true;
    }
  }
}
'@
        $script:UiaWin32ComDefined = $true
    }
    try { return [FfUiaCom.Win32]::SetComboSelection($MainWindowHandle, $ControlId, $Index) } catch { return $false }
}

function Set-UiaComboSelectionElement {
    # Drives a COMBOBOX element directly by its own HWND: CB_SETCURSEL selects
    # the item and a CBN_SELCHANGE WM_COMMAND to the parent notifies the app,
    # exactly as a real user selection would. Preferred over the ControlId
    # variant because it needs no identifier lookup -- the UIA element IS the
    # native control once the host raw element provider is exposed.
    param([IntPtr] $ComboHandle, [int] $Index)
    if ($ComboHandle -eq [IntPtr]::Zero) { return $false }
    if (-not $script:UiaWin32ComDefined) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace FfUiaCom {
  public static class Win32Combo {
    [DllImport("user32.dll", CharSet=CharSet.Auto)]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern IntPtr PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern IntPtr GetParent(IntPtr hWnd);
    public static bool SetSelection(IntPtr cb, int index) {
      if (cb == IntPtr.Zero) return false;
      const uint CB_SETCURSEL = 0x014E;
      const uint WM_COMMAND = 0x0111;
      IntPtr parent = GetParent(cb);
      if (parent == IntPtr.Zero) return false;
      // Control identifier of the combo = its child id within the parent.
      int ctrlId = (int)SendMessage(cb, 0x0129 /*WM_GETCONTROLID*/, IntPtr.Zero, IntPtr.Zero);
      SendMessage(cb, CB_SETCURSEL, new IntPtr(index), IntPtr.Zero);
      IntPtr wp = new IntPtr((1 << 16) | (ctrlId & 0xFFFF));
      PostMessage(parent, WM_COMMAND, wp, cb);
      return true;
    }
  }
}
'@
        $script:UiaWin32ComDefined = $true
    }
    try { return [FfUiaCom.Win32Combo]::SetSelection($ComboHandle, $Index) } catch { return $false }
}

Export-ModuleMember -Function Set-UiaComboSelectionHwnd, Set-UiaComboSelectionElement
