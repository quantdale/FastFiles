# Zero-touch autonomous engineering (uia-driver): Win32 P/Invoke helpers for
# driving real Win32 controls that the product's custom UIA provider exposes as
# ControlType=Pane (e.g. the owner-drawn search sort COMBOBOX). Standard UIA
# expand/click/keyboard paths cannot select an item on those controls, so this
# module drives the native control directly through its HWND.
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
