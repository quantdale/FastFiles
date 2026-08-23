# Fast Files — Agent Workstate

Last updated: 2026-08-23 (Phase-7 certification campaign; current live checkpoint)
Status: **Phase-7 certification in progress; local visual/UIA hardening, evidence, and executable gates are complete, repository handoff remains.** Live Git remains authoritative: campaign baseline is `90fa560a99217cabcf456b1198c83351c46513ed`; no campaign commit has been created yet.
NEXT EXACT ACTION: (1) inspect and stage only campaign-owned changes; (2) commit coherent slices and push `main`; (3) verify the exact pushed SHA, hosted CI query, and clean-tree handoff.

## Current 2026-08-23 Phase-7 certification checkpoint

- Reconciled at campaign start: branch `main`, local `HEAD == origin/main == 90fa560a99217cabcf456b1198c83351c46513ed`, clean before implementation. `gh run list` was unavailable because the local GitHub CLI session is unauthenticated; re-query for the final pushed SHA.
- Deterministic visual harness is implemented under `verify/visual/` and `verify/capabilities/windows-ui-visual-validation/`. It backs up/restores product state, uses a disposable mapped fixture drive, requires semantic UIA postconditions before capture, captures via DWM extended bounds + `CopyFromScreen`, rejects unexpected duplicate hashes, and writes manifest/Markdown evidence. The refreshed final run produced 53 rows: 26 PASS images, 2 SKIPPED, 25 BLOCKED_ENVIRONMENT, 0 FAIL; 3 Home images include recorded semantic privacy redactions; no personal path text was present in manifest/summary or committed visual evidence. The dark Storage Analysis row is included and passed.
- UIA flake audit: treemap root cause (missing `return` after a failed sub-navigation assertion) fixed; drag root cause (stale pre-toggle ColumnView provider and unchecked foreground/pane activation) fixed; selection root causes (unchecked foreground, stale/unsafe synthetic click sequence, `.txt` shell association contamination, and incorrect Pane.HasKeyboardFocus expectation when a row owns focus) fixed in the harness. Fail-fast `FailFast` wrappers retain per-attempt UIA tree/screenshot/log/foreground diagnostics and never retry by default. Treemap isolated/group runs pass repeatedly; drag group is PASS with documented Explorer-source and host-foreground skips; selection is PASS-or-explicit `SKIPPED/interactive-foreground-unavailable` when `browser-blackhole`/`clone-MC_v3` owns the desktop foreground, with no downstream assertion attempted.
- Local UI debt implemented: tokenized custom paint for Storage Analysis native radio buttons while preserving native radio semantics, keyboard behavior, UIA RadioButton/SelectionItem providers, High Contrast fallbacks, and checked/focus/disabled states; display-only 8.3/long-path normalization via `ffui::NormalizeDisplayPath` across navigation chrome, tabs, Home, UIA, Search, sidebar, and details while canonical operational paths remain unchanged. Focused `ffstorage_radio_state_tests` and `ffdisplay_path_tests` passed on the diagnostic build; Debug and diagnostic FastFiles builds passed after the changes.
- Repository gates after the evidence refresh: Debug CTest `73/73 PASS`; release-test CTest `73/73 PASS`; Analyze build completed warning-clean; ASAN `ffprotocol_fuzz_tests` `1/1 PASS`; source markers and diagnostic policy passed; visual-capture contract passed; strict OpenSpec validation passed `59/59`. The repository has no standalone `verify/check_doc_drift.ps1`; the `ffdoc_drift_tests` CTest passed in both full configurations.
- Current OpenSpec checkbox truth (live `tasks.md` audit): complete: `allocated-size-storage-accuracy` 36/36, `indexed-search-allocated-size` 74/74, `semantic-treemap-uia` 40/40, plus all previously closed changes; genuinely open: `autonomous-runtime-verification` 2 external/provider tasks, `close-independent-validation-gaps` 6 external/visual-validation tasks, `real-user-dogfooding-release-engineering` 1 final push/CI task, and `zero-touch-autonomous-engineering` 3 external/provider tasks. Do not claim production signer, signed privileged runtime, clean-host MFT/USN, or usable Windows guest/provider evidence.
- Campaign-owned untracked UIA repeat artifacts include raw failure screenshots that may contain the interactive desktop; they must remain local/ignored execution evidence or be removed before final clean handoff. Only sanitized visual-catalog evidence may be committed.

## What the interrupted 2026-08-22 session actually left behind

- `d1680df` committed the DropFeedbackLayout seam WITH build+ctest verification (70/70 at that time) — the old "build-pending" caveat was already resolved before this campaign.
- `c789f10` → `d43201a` → `5c905b9`: search button UIA semantics (real Button control type, Invoke, keyboard traversal, ffsearch_tab_order_tests) and the address-bar Ctrl+L retry hardening. These are CURRENT functionality; do not redo them.
- `91390e7` added 22 PNG captures under `codebase-audit/sessions/2026-08-22-ui-v3-final/visual/` and nothing else. Audit findings about that evidence set:
  - Three duplicate-image groups (`02=03=04`, `12=13`, `15=16` — byte-identical blobs): the state transitions silently failed and the script captured anyway. Those files are INVALID evidence for their labeled states.
  - Numbering gaps (07 conflict dialog, 19/20 drop feedback planned but skipped); `18-wide` resize failed; several labels mismatched actual content (01 "home" showed restored workspace; personal paths leaked into committed artifacts contrary to the screenshot policy).
  - Root cause of the silent duplicates: the capture harness had NO post-condition assertions and swallowed failures with try/catch.

## Defects found by this campaign's visual audit — ALL FIXED AND LIVE-VERIFIED

Evidence method note: the earlier "white combos/buttons" readings were small-screenshot misreads (zoomed crops + pixel sampling corrected the record). Pixel sampling (GetPixel) plus forced-redraw probes were used as ground truth throughout.

1. **V3-01 (P1) overlay bleed-through**: Search/Storage surfaces rendered ColumnView beneath inset native child controls; every gutter showed stale browse pixels (left icon strips, heading ghosts, right letter slivers, bottom yellow strips). Fix: `WindowShell::Render` clears to theme background instead of rendering columns when Search/Storage is visible (treemap branch unchanged).
2. **V3-02 (P1) system-white results header in dark**: design intent was to hide the ListView header, but a bare `SW_HIDE` raced the list's first layout and re-showed. Fix: `LVS_NOCOLUMNHEADER` at creation (column widths still drive scroll extent). An interim themed NM_CUSTOMDRAW implementation proved the mechanism live and was removed as dead code once the style fix landed.
3. **V3-03 (P1) stale viewport after navigation**: `NavigateToPath/Hierarchy/ComputerRoot` rebuilt the column chain but never touched horizontal scroll; the leaf stayed off-screen right (captures showed C:\ children while breadcrumb claimed a deep path). Fix: `EnsureColumnVisible(FocusedColumnIndex(), NavigationViewportWidth())` after every navigation mutation site (NavigateToLocation, NavigateColumnViewTo, NavigateWorkspace selectName branch, RestoreActiveTabViewState fallbacks).
4. **V3-04 (P1) Home recents text collision**: home primary text drew raw `DrawTextW` with a NO_WRAP format lacking trimming; long recent-folder paths painted across grid columns. Fix: `DrawHomeText` routes through the existing bounded character-ellipsis `DrawRowLabel`.
5. **V3-06 (P2) storage toolbar label clipping**: compact packing used a 44-DIP minimum button width; native radio labels have no ellipsis ("Largest Fol…"). Fix: `kLabelFitButtonWidth=118` in the headless seam (two-row wrap when needed); status static gets `SS_ENDELLIPSIS | SS_LEFTNOWORDWRAP`; test updated to assert the wrap + label-fit invariant.
6. **V3-07 (P2) treemap blank canvas**: empty layout returned silently. Fix: deliberate state surface ("Calculating folder sizes…" during scope load, flipped to "No sized data for this scope." on completed-empty aggregate) via `TreemapView::SetEmptyStateText`.
7. **Storage dark mode (same defect class, found during V3-06 verification)**: overview/drill-down ListViews kept system-white backgrounds because the lazily-initialized panel never received the startup theme. Fix: `SetDarkTheme(gUiDarkTheme)` at Initialize end; ListView colors themed; headers themed via NM_CUSTOMDRAW (kept here because these data tables need headers, unlike search).
8. **Query-field white flash**: keystroke-driven edits repainted natively and left stale light frames over the custom paint until an unrelated repaint. Fix: SearchEditProc invalidates after WM_KEYDOWN/WM_CHAR. Verified by forced-redraw probe (before: white; after RDW_UPDATENOW: correct dark).

Verification evidence (this host, diagnostic build): zoomed crops + GetPixel samples show dark field/header/body where white stood before; deep navigate reveals the leaf column; Home recents trim without overlap; storage toolbar wraps with full labels; idle probe: UI 0 ms CPU over 20 s (no animation timer while idle), handles/threads/WS flat (1171/30/61.1 MB), engine ~0.07 %.

## Prior 2026-08-22 gates (historical checkpoint)

- Debug build warning-clean; Debug ctest **71/71 PASS**; release-test build + ctest **71/71 PASS**; analyze `/analyze` build clean.
- OpenSpec strict validation **59/59 PASS**; source-marker PASS (Failures: 0); diagnostic policy PASS; doc-drift/line-endings PASS (after CRLF normalization of edited files); `git diff --check` PASS.
- Full `windows-ui-automation-validation` capability via `verify.ps1 run -Capability windows-ui-automation-validation`: **status=PASS** (run tree ad-hoc `20260822-211341`). Honesty note: an earlier same-session invocation returned `scenario-failed`; the immediate rerun passed fully. Consistent with the registry's documented geometry-flaky scenarios (treemap-readability-click-precision, selection-model-interactions, drag-between-panes are Flaky=true); NOT counted as clean-without-caveat — flagged for a repro attempt in the next slice.

## Prior capture-harness requirements (catalog now rebuilt in current checkpoint)

The mandatory catalog (docs/ui-improvement/05 §1) is still only partially covered. Rebuild rules learned here:
- Reset `%LOCALAPPDATA%\FastFiles\workspace-state.json` (back it up first) so captures start deterministic; never rely on restored personal tabs.
- Use sanitized fixtures under `%TEMP%\ffviz-*` (long names for trim/reflow coverage); never navigate personal folders in captured states.
- Screen-region CopyFromScreen of DWMWA_EXTENDED_FRAME_BOUNDS is the capture method (PrintWindow matched it everywhere tested, but region capture is ground truth by construction).
- ASSERT post-conditions via UIA (element exists/state changed) BEFORE capturing; fail loudly instead of capturing a duplicate.
- Dedupe guard: compare blob hashes across the batch; abort on unexpected duplicates.
- Known-good chords: Ctrl+F opens search (NOT Ctrl+Shift+F); palette flow Ctrl+Shift+P → AutomationId 6301 → query → Enter works ('Home'/'Analyze' commands verified); typing "Home" into Ctrl+L produces the honest red validation banner instead of navigation.
- Host boundaries (unchanged): single 1920x1080 @96 DPI monitor means live monitor-to-monitor PerMonitorV2 transition and live 125/150/200 percent scale matrix remain BLOCKED_EXTERNAL (headless coverage: TestFullClientCoverageInvariant at 1.0/1.25/1.5/2.0 x details x panes); High Contrast toggle not exercised this session (invasive host setting; code paths HC-gated and unit-covered elsewhere).
- Animations-off live capture NOT EXERCISED: SystemParametersInfo(SPI_SETCLIENTAREAANIMATION=0x1043) reports success but readback stays enabled from this session context - do not retry blindly; the product-side gate (SPI_GETCLIENTAREAANIMATION via UiAnimation, snap-instant when disabled) is unit-covered instead.
- HOST-SETTING SAFETY LESSON (2026-08-23): an SPI probe initially used WRONG constants (0x4A/0x4B are FONT SMOOTHING, not client-area animation which is 0x1042/0x1043), turning ClearType off host-wide. Recovery: registry FontSmoothing=2 plus SystemParametersInfoW(SPI_SETFONTSMOOTHING with uiParam=1 and pvParam=NULL, f=3) - the ref-BOOL variant does not stick in this session context. Final verified state: fontSmoothing=1, clientAreaAnimation=1, registry=2. Always read back after any host-setting probe and restore immediately.

## Prior next recommended slices (historical checkpoint)

1. Rebuild the full visual-state catalog per the rules above (largest open Phase-7 item; mostly mechanical now that the harness contract is written down).
2. Reproduce-and-bound the intermittent UIA scenario failure noted above (flaky-test policy: root cause or documented bound, never retry-as-resolution).
3. Native radio buttons/storage toolbar remain system-styled in dark mode (P3 debt class shared with other Win32 chrome); owner-draw radios would close the last system-light controls in the storage surface.
4. Breadcrumb/status display still shows 8.3 short names (MICHAEL~1) when navigated via short-path environments — display-side normalization candidate (P3).
5. OpenSpec changes with open tasks (per tasks.md checkbox counts, verify before implementing): close-independent-validation-gaps, allocated-size-storage-accuracy, autonomous-runtime-verification (external/provider), indexed-search-allocated-size, semantic-treemap-uia, zero-touch-autonomous-engineering.

Historical records below are retained unchanged for provenance.

---

## Shell-outage forensics + TreemapView decision (2026-08-22, ox-alpha) [SUPERSEDED STATUS — outage resolved; content retained]

- Outage forensics (all read-only, no shell available): server log shows bash permission evaluations occurred normally on 2026-08-20 but ZERO on 08-21/08-22 while sessions kept streaming -> failure sits between parse and permission ask inside the shell tool execute path; tool factory still logs `shell tool using shell` fine. `shell.ts` is byte-identical between tags v1.18.19 and v1.18.21 -> provisioning/deps regression, not the tool. Node `path.resolve` semantics confirm the exact message only when an argument is undefined. Failed workarounds (documented so nobody retries): disabling plugins mid-session (no effect), DEBUG logLevel (needs restart), custom-tool shim hot-load in BOTH `~/.config/opencode/tool/` and project `.opencode/tool/` (registry is boot-time; fresh subagent sessions do not re-scan).
- Product decision RECORDED (closes UI-v3 audit item): treemap exposes exactly base tile + hover lift; NO selected/pressed/keyboard-focus state by design - activation always navigates away (drill-in-place or Column View with file selected per spec), hover already carries full canonical metadata, keyboard traversal would need its own spec delta. Full rationale appended as `semantic-treemap-uia/design.md` section 21. No code change required.

## UI-v3 mass implementation campaign (2026-08-22) - historical checkpoint

- Branch consolidation merge-base `1d4fb45`; foundation `b984be0`; waves A/…/settings `660a8d6`, `01136ef`, `2e2db34`, `da04799`, `2b40be6`, `6f31e2c`, `19bd47a`, `a8b135f`. All waves built clean under /W4 /WX before commit.
- Deferred-by-design items were later closed: DrawDropTargetFeedback headless seam + test (`d1680df`), OleDragDrop custom drop-target visuals (`19bd47a`), TreemapView state question (design.md section 21).

## Real-user dogfooding and release-engineering campaign (2026-08-21) — historical

- Fixture/acceptance wave, release candidate wave, UIA wave, measurement wave all complete as recorded below; RLS-001 and UX-001 resolved; final gates Debug/release-test 69/69 each at that time, /analyze clean, ASan fuzz PASS, strict OpenSpec PASS, final all-scenario UIA acceptance PASS.

## Final production-readiness closure pass (2026-08-21) — historical

- Bottom-edge strip root-caused (DWM invisible borders + WM_DPICHANGED client sizing), fixed, and covered by TestFullClientCoverageInvariant; targeted/full UIA certification runs passed; external blockers intentionally unchanged (production signing pins, signed install, elevated clean-host MFT/USN matrix, guest/VM provisioning).
