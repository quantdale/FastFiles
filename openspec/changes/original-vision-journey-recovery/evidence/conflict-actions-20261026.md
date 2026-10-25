# Native conflict action recovery — 2026-10-26

## Actual baseline and source identity

Public UIA/palette Copy and Paste create a real name collision only in a marker-owned synthetic session fixture. Compiled source **a3e20abb2cf5f97247ae632751743ce518ace69c**: UIA cannot find the painted Replace action. [Dark capture](conflict-actions-20261026/before-dark.png), [compact red metadata](conflict-actions-20261026/before.json). The native child names include an empty string where Replace is drawn. Actual Cancel through the existing E2E public-pointer fallback preserves both incoming and destination SHA-256; lifecycle cleanup passes. This baseline is red, not a passing conflict journey.

ConflictDialog.cpp, UiComponents.h and UITheme.h have empty source diff between verified branch base981a9ba and a3e20ab. This supports baseline attribution for this dialog only; the full applications are not equivalent.

The actual120DPI dark image also shows a white custom-window background, black checkbox caption on a dark field, and inconsistent native buttons. Source confirms WM_CTLCOLORDLG alone cannot erase this custom HWND and no control has WS_TABSTOP. Native Tab behavior still needs the new public-input regression; source inspection alone does not certify keyboard traversal.

## Design and implementation

Keep the established Native Precision Things/shadcn reference lock and localized warning around Replace. Supply the real native caption and Tab stops, preserve Skip initial focus, paint WM_ERASEBKGND using the token brush, and reuse Settings' native Explorer theme helper (including system High Contrast reset). Checkbox text follows theme.text. The owner-drawn action uses its configured body font. No custom accessibility-only state, control IDs, operation decisions, data contracts, conflict policy or process boundaries change.

The baseline audit evolved through explicitly retained harness failures: audit01 used the wrong cleanup result field (actual cleanup passed); audit02 records missing Replace; audit03 captures the actual window then finds the host native Cancel lacks InvokePattern; audit04 reuses the existing real-pointer fallback and records the conclusive red plus preserved content. No product assertion was suppressed or relabeled PASS. Original logs remain under the integration worktree's build/native-status-integration-20261026.

## Regression and current qualification

`verify/e2e/Test-FileConflictActions.ps1` requires fresh artifacts and a clean-source provenance gate. Ordinary persisted theme settings are seeded only inside fresh session sandboxes. It checks all five control names/physical bounds, default Skip and a full real Tab cycle, then Cancel/Skip/Keep both/Replace in both themes. Incoming and unrelated sentinel hashes must remain intact; each destination or unique copy must match the intended exact content. It captures both real dialogs and requires a separately reviewed light/dark brightness change. The operation fixture cannot escape its marked root. It changes no user/system appearance setting.

Development diagnostic all-target build445/445, PowerShell AST and strict OpenSpec pass. Native green, focused operation regressions, final clean-source binding and visual review are pending. High Contrast/other physical DPI, multi-conflict apply-to-all interaction and production release remain unqualified.
