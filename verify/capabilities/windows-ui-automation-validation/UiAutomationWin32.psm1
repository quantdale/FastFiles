# Zero-touch autonomous engineering (uia-driver): Win32 P/Invoke helpers for
# driving real Win32 controls that UIA expand/click/keyboard paths cannot
# drive (e.g. the owner-drawn search sort COMBOBOX whose dropdown popup is
# not UIA-discoverable). This module drives the native control directly
# through its HWND.
$script:UiaWin32ComDefined = $false
$script:UiaWin32ComboComDefined = $false
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
    if (-not $script:UiaWin32ComboComDefined) {
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
        $script:UiaWin32ComboComDefined = $true
    }
    try { return [FfUiaCom.Win32Combo]::SetSelection($ComboHandle, $Index) } catch { return $false }
}

function Get-UiaListBoxState {
    param([IntPtr] $ListHandle)
    if ($ListHandle -eq [IntPtr]::Zero) {
        return [pscustomobject]@{ Available = $false; Items = @(); SelectedIndex = -1; Visible = $false }
    }
    if (-not ('FfUiaCom.Win32ListBox' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
namespace FfUiaCom {
  public static class Win32ListBox {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, StringBuilder lParam);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    public static string[] GetItems(IntPtr hWnd) {
      const uint LB_GETCOUNT = 0x018B;
      const uint LB_GETTEXTLEN = 0x018A;
      const uint LB_GETTEXT = 0x0189;
      int count = SendMessage(hWnd, LB_GETCOUNT, IntPtr.Zero, null).ToInt32();
      if (count < 0 || count > 4096) return Array.Empty<string>();
      var items = new List<string>(count);
      for (int i = 0; i < count; ++i) {
        int length = SendMessage(hWnd, LB_GETTEXTLEN, new IntPtr(i), null).ToInt32();
        if (length < 0 || length > 16384) return Array.Empty<string>();
        var text = new StringBuilder(length + 1);
        if (SendMessage(hWnd, LB_GETTEXT, new IntPtr(i), text).ToInt64() < 0) return Array.Empty<string>();
        items.Add(text.ToString());
      }
      return items.ToArray();
    }
    public static int GetSelectedIndex(IntPtr hWnd) {
      const uint LB_GETCURSEL = 0x0188;
      return SendMessage(hWnd, LB_GETCURSEL, IntPtr.Zero, null).ToInt32();
    }
    public static bool IsVisible(IntPtr hWnd) { return IsWindowVisible(hWnd); }
  }
}
'@
    }
    try {
        $items = @([FfUiaCom.Win32ListBox]::GetItems($ListHandle))
        return [pscustomobject]@{
            Available = $true
            Items = $items
            SelectedIndex = [FfUiaCom.Win32ListBox]::GetSelectedIndex($ListHandle)
            Visible = [FfUiaCom.Win32ListBox]::IsVisible($ListHandle)
        }
    } catch {
        return [pscustomobject]@{ Available = $false; Items = @(); SelectedIndex = -1; Visible = $false }
    }
}

Export-ModuleMember -Function Set-UiaComboSelectionHwnd, Set-UiaComboSelectionElement, Get-UiaListBoxState
