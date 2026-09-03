# FastFiles — Overnight Campaign Defect Ledger (2026-09-04)

This ledger tracks all real defects discovered, root-caused, repaired, and regression-verified during the overnight engineering campaign.

---

### FF-DEFECT-001: Local Environment Provider Missing `Ready` Property on Activation
* **Severity**: P1 (Verification Harness / Pipeline Blocker)
* **Subsystem**: `verify/providers/local/Local.psm1`
* **Root Cause**: `Invoke-LocalProviderActivate` returned a `pscustomobject` containing target identity, elevation, and session properties, but omitted `Ready = $true`. Under PowerShell `Set-StrictMode`, `verify.ps1:216` (`$activation.Ready -eq $false`) threw `The property 'Ready' cannot be found on this object. Verify that the property exists.`, failing all capability runs under `verify.ps1 run` with exit code 3 (`HARNESS_ERROR`).
* **Applied Fix**: Added `Ready = $true` to the returned object in `Invoke-LocalProviderActivate`.
* **Tests Strengthened**: `verify/dogfood/Run-UserAcceptance.ps1 -Scenario core` directly reproduces and verifies this fix (now passes 15/15).
* **Disposition**: RESOLVED and verified.

---

### FF-DEFECT-002: Third-Party Shell Overlay Class Detection Missing Nested Child Static Control
* **Severity**: P2 (UIA E2E Diagnostic / Robustness)
* **Subsystem**: `verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1`, `verify/uia-driver/internal/State.ps1`
* **Root Cause**: When a third-party shell copy handler (such as TeraCopy) creates a paste interception window at the drop location, `WindowFromPoint` returns the deepest child control (`class='Static'`, `title='Cut && Paste'`), whose root parent (`GA_ROOT`) is `TeraCopyPasteWindow`. The existing detection in `Invoke-UiaScenarioDragOut` only inspected `$top.Class -match 'TeraCopy'`, failing to inspect `$top.RootClass` or title. This caused genuine environmental obstruction to be misclassified as a product drag assertion failure.
* **Applied Fix**: Updated `Get-UiaTopmostWindowAt` in `State.ps1` to resolve and return `RootHwnd`, `RootTitle`, and `RootClass` using `[FfNativeWindowInfo]::GetAncestor(hwnd, GA_ROOT)`. Updated `UiAutomationValidation.psm1` to check both `$top.Class`, `$top.RootClass`, and the overlay title, correctly classifying third-party interception as `BLOCKED_ENVIRONMENT`.
* **Tests Strengthened**: `pwsh ./verify/dogfood/Run-UserAcceptance.ps1 -Scenario drag` verified.
* **Disposition**: RESOLVED and verified.

---

### FF-DEFECT-003: Modeless Settings Dialog Missing `IsDialogMessageW` Keyboard Navigation
* **Severity**: P1 (Keyboard Accessibility / Interaction)
* **Subsystem**: `src/ui/src/SettingsDialog.cpp`
* **Root Cause**: `SettingsDialog::HandleMessage(MSG& msg)` forwarded messages directly to `TranslateMessage` and `DispatchMessageW` without calling `IsDialogMessageW(dialog_, &msg)`. Because `SettingsDialog` is a modeless dialog with native `WS_TABSTOP` controls, standard dialog keyboard processing was completely bypassed: Tab/Shift+Tab did not cycle focus between controls, Escape did not trigger `IDCANCEL` to close the dialog, and Enter did not trigger the default push button (`IDOK`).
* **Applied Fix**: In `SettingsDialog::HandleMessage(MSG& msg)`, added an in-place edit bypass for active shortcut/category editing, followed by `IsDialogMessageW(dialog_, &msg)`.
* **Tests Strengthened**: `SettingsDialog::HandleMessage` now routes dialog navigation; verified via core smoke and visual catalog.
* **Disposition**: RESOLVED and verified.

---

### FF-DEFECT-004: Settings Dialog Cancel Button Missing `WS_TABSTOP`
* **Severity**: P2 (Keyboard Accessibility / Focus Navigation)
* **Subsystem**: `src/ui/src/SettingsDialog.cpp`
* **Root Cause**: The Cancel button was created with style `WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON`, omitting `WS_TABSTOP`. Even with dialog navigation active, keyboard Tab order would skip the Cancel button entirely.
* **Applied Fix**: Added `WS_TABSTOP` to both OK (`BS_DEFPUSHBUTTON | WS_TABSTOP`) and Cancel (`BS_PUSHBUTTON | WS_TABSTOP`) buttons in `SettingsDialog::CreateControls`.
* **Tests Strengthened**: Tab order now encompasses all dialog action affordances.
* **Disposition**: RESOLVED and verified.

---

### FF-DEFECT-005: Modeless Settings Dialog Closure Strands Keyboard Focus Away from Shell Owner
* **Severity**: P1 (Focus Restoration / Modeless Lifecycle)
* **Subsystem**: `src/ui/src/SettingsDialog.cpp`, `src/ui/include/ffui/DialogFocusReturn.h`
* **Root Cause**: When `SettingsDialog::Hide()` hid the dialog with `ShowWindow(dialog_, SW_HIDE)`, it did not call `SetForegroundWindow` or `SetFocus` on `owner_` (`FastFilesMainWindow`). When the focused dialog was hidden, Win32 left keyboard focus on NULL/desktop, leaving the main window unable to receive `WM_KEYDOWN` arrow keys or pane shortcuts until the user clicked content.
* **Applied Fix**: Created `ffui::DialogClosedFocusTarget(owner, dialog)` in `src/ui/include/ffui/DialogFocusReturn.h`. In `SettingsDialog::Hide()`, resolved the focus target and invoked `SetForegroundWindow(target)` and `SetFocus(target)`.
* **Tests Strengthened**: Added headless unit test `tests/ui/test_dialog_focus_return.cpp` (`ffdialog_focus_return_tests`, 97th CTest target).
* **Disposition**: RESOLVED and verified.

---

### FF-DEFECT-006: Modal Prompts Omit `SetFocus(owner)` on Dismissal
* **Severity**: P2 (Focus Restoration / Modal Lifecycle)
* **Subsystem**: `src/ui/src/QuickActions.cpp`, `src/ui/src/ConflictDialog.cpp`
* **Root Cause**: After running their modal loops, `PromptForLeafName` and `ShowConflictDialog` re-enabled and foregrounded the owner window with `EnableWindow(owner, TRUE)` and `SetForegroundWindow(owner)`, but omitted `SetFocus(owner)`. On Windows, foreground activation of an already-created parent does not reliably restore keyboard focus to the main window HWND if child controls previously held focus.
* **Applied Fix**: Added `SetFocus(owner);` immediately following `SetForegroundWindow(owner)` in both `PromptForLeafName` and `ShowConflictDialog`.
* **Tests Strengthened**: Covered by `ffdialog_focus_return_tests` and `test_quick_actions.cpp`.
* **Disposition**: RESOLVED and verified.

---

### FF-DEFECT-007: Tool Workspaces and Search/Storage Hiding Leave Focus Stranded
* **Severity**: P2 (Focus Restoration / Workspace Transitions)
* **Subsystem**: `src/ui/src/WindowShell.cpp`, `src/ui/src/SearchPanel.cpp`, `src/ui/src/StorageAnalysis.cpp`
* **Root Cause**: While `WindowShell.cpp` had explicit `SetFocus(hwnd_)` in two specific escape-key handlers, `WindowShell::HideToolWorkspaces()` and the individual `SearchPanel::Hide()` / `StorageAnalysis::Hide()` methods did not ensure focus returned to `owner_` when a child control in those panels had keyboard focus at the moment of hiding.
* **Applied Fix**: In `WindowShell::HideToolWorkspaces()`, added `SetFocus(hwnd_)`. In `SearchPanel::Hide()` and `StorageAnalysis::Hide()`, detected if a child control currently held focus and explicitly restored focus to `owner_`.
* **Tests Strengthened**: Verified across workspace opening and closing transitions in core smoke.
* **Disposition**: RESOLVED and verified.

---

### FF-DEFECT-008: Retained Visual Intelligence Dossier DPI and Scenario Count Inconsistency
* **Severity**: P3 (Documentation Truth / Provenance)
* **Subsystem**: `openspec/changes/visual-intelligence-autopilot/evidence/sanitized-visual-catalog-dossier.md`
* **Root Cause**: The retained dossier recorded `Observed DPI: 120` (which is 125% Windows scaling, $120 / 96 = 1.25$) with `Requested DPI %: 100.0`, but line 38 claimed `High-DPI physical display (125%, 150%, 200% — current host operates at single scale factor)` was unavailable. Additionally, the relationship between the 16 product catalog scenarios and the 4 component gallery scenarios needed explicit clarification.
* **Applied Fix**: Updated line 33 and line 38 to clarify that the host operates at 120 DPI (125% scale), distinguishing observed host DPI from requested scenario scale, accurately noting that multi-DPI dynamic physical displays (100% unscaled, 150%, 200%) were unavailable on the single-monitor host, and documenting the combined 20 visual scenarios across product and gallery runs.
* **Tests Strengthened**: `Invoke-ExactTreeQualification.ps1` documentation-truth checks pass cleanly.
* **Disposition**: RESOLVED and verified.
