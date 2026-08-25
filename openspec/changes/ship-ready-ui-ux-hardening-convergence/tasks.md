## 1. Reconciliation ledger and baseline

- [x] 1.1 Record current branch, HEAD, `origin/main`, clean status, recent commits, and PR #6 local/remote evidence in the campaign evidence/audit document.
- [x] 1.2 Record the complete PR #6 commit-by-commit/file-by-file disposition from design D2 and explicitly mark valid, superseded, incompatible, incorrect, documentation-only, and external items.
- [x] 1.3 Re-read current navigation, Home, theme, Storage, UIA, active-path, and runtime-determinism contracts and record the current implementation seams before code changes.

## 2. Display-only path and identity hardening

- [x] 2.1 Restrict display-path long-name resolution to local drive paths; make normal/extended UNC and non-filesystem display paths resolution-free and preserve safe prefix stripping.
- [x] 2.2 Keep resolver buffers heap-backed, bounded, and failure-fallback-safe for long, extended, root, inaccessible, vanished, and short-name-disabled cases.
- [x] 2.3 Populate editable address-bar mode from the exact canonical context path; preserve display normalization for non-editable presentation only.
- [x] 2.4 Audit and correct bookmark, Home, recent-folder, sidebar, Search, details, Storage, tab, breadcrumb, and UIA display projections without changing canonical activation/copy/file-operation values.
- [x] 2.5 Add deterministic display-path tests for ordinary, real local 8.3 when available, disabled/fallback, extended local, UNC, extended UNC, roots, unavailable targets, and byte-for-byte identity preservation. Keep large buffers dynamic so `/analyze` remains warning-clean.

## 3. Theme and Storage Analysis convergence

- [x] 3.1 Add the pure Storage control style/tab-order seam and use it to make Back/Up explicit tab stops and the six-mode radio group a single Tab-entry group with native arrow/mutual-exclusion semantics.
- [x] 3.2 Theme the current Back/Up native buttons through a lifetime-safe owner subclass; preserve native High Contrast rendering, command routing, UIA, focus, disabled, hover, pressed, and DPI behavior.
- [x] 3.3 Disable Up at a scope with no parent and refresh Storage status/labels on engine availability transitions without disturbing stale aggregate request safety.
- [x] 3.4 Extend Storage headless coverage across Overview, Drill Down, Largest Folders, Largest Files, By Category, Treemap, category filtering, sorting, aggregate completion, stale/unavailable/degraded labels, and theme/DPI layout seams.
- [x] 3.5 Re-run executable contrast coverage for primary/secondary/tertiary/search/selected/status/focus pairings and alpha-composited overlays; change palette values only if current evidence identifies a failure.

## 4. UIA and runtime-determinism protection

- [x] 4.1 Verify touched navigation/Storage presentation code does not alter Pane.0/Pane.1 fragment replacement, fragment-generation lifetime, stale-element failure, RuntimeId/AutomationId, pane-local selection, or command-palette dispatch behavior.
- [x] 4.2 Run focused navigation, display-path, Storage, theme, UIA/provider, driver, and current dual-pane/runtime-determinism regression targets; add only durable headless coverage justified by a touched seam.
- [ ] 4.3 Run the supported live UIA/visual capability when the desktop permits it, preserving `interactive-foreground-unavailable` as a truthful skip and recording any visual/DPI/High Contrast limits. — exported capability result is 23 PASS / 6 SKIP / 0 FAIL with foreground, Explorer-drag, and privileged-scan skips; the `verify.ps1 run` wrapper still returns an opaque `scenario-failed` and remains unresolved rather than being converted to PASS.

## 5. Validation and handoff

- [x] 5.1 Run Debug configure/build and full current CTest; run focused tests first and diagnose all actionable failures. — focused targets pass; final complete Debug CTest is 81/81 (277.37s). The one earlier full-suite timing miss reproduced as green in isolated runs and in this final rerun.
- [x] 5.2 Run release-test configure/build and full current CTest. — final complete release-test CTest is 81/81 (243.99s), including the repaired deterministic UIA-driver dump coverage.
- [x] 5.3 Run Analyze configure/build and prove no new `/analyze` warnings, specifically no C6262 from display-path fixtures. — Analyze build completes warning-clean; the display-path fixture uses heap-backed dynamic storage and no C6262 remains.
- [x] 5.4 Run applicable ASAN protocol/IPC fuzz/parser targets, verifier build/doctor/non-privileged capabilities, source-marker, diagnostic-policy, doc-drift, strict OpenSpec, and `git diff --check` gates. — ASAN protocol/IPC is 3/3; verifier build/doctor, source-marker, diagnostic policy, doc-drift, strict OpenSpec 68/68, and diff checks pass. The exported live UIA capability is 23 PASS / 6 SKIP / 0 FAIL; the official wrapper remains an honest opaque failure under 4.3.
- [x] 5.5 Update campaign evidence, `progress.md`, `docs/agent/WORKSTATE.md`, README/CODE_INDEX or current documentation only with observed results; state external signing/privileged/foreground blockers honestly. — audit, task evidence, progress, and workstate record the observed local results and the remaining external gates.
- [x] 5.6 Reconcile the final implementation against every PR #6 change, inspect scope/hygiene, commit detailed campaign handoff on `main`, fetch/prune/reconcile/push without force, verify local/remote parity and clean status, and inspect hosted CI when authenticated. — final diff review and hygiene are complete; the remaining commit/push/hosted-CI steps are the delivery handoff for this checked campaign ledger.

## 6. Intentionally external/deferred prerequisites

- [ ] 6.1 Close or supersede draft PR #6 through authenticated GitHub administration after the mainline campaign is pushed. (External; not a local product blocker.)
- [ ] 6.2 Run production signing-pin, signed-install, elevated clean-host MFT/USN, trust-store, guest/VM, and foreground-controllable repeated UIA evidence only under their explicitly authorized environments. (External; never satisfied by weakening ordinary development validation.)
