# Visual Closure Evidence: FF-VIS-FOCUS-001

Date: 2026-09-03
Finding ID: `FF-VIS-FOCUS-001`
Scenario: `browse-keyboard-focus`
Surface: Column View browse (post address-bar navigation)
Category: Focus / Keyboard Navigation / Accessibility
Severity: Major
Confidence: 1.0
Disposition: `FIXED`

## 1. Problem Statement & Root Cause

After address-bar navigation (`Ctrl+L`, edit path, press `Enter` to commit), Win32 keyboard focus remained stranded on the `FastFilesNavigationChrome` child window (`GetGUIThreadInfo` reported focus on `FastFilesNavigationChrome`; UI Automation reported `FocusedElement` = `chrome Pane`).

Because `FastFilesNavigationChrome` does not route `WM_KEYDOWN` messages to the active view, pressing `DOWN` or other arrow keys resulted in a silent no-op. No row in `ColumnView` received keyboard focus or selection.

In the automated visual catalog, `browse-keyboard-focus` sent `DOWN` and captured a screenshot, but because no row received focus or selection, the captured screenshot was pixel-identical to the prior `browse-column-view` screenshot. The duplicate-image protection guard flagged this as an unexpected duplicate capture and failed the scenario.

## 2. Product Fix

The root cause was fixed in `src/ui/src/NavigationChrome.cpp` (commit `d47063055859c4290c3f1a1f58e40edf06a962e6`):
- When address-edit closes (on commit success or cancel restore), `NavigationChrome` now calls `SetFocus(AddressEditClosedFocusTarget(owner_, window_))`.
- Keyboard focus is restored to the shell owner window (`FastFilesMainWindow`), which properly routes `WM_KEYDOWN` to `ColumnView`.
- A formal focus-target contract was added in `src/ui/include/ffui/AddressBarFocus.h` with dedicated C++ regression tests in `tests/ui/test_address_bar_focus.cpp` (`ffaddress_bar_focus_tests`), verifying both live-owner and fallback target behavior.

## 3. Visual Catalog Hardening

In `verify/visual/Invoke-VisualCatalog.ps1` (commit `cb2d96765a2bbfed430dc157519b83b528bb3925`):
- Replaced the prior hardcoded `Pass = $true` assertion with `Invoke-CatalogKeyboardFocus`.
- Verifies FastFiles foreground ownership before input.
- Polls for populated ColumnView rows.
- Records initial focused/selected state.
- Delivers `DOWN` key input.
- Bounded-polls for semantic focus and selection transition on a visible row.
- Requires pixel delta via the duplicate-image guard.

## 4. Before / After Visual Closure Evidence

Closure packet location: `verify/runs/visual-closure/browse-keyboard-focus-20260903-182912/`

### Captures & Hashes
- **Before Image**: `before-browse-column-view.png`
  SHA-256: `e8c9d0f42dfb256122d8888d1ca7cbf00b9a36c2e81f14fa63b1e09451f81fe8`
- **After Image**: `after-browse-keyboard-focus.png`
  SHA-256: `8b974a07720d2106807ac14ab83a68842e4fa1a30d4d91e9943d6d7041428f98`
- **After Baseline (unfocused ColumnView)**: `after-browse-column-view.png`
  SHA-256: `674bc405689716ddd9c7d2b0b974c5c186d6fc93d83b5da4a21d12d921091053`

### Deterministic Comparison
- **Minimal Pair Comparison** (`after-browse-column-view.png` vs `after-browse-keyboard-focus.png`, same live run):
  - Classification: `Different`
  - Total Pixels: 1,001,088 (1264x792)
  - Changed Pixels: 8,919 (0.89%)
  - Location: Exclusively localized to the focused/selected row in `ColumnView`.
  - Diff Map: `diff-minimal-pair.png`
- **Primary Comparison** (`before-browse-column-view.png` vs `after-browse-keyboard-focus.png`):
  - Classification: `Different`
  - Total Pixels: 1,001,088
  - Changed Pixels: 361,687 (36.13%, includes sandbox path column variance across runs)
  - Diff Map: `diff-map.png`

## 5. Independent Lane Verification

| Lane | Result | Evidence |
| --- | --- | --- |
| **Visual Regression** | `PASS` | Comparator classified `Different`; minimal-pair delta is 0.89% (within threshold 0.01) isolated to focused row; duplicate-image guard satisfied. |
| **Visual Review** | `BLOCKED` | External real vision-critic provider unconfigured in local environment (FakeCritic passes in CI; external provider modeled as external). |
| **UIA Semantic** | `PASS` | Pre-fix: UIA `FocusedElement` = chrome Pane, zero rows focused/selected before and after `DOWN`. Post-fix: shell owner retains focus, `DOWN` focuses and selects row 0 (`EmptyFolder`), second `DOWN` advances focus and selection to row 1. Verified live via UIA tree probe. |
| **Accessibility** | `PASS` | Focus ring visually rendered on active row; UIA `HasKeyboardFocus` = true and `IsSelected` = true on focused row; keyboard navigation operable without pointer interaction. |
| **Functional** | `PASS` | `ffaddress_bar_focus_tests` PASS (AddressEditClosedFocusTarget owner/fallback contract); Debug 96/96 PASS; release-test 96/96 PASS; ASAN protocol/IPC slice 3/3 PASS; visual contract suites 10/10 PASS; OpenSpec strict 78/78 PASS. |
| **Performance Sanity** | `PASS` | `SetFocus` to cached owner HWND is O(1) with zero heap allocations; imperceptible latency on address-bar edit closure. |

## 6. Provenance & Disposition
- Finding: `FF-VIS-FOCUS-001`
- Shipped Product Fix Commit: `d47063055859c4290c3f1a1f58e40edf06a962e6`
- Catalog Harness Fix Commit: `cb2d96765a2bbfed430dc157519b83b528bb3925`
- Disposition: `FIXED`
- Task 8.2 status: Satisfied with live before/after closure packet and real shipped visual/product fix.
