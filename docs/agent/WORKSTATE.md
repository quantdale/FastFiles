# Fast Files — Agent Workstate

## verifier-single-artifact-envelope-fix — VERIFIED 2026-09-13 (local readiness audit slice)

- Found by `pwsh ./verify/verify.ps1 release-readiness -Mode compose`: `runtime/windows-filesystem-validation` failed with `HARNESS_ERROR` / `result-envelope-composition-failed`, summary naming `[System.Management.Automation.PSObject] does not contain a method named 'op_Addition'` (plus a cascading `runtime-capability-suite` FAIL).
- Root cause (`verify/core/CapabilityRunner.psm1`, `New-CapabilityWorkerEnvelope`): `$rawArtifacts = @(Get-VerificationProperty ... 'Artifacts') | Where-Object { $null -ne $_ }` lets the pipeline unwrap a **single** artifact into a scalar PSObject; the later `@($rawArtifacts + $workerArtifacts)` then evaluates `PSObject + array` and throws `op_Addition`. Only capabilities whose primary path emits exactly one artifact hit it; `FilesystemValidation.psm1:105` does exactly that (`WindowsFilesystemValidation` expected PASS). Audit of the same pattern elsewhere: `ResultModel.psm1:192` and `InstallService.psm1:65` are already `@()`-wrapped; `InstallService.psm1:188` / `PrivilegeValidation.psm1:82` unwrap but only use `.Count`/`-join`/`@()` re-wrap, so they are safe.
- Fix: wrap the whole pipeline in `@(...)` so 0/1/many artifacts always yield an array, with a comment naming the failure mode.
- Regression coverage: `verify/core/tests/Test-ResultComposition.ps1` gained fixture mode `single-artifact` plus two worker-boundary assertions (`ffverify_result_composition_ps_tests`): the envelope must compose `PASS/PASS` and preserve the capability artifact alongside the two worker artifacts (observed `artifacts=3`).
- Evidence: buggy form reproduces the exact `op_Addition` message while the fixed form composes 3 artifacts; `pwsh -File verify/core/tests/Test-ResultComposition.ps1` all PASS; CTest `ffverify_result_composition_ps_tests` Passed (58 s); `pwsh ./verify/verify.ps1 run -Capability windows-filesystem-validation` now `status=PASS classification=PASS summary=7 filesystem scenario batteries passed; 0 failed; 0 skipped` (exit 0; run `verify/runs/ad-hoc/20260913-074442`).
- NEXT EXACT ACTION: re-run `pwsh ./verify/verify.ps1 release-readiness -Mode compose` on the committed tip and record the composed result; only `production-release-artifact-readiness` (fail-closed external signing) and elevation/tier-3 SKIPs should remain non-PASS.

## shell-constant-consolidation — VERIFIED 2026-09-13 (same audit slice)

- Gate note: `verify.ps1 build` reported 8/9 `ASSERTION_FAILURE` once while a DPI probe was launching the app concurrently; the same commit passed 9/9 with no concurrent activity. Do not launch the app/UIA probes while the gate runs.
- DPI truth correction: the host is multi-monitor with one display at 125% scaling (`PerMonitorSettings` DpiValue=1; the app window measures 120 DPI), so the session's probes/captures are 125% evidence; earlier docs claiming a 96-DPI host were wrong. 100%/150%/200% DPI and High Contrast remain external.

- The shell chrome height had three independent definitions: `ChromeLayout.h`'s own literals (used to size the chrome window), `UiMetrics::kChromeHeight` (consumed by the shell layout and, after this change, the sidebar), and a local `constexpr int kChromeHeight = 72` in `NavigationSidebar.cpp`. A change to any one would silently misalign the sidebar/content under the chrome.
- Fixed: the sidebar now consumes `UiMetrics::kChromeHeight`; `ffui_state_palette_tests` gained `TestChromeHeightTokenAgreement`, which fails if the chrome-layout height and the shared token ever drift.
- Validation: Debug CTest **103/103**, release-test CTest **103/103**.

## address-error-slot — VERIFIED 2026-09-13 (same audit slice)

- Severity: medium, user-visible, pre-existing on `main`. The address-error line was painted at y=66..80 in a 72-DIP chrome (clipped to a few pixels), and the opaque native address edit covered the whole location row, so committing an invalid path produced no readable error.
- Fix: `ffui::ChromeLocationRow` + `ComputeChromeLocationRow()` (ChromeLayout.h) split the location row into the editable field and a right-aligned error slot capped at half the row; the chrome reserves the slot (shrinking the edit), tints the field frame with `theme.error`, and draws the message inside the slot. Row rebuilds on `BeginAddressEdit`/`CommitAddressEdit`/`EndAddressEdit`.
- Tests: `TestLocationRowErrorSlot` in `ffchrome_layout_tests` (disjoint, right-aligned, shared band, half-row cap, degenerate widths).
- Ground truth (125% DPI, invalid path committed through the real address bar): edit shrank 867→627 px (exactly the 240 px slot); slot ink profile y=99..111 dominated by the error reds. `build/probe-address-error.ps1`, capture `build/visual/probe-address-error-print.png`.
- Validation: Debug CTest **103/103**, release-test CTest **103/103**.
- NEXT EXACT ACTION: continue the same layout/geometry audit surface (details pane, sidebar, search panel bands).

## breadcrumb-band-paint-fix — VERIFIED 2026-09-13 (autonomous audit slice)

- Severity: high, user-visible. The chrome-layout unification (`6803650`, on this branch only) computed the breadcrumb band's vertical extent from the search control's **width** (`chromeLayout.search.size` = 200/240 DIP) instead of its row height (28 DIP). `DrawTextSegment` centres with `DT_VCENTER` inside the segment rect, so breadcrumb text was centred at y≈140–175 inside a 72-DIP chrome and clipped: the location path was invisible at Normal and Wide widths. The same expression also stretched the address click target past the toolbar row.
- Root cause fixed at the seam: new `ffui::ChromeBand` + `ChromeLocationBand()` in `src/ui/include/ffui/ChromeLayout.h` is now the single location-band definition; both the painter (`NavigationChrome::DrawBreadcrumbs`) and the `WM_LBUTTONDOWN` address hit test consume it. Sibling audit: `ChromeRect::size` is consumed only by `NavigationChrome` (3 uses); the overflow-button use is a legitimate width and is unchanged.
- Regression coverage: `ffchrome_layout_tests` gained `TestLocationBandInsideChrome` (band height is the command-row height at Narrow/Normal/Wide, ends with the toolbar controls, never exceeds `chromeHeight`, and a wide search field never stretches it) plus vertical-composition invariants in `TestCompositionInvariants` (band + every command/overflow rect inside `chromeHeight` for widths 320–2200 and 0–36 tabs). This was the missing invariant class.
- Empirical proof (no vision available this session; pixel analysis used): `build/probe-breadcrumb.ps1` drives the diagnostic build at 125% DPI to a deep fixture path, dumps real child-window geometry, PrintWindow-captures, and profiles ink per row in the address band. Before: zero ink in the toolbar band (only the tab pill edge registered). After: text-like ink profile at chrome y=61..73; Back/Forward/Up, tab+close, New Tab, Settings, and Fast Search each independently ink-verified. Capture `build/visual/probe-breadcrumb-print.png`.
- Validation: Debug CTest **103/103**, release-test CTest **103/103**; `ffchrome_layout_tests` green.
- NEXT EXACT ACTION: continue the audit (the same era of layout unification is the highest-risk surface); then run `pwsh ./verify/verify.ps1 build` on the committed tip.

## overhaul-hardening-campaign — VERIFIED 2026-09-12 (autonomous slice on `feat/refero-frontend-overhaul`)

- Cut-ghost feedback now repaints itself: `ColumnView::SetCutPaths` returns whether the effective set changed (pure `CutPathSetChanged` seam + new assertions in `ffcolumn_hit_test_tests`) and `WindowShell::SyncCutGhost` schedules the repaint, so Ctrl+X/Ctrl+C ghosting no longer waits for an unrelated invalidation; the clipboard-sequence re-check on focus/file-op completion still clears it.
- Expired status/operation notices repaint promptly: the existing 500 ms workspace-save timer clears an expired notice and requests one repaint (previously the text could linger until the next unrelated invalidation).
- Command palette no longer lists the legacy `navigation.toggle-column-view` alias as a second “Toggle Dual Pane” row: `CommandDescriptor::paletteHidden` keeps it resolvable/invocable for persisted shortcut bindings while `SearchCommands` skips it. `ffcommand_tests` pins both halves.
- Visual catalog hardening (`verify/visual/Invoke-VisualCatalog.ps1`): the `search-no-results` scenario had closed the search workspace before looking for its query edit, so it skipped on every run — ordering fixed and the hardcoded `Pass=$true` replaced with a checked assertion (query applied + `Search.Results` present with zero rows). The `dual-pane` pane check now asserts the pane group's direct children (`Pane.0`/`Pane.1`) instead of a full-tree DFS for `Pane.1`, which reliably times out once both panes of columns are loaded even though the tree is correct.
- Catalog result: **16/16 PASS, 0 skipped, 0 failed**; the committed-tree rerun binds `exactTreeBound=True` to commit `3945872` / tree `220f80a` (`build/vis-final-committed`), superseding the dirty-provenance run `build/vis-campaign3` on `40b16de`. The previous skip is closed and dual-pane is deterministic.
- Documentation accuracy: `CODE_INDEX.md` now maps the public `src/ui/include/ffui/` headless seam headers and the shared `UiStatePalette.h`; `ffdoc_drift_tests` green (line endings normalized after edits).
- Validation on the exact working tree: Debug CTest **103/103**, release-test CTest **103/103**; focused `ffcommand_tests`, `ffcolumn_hit_test_tests`, `ffdoc_drift_tests` green; live probe confirms one canonical palette row (`rows=[Toggle Dual Pane | Toggle Details Pane]`).
- Findings deliberately not productized: the palette matcher's single-typo tolerance makes “dual pane” also list “Toggle Details Pane” (documented behavior, not a defect); the driver's full-tree DFS remains O(window tree) — scenario-scoped assertions now avoid it rather than changing the shared driver.
- NEXT EXACT ACTION: run `pwsh ./verify/verify.ps1 build` on the committed tree and record the result; then resume the external frontier (GitHub Actions billing block, palette live screenshot, 125/150/200% DPI + High-Contrast captures).

## refero-frontend-overhaul — ACTIVE 2026-09-12 (branch pushed; all UI phases 1–12 advanced)

- Follow-up verification pass (same day, tip `4101e0b`): Debug CTest **103/103** and release-test CTest **103/103** re-run green on the exact tip (docs-only delta from `4b7ef8b`); `ffdoc_drift_tests` green after this record. The earlier session screenshots were captured with a `debug-diagnostic` binary that predated the final SettingsDialog/ColumnView/WindowShell edits, so a compact matrix was re-captured from the rebuilt binary (`build/visual/recheck-*.png`): home, longnames (ellipsis), empty state, dark browse, settings (dark, UIA-opened), gallery, collapsed rail — all visually verified correct.
- New ground-truth evidence: dual-pane and palette were driven through the real UIA command surface on the rebuilt diagnostic binary. Dual-pane now has an exact-tree **PrintWindow** capture (`build/visual/verify-dual-pane-print.png`) proving two real pane roots (Pane.0/Pane.1) plus a rendered split; this closes the dual-pane visual gap that the catalog DWM grab could not (stale frame).
- Palette visual screenshot remains **external**: the palette opens (edit 6301 focused, commands dispatch and close), but its layered dimmer plus this host's color-shifted/stale screen-capture path makes a trustworthy palette image impossible; `build/visual/verify-command-palette-{print,screen}.png` document the artifact. Settings "missing preview controls" investigated: the Win32 control dump proves the checkbox/edit/hint exist, are visible, and have correct geometry — the earlier pixels were a PrintWindow artifact, not a defect.
- **External blocker discovered**: GitHub Actions cannot start any job for this repository — hosted runs (PR #8 and `main` alike) fail instantly with `The job was not started because recent account payments have failed or your spending limit needs to be increased`. So branch/PR CI evidence is unavailable until the account billing block is cleared; local CTest gates above are the current authority. Do not fabricate hosted-green.
- Branch `feat/refero-frontend-overhaul` (PR #8), forked from `main` `e895c66`; nine slices pushed: `6803650` (chrome layout policy + details hierarchy + shell-layout test alignment), `507349c` (treemap empty state, dark checkboxes, two-line recents), `3f0aa13` (component gallery expansion + no details overlay), `327b725` (deep-startup leaf reveal + ellipsis trimming), `0514207` (content-sized engine chip + interactive details rail), `160cbf5` (palette shortcut reservation + no-match row), `eeaea38` (shared state palette + operation feedback), `96f3f2a` (settings header/helper grammar), `4b7ef8b` (cut-item ghosting). HEAD == origin.
- Phase 1 (shell/navigation): pure `ffui/ChromeLayout.h` (Narrow/Normal/Wide; 28-DIP command family; location/search reservation; 96-DIP tab floor + windowed overflow + tab-list popup) consumed by `NavigationChrome`; `ffchrome_layout_tests` pins it.
- Phase 3: deep startup paths reveal their leaf column; row/details labels trim with `CreateEllipsisTrimmingSign`; cut items ghost (dimmed icon/label) until paste/external clip change, with a pure join/match seam in `ffcolumn_hit_test_tests`.
- Phase 4/7/9/10: Home recents are name+path cards; details is a hierarchy (title, summary, high-value rows, wrapped path, quiet caption, deliberate empty/multi states); treemap zero-size scopes show centered state text; settings rows carry section headers + muted helper text and dark-mode checkboxes.
- Phase 5/6/8/11/12: search states verified live (idle, results with match highlighting and accent selection, no-match with query echo + degraded note); palette reserves the shortcut column and shows an inert no-match row; operation queued/active/progress/completion/cancellation/failure ride the title plus a non-modal status notice, with an indeterminate band before the first percentage; browse/sidebar/search share one semantic state palette (`src/ui/src/UiStatePalette.h`, `ffui_state_palette_tests`); gallery exposes 10 sections and is excluded from the details overlay.
- Validation: **Debug CTest 103/103 and release-test CTest 103/103 on the final tip `4b7ef8b`, re-confirmed on `4101e0b`**; catalog run `build/vis-final/` binds 15 PASS + 1 skipped (`search-no-results` edit not discoverable) to commit `4b7ef8b`, tree `91ff7c9`; `ffdoc_drift_tests` green.
- Evidence (untracked): `build/visual/*.png` PrintWindow captures cover light/dark, 620/800/900/1280/1600 widths, Home (first-run/populated), browse, long names, empty and access-denied folders, details open/rail, search idle/results/no-results, storage overview/treemap, settings (light/dark), gallery, and the collapsed sidebar rail; `build/visual/recheck-*` and `verify-*` are this verification pass's bound captures. `build/probe-sandbox/fixtures/{empty,longnames,deep}` supply the pathological fixtures.
- Environment limits (corrected 2026-09-13): the host is multi-monitor with one display at 125% scaling (`HKCU\Control Panel\Desktop\PerMonitorSettings` DpiValue=1; the app window measures 120 DPI via `GetDpiForWindow`), so 125% DPI is **covered** by live probes and captures rather than external. What remains external is 100%/150%/200% DPI and High Contrast (the session's pixel probes and PrintWindow captures all ran at 125%). Keyboard chords/palette still cannot produce a trustworthy screenshot (layered dimmer + host screen-grab color shift/staleness); `SetForegroundWindow` works locally via the UIA driver, but DWM/screen frames are unreliable, so PrintWindow is the ground-truth capture path (kill the tree with `taskkill /T` or the engine holds `FastFiles.Ui.Ctrl.1` and breaks `ffengine_client_handshake_tests`). Process lesson: never launch the app or a UIA probe while `verify.ps1 build` is running — a concurrent launch made the gate report 8/9 `ASSERTION_FAILURE` once (it passed 9/9 on the same commit with no concurrent activity).
- NEXT EXACT ACTION: no known remaining local UI gap for phases 1–12; hosted CI is blocked by the GitHub Actions billing failure (external — clear it, then confirm the branch tip runs green); remaining external evidence is a live command-palette screenshot and 125/150/200% DPI + High-Contrast captures.

## runtime-certification-and-verifier-convergence — CLOSED 2026-09-06 (hosted CI green on final tip)

- Local closure executed on an interactive Windows 11 workstation (VS BuildTools 18, pwsh, live desktop): measured capability budget (1.3), seeded repeatability matrix (unseeded/1234 all, 777 search, 4242 isolated — one execution per seed, no retry-until-green, 8.2), full supported UIA PASS 23/6-skips/0-fail (8.3), live interrupted-worker probe, and unsigned release-candidate qualification at the clean tree `e47c743` (0 failures; hosted-CI path `Test-ReleaseCandidate.ps1`).
- The interrupted-run probe exposed a real harness escape: CIM can report product-process `ExecutablePath` with an extended-length `\\?\` prefix, silently defeating the build-root prefix filter so an owned process escaped preflight/reaper tracking. Fixed at the owning seam (`Get-VerificationNormalizedExecutablePath` in `verify/core/CapabilityRunner.psm1`, applied to snapshot + identity comparison; volume-GUID paths preserved) with live regression fixtures in `verify/core/tests/Test-ProcessReaper.ps1` (registered as `ffverify_process_reaper_ps_tests`).
- Validation ladder: Debug CTest 101/101 (one `-j8` `ffpreview_tests` timing flake observed, passed standalone and on full re-run), release-test 101/101, verifier PS fixtures green, post-fix live UIA smoke PASS (`20260905-180845`), strict OpenSpec 82/82, project-status/lifecycle integrity PASS.
- Truth surface: lifecycle entry `runtime-certification-and-verifier-convergence` -> `completed` with hosted binding run `33983389425` on `b6a04e649984355d58a9014f925a0504e16f9722` (SUCCESS 6/6); closure tip `fc9da051388357a9d402482abe631c7af76c969d` verified live by hosted run `33984985055` (push/main/`fc9da051388357a9d402482abe631c7af76c969d`, SUCCESS 6/6); the now-false `BLOCKED_ENVIRONMENT`/`REQUIRED-BUT-UNAVAILABLE` overrides for 1.3/5.3/7.2/8.2/8.3 removed (tasks executed with local+hosted evidence). Evidence ledger: `openspec/changes/runtime-certification-and-verifier-convergence/evidence/budget-matrix-and-final-closure-20260905.md`.
- Completion-integrity 4.5.x/6.15 remain honestly unchecked: the continuous foreground journey including preview/properties/file-operation steps is not covered by the UIA scenario registry, and the remaining clauses are display/clean-host/elevated-bound. All other repository unchecked tasks are `[external]` or OWNER-EXCLUDED.
- NEXT EXACT ACTION: no deterministic local work remains. External frontier unchanged (production signing/pins, elevated MFT/USN, clean-host matrix, physical-display matrix) — execute only on authorized capability availability per owner directive.
## index-durability-false-success-closure — COMPLETE 2026-09-05 (hosted CI green on final tip)

- Closure tip: `d7217327ba2f454cce7c66eb65eb0aa8e3a46bc3` (5 wave commits: protocol+service `426b6b0`, engine completion `9d5d675`, reconciliation `b4a48cc`, store verdict `e4aed46`, campaign record `d721732`), pushed normally; HEAD == origin/main.
- Hosted CI on the closure tip verified live: run `33941775599` (push/main/`d7217327ba2f454cce7c66eb65eb0aa8e3a46bc3`) completed SUCCESS 6/6 (debug, release-test, analyze, asan-protocol-ipc-lifecycle, repository-hygiene-and-contracts, unsigned-release-candidate-qualification). Lifecycle record now binds this run; entry `completed`, tasks 1.1–7.5 all checked.
- Local validation (pre-push): Debug CTest 100/100 ×2 consecutive, release-test 100/100, `/analyze` warning-clean, ASAN 4/4, OpenSpec strict 82/82.
- NEXT EXACT ACTION: campaign closed. Resume the privileged production-validation campaign only on explicit owner request for a production signed release.

- Provenance: `docs/agent/POST_CERTIFICATION_PRODUCT_AUDIT_2026-09-05.md` (PA-001..PA-006); OpenSpec change `openspec/changes/index-durability-false-success-closure/` (proposal/design/spec/tasks, D1–D6); lifecycle entry `completed` in `openspec/lifecycle.json`.
- Implementation COMPLETE: protocol `ScanFailed = 22` + `{2,2}` gate; service `SendScanTerminal` + EOF-vs-fault enumeration truth; engine completion trust model (stale/paused/batch-failed terminals non-authoritative, `OnScanFailed` never marks); reconciliation identity (`AbortReconciliationPass`, deferred-safe `TriggerReconciliation`, USN seen-set, journal-failure reconciliation request); store `IntegrityVerdict` (prepare-failure classification via `sqlite3_extended_errcode`) + fail-closed open without deletion license on inconclusive; `Main.cpp` bounded retry (3 attempts/500 ms, delete only on Corrupt); 42–46 diagnostics.
- Tests: 5 VSM + 3 pipeline + 3 store + protocol gates + command-surface rejection + new `ffvolumescanner_terminal_tests` (legacy `{2,1}` fallback proof). Fixture lessons: WAL readers ignore `BEGIN EXCLUSIVE` (use `locking_mode=EXCLUSIVE` + active read); page-1 b-tree damage fails integrity-check *prepare* (classify the connection code); pure-garbage bytes read as empty DB; `/analyze` C6326 on constant enum compare (volatile reads); `#define private public` cannot call cross-TU private methods on MSVC (mangled `Q` vs `A`).
## Current authoritative state — 2026-09-05 (final provenance + documentation-truth closure COMPLETE)

Terminal classification: `CERTIFICATION READINESS COMPLETE — EXTERNAL EXECUTION PENDING`.

## NTFS observer + visual-matrix closure + soak-gate repair campaign — COMPLETE 2026-09-04 (tip CI since verified green)

- Closure tip: `630960bec2051649fe511d02eebb133b846e41ad` (commit A: exact-tree evidence + docs scrub + guard), pushed normally; HEAD == origin/main. Primary worktree still dirty ONLY with the 2 pre-existing unrelated `.opencode/package.json[.lock]` edits (preserved untouched: not staged, not committed, not reverted, not overwritten).
- Hosted CI on the closure tip verified live: run `33932827458` (push/main/`630960bec2051649fe511d02eebb133b846e41ad`) completed SUCCESS 6/6 (debug, release-test, analyze, asan-protocol-ipc-lifecycle, repository-hygiene-and-contracts, unsigned-release-candidate-qualification). Lifecycle record now binds this run. Prior tip run `33899848739`/`5dbe234` and implementation run `33897389042`/`227902eaecfdcaccd733d328ec6e63bb56c57d82` retained below as history.
- Campaign `external-certification-execution-engine-and-frontier-truthfulness`: `completed` in `openspec/lifecycle.json`, all tasks checked. No active planner prompt (`.agent/EXECUTION_PROMPT.md` ARCHIVED/FROZEN) — this session runs only the narrow terminal closure: exact-tree evidence, docs scrub + guard, WORKSTATE convergence, final-tip CI.
- Defect ledger: `docs/agent/NTFS_VISUAL_CERTIFICATION_DEFECT_LEDGER_2026-09-04.md` (NV-001..NV-011 + FC resolutions, all forensically established against the live tree).
- NTFS observer (FC-003/FC-004, NV-002/005/006): `fftest ntfs-observe` (semantics/find/usn via production ioctls+parsers, read-only, 0 mutations) + runner semantics checks + observer-backed fixture confirmation + `mft/usn-observer-semantics` vocabulary (schema/manifest/frontier 4.6/4.7). Live non-admin proof: real open attempted, error 5, BLOCKED exit 2. Commit `98375b0`.
- Visual EX-009 (FC-007, NV-007): treemap degraded fallback + totals recompute + partial-known gating + matrix scope warmup + `-Preset debug-diagnostic`. Live: visual-matrix 2/2 x 14/14 PASS (10/20/30/40, drill 100%). Frontier 5.1 -> READY_EXTERNAL. Commit `343d22e`.
- Soak gate (NV-011): settled-step allowance + Test 7 warmup; 600 s interactive soak PASS (12/12, drift=2, graceful, zero orphans); 20-cycle + 45-cycle probes prove bounded caches, no leak. Commit `a8662a0`.
- FC-001: post-fix rapid-input 100/100 PASS (1100 assertions). FC-002/005: no vendor-specific VM-provider build per owner scope directive (fail-closed + declared credentials verified). NV-001/004/009 not reproduced; NV-003/010 no-change-needed/documented.
- Exact-tree evidence status: retained UIA/IPC/soak JSON artifacts record `exactTreeBound=true` but bind older implementation SHAs (e.g. `39b873a`, `1a29ef5`); the post-fix rapid-input 100/100 and interactive-soak 600 s passes live only in untracked run trees plus NV-010 dirty-tree prose. Authoritative regen COMPLETE in clean cert worktree `C:/ff-cert` (branch `cert/final-exact-tree` at `5dbe234`, `status --porcelain` empty throughout; tree `8f648afcbcd04a0a211c921fd168a37cc1d523c4`). Committed as `openspec/changes/external-certification-execution-engine-and-frontier-truthfulness/evidence/exact-tree-5dbe234/` (SUPERSESSION.md + manifest + sidecars; consistency `-RequireExactTree` PASS). Product `src/` and `verify/` are byte-identical between `5dbe234` and the closure tip; the only test-tree delta is the new drift-guard script, itself validated (Debug 99/99 re-verified in-primary on final content). Environment finding: a detached HEAD breaks `ffproject_status_ps_tests` (empty Branch parameter) — cert branch used instead; commit/tree binding unaffected.
- Docs scrub 2026-09-05: excluded vendor-specific VM technology name removed from all tracked `.md` prose (14 files across docs/openspec/.agent/CODE_INDEX/codebase-audit); implementation symbols (`.ps1`/`.psm1`/`.json` provider ids, adapter paths, executed commands incl. historical `external-frontier-summary.json`) intentionally preserved per scope. Durable guard `Test-ExcludedProviderTerminology` added to `tests/drift/check_doc_drift.ps1` (`ffdoc_drift_tests`); green after this rewrite. Prior broad `verify/README.md` rewrite attempts failed hosted CI (runs `33865833834`/`33865755337`/`33865780253`); this closure uses minimal token-level replacements only and validates all gates locally before push.
- Remaining frontier is external-only (elevated NTFS, physical display/DPI, external environment provider, production signing, clean-host).
- NEXT EXACT ACTION: No deterministic local certification engineering remains. Execute the retained external frontier only when the required authorized external capabilities become available.
- NOTE: entries below (including the IN PROGRESS FC-001..FC-009 header) are superseded checkpoint evidence; the head entry above is current.

## Final local certification closure (FC-001..FC-009) — IN PROGRESS 2026-09-04

Current campaign: `external-certification-execution-engine-and-frontier-truthfulness` — **ACTIVE**.

Owner scope directive: vendor-specific VM-provider execution and testing are explicitly **out of scope** for FastFiles local completion and future campaigns unless the owner explicitly reverses this directive. Generic provider lifecycle, provenance, cleanup, and fake-adapter contract tests may remain as repository infrastructure; they are not a completion gate for any specific VM provider.

### FC-001 focus race

- Root-caused and product-fixed in `b3dffc5a94ad09529016364dd5a6974d494742a6`.
- `CommandPalette::Show()` now verifies edit-focus acquisition via `EnsureEditFocus()`; `ffui/PaletteFocus.h` provides a pure readiness seam; harness retry counts were not increased.
- Regression target `ffpalette_focus_tests` added as the 98th Debug CTest target.
- Full Debug CTest: 98/98 PASS ×1.
- Post-fix authoritative rapid-input stress: CLOSED 2026-09-04 by the NTFS/visual/soak campaign -- rapid-input 100/100 PASS (1100/1100 assertions) on the palette-fix tree; see the campaign head entry above and `docs/agent/NTFS_VISUAL_CERTIFICATION_DEFECT_LEDGER_2026-09-04.md` (FC-001).

### Remaining LOCAL work

1. ~~Run post-fix rapid-input 100× minimum~~ DONE 2026-09-04: 100/100 PASS (see campaign head entry; FC-001 closed).
2. ~~Implement real default MFT and USN observation backends~~ DONE 2026-09-04: `fftest ntfs-observe` + runner wiring (commit `98375b0`; see ledger NV-002).
3. ~~Resolve visual-matrix task 5.1~~ DONE 2026-09-04: degraded treemap fallback + scope warmup + diagnostic preset; matrix 2/2 x 14/14 PASS; frontier 5.1 READY_EXTERNAL (see ledger NV-007).
4. ~~Complete the validation ladder~~ DONE 2026-09-04: Debug 99/99 x3, release-test 99/99 x2, Analyze, ASAN 6/6, cert suites 10/10, IPC 100/100/25, UIA 100/100, soak 600 s PASS, OpenSpec 81/81, markers/policy green.
5. Complete campaign tasks 6.3/7.3, reconcile lifecycle/current documentation, push normally, and bind successful hosted CI on the exact final tip. (7.1/7.2 checked; 6.3/7.3 close with CI.)

### Explicitly excluded from local completion

- Vendor-specific VM-provider guest execution/testing.
- Provider-specific guest images, guest credentials, guest-channel execution, and provider-specific rollback qualification.

Do not reintroduce those items into the remaining-local-work list, validation ladder, or future FastFiles campaign prompts unless the owner explicitly changes scope.

### Genuinely external prerequisites still relevant to in-scope certification

- production signer/pins;
- elevated disposable NTFS environment for real privileged execution;
- clean disposable host where required;
- physical DPI/contrast/display matrix;
- real vision provider if still required by the visual-review lane.

### Historical state

Prior campaign reports and historical evidence remain available in their OpenSpec change directories and Git history. They are historical records, not current instructions. When historical prose disagrees with this section or generated project status, current lifecycle/project-status truth wins.

## Historical hosted-run bindings (lifecycle record corroboration)

Retained because `openspec/lifecycle.json` hosted-run records point at
this file and the project-status gate requires the run ID and exact SHA
to appear in the record text:

- `final-product-completion-and-production-readiness`: hosted run
  `33294518606` on exact SHA
  `0ad79a8caeab30f6c129c0f285279d1508b62c48` (6/6 SUCCESS); ledger tip
  `94a4830cb799a459844e7af421257fd525b82116` (hosted run
  `33295510209`, 6/6 SUCCESS).
- `completion-integrity-hardening-and-release-certification`: hosted run
  `33294518606` on exact SHA
  `0ad79a8caeab30f6c129c0f285279d1508b62c48` (6/6 SUCCESS).
- `external-certification-execution-engine-and-frontier-truthfulness`
  exact-tree closure: hosted run `33932827458` on exact SHA
  `630960bec2051649fe511d02eebb133b846e41ad` (6/6 SUCCESS).
  Supersedes tip run `33899848739` on
  `5dbe234248f630e58aecc621ac3c7a6cacb8e449` (6/6 SUCCESS) and
  implementation run `33897389042` on
  `227902eaecfdcaccd733d328ec6e63bb56c57d82` (6/6 SUCCESS).

## Next exact action

No deterministic local certification engineering remains. Execute the retained external frontier only when the required authorized external capabilities become available. Vendor-specific VM-provider testing remains excluded by owner directive.
