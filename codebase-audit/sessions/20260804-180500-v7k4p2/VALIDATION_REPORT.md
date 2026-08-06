# Independent Change and Results Validation Report

**Session ID:** `20260804-180500-v7k4p2`
**Report directory:** `codebase-audit/sessions/20260804-180500-v7k4p2/`
**Validation date:** 2026-08-04
**Repository/commit:** `D:\Documents\tryPython\FastFiles` @ `216d931aae00657b2c57204a3cdea9721cad8d07` (branch `main`)
**Mode:** Read-only validation (no implementation files modified).

---

## 1. Executive Summary

The supplied summary is **mostly accurate at the code level but contains material inaccuracies in its reported results**. Every claimed *change* (Items 1–4, M4, L7, L10, L1) exists in the committed history and was verified against the actual code. The build and tests were **independently reproduced** from a clean configuration. However:

- **The test result is misreported.** The addendum claims "`ctest` 26/26" and the intake-gate test is "test #26". Independent execution shows **27/27 tests pass** and `ffintake_gate_ps_tests` is **Test #27**. The "previously 25 tests" claim is also inaccurate (`ffuia_driver_ps_tests` #26 pre-existed).
- **The build claim is accurate.** A clean `/W4 /WX` build succeeds (exit 0, 159/159 steps, all executables linked, no warnings). This supersedes the prior opencode audit's "link-failure" result, which was an *environment* limitation (no VS developer shell), not a code defect.
- **Items 3 & 4 are accurate for the specific files, but leave a remaining contradiction.** `PrivilegeVerification.h` and `design.md` (and, additionally, `AGENTS.md`) were correctly updated (virtual account → LocalSystem; stubbed → implemented). But the *parallel* guidance files `CLAUDE.md` and `CODE_INDEX.md` still assert the superseded "virtual service account… never LocalSystem" model — a documentation contradiction the correction sweep missed.
- **The "deferred" UI list is partly stale — and one item (L9) is a real functional defect.** Static inspection shows L5 (metric consolidation) and likely M5 (dark-theme coverage) were *already addressed* by the broader `modernize-ui-appearance` change. But independent static inspection of **L9 ("% of Parent")** found a genuine calculation defect: the denominator is the sum of sibling *directories'* subtree sizes (excluding files and not the parent's total), so the column is mislabeled, files always show "—", and percentages don't sum to 100% when the parent contains files; the sort key also risks uint64 overflow for folders >~18 TB. **M3** is confirmed still applicable (the 300px right-anchored details card overlaps columns at narrow widths). These still require visual validation, but L9 is a functional defect, not merely a visual one.
- **No regressions were found** in build, tests, or the security invariants (closed protocol, connection-scoped handles — now unit-tested, placeholder pins fail-closed, scanner forwards no `$DATA`).
- **The working-tree claim is contradicted:** the addendum says changes were uncommitted; in fact the working tree is clean and all claimed changes are committed in `216d931`.
- **Outstanding external blockers (code-signing pins, non-elevated runtime, clean-host matrix, Windows UI validation) are all confirmed still open** — none marked resolved without evidence. The placeholder Authenticode pins remain all-zero and fail-closed.

**Current production-readiness conclusion:** Degraded-mode path — **Ready with Minor Concerns** (build/test health confirmed; minor doc/hygiene issues). Privileged-path — **Not Ready** (placeholder signature pins + pending operational validation, as documented). UI changes — **Requires Visual Validation**. Overall: the supplied changes are correct and safe to merge for the degraded-mode path, but the reported "26/26" must be corrected to "27/27", and `CLAUDE.md`/`CODE_INDEX.md` should be updated.

## 2. Repository State

- **Branch:** `main`
- **Commit:** `216d931aae00657b2c57204a3cdea9721cad8d07` ("refactor(ui): modernize appearance and harden renderer")
- **Working-tree state:** CLEAN for tracked files. `git status --porcelain=v2` → only `? all-edit-with-results-20260804-1748.md` (untracked user input). No staged/modified/deleted tracked files.
- **Staged state:** none.
- **Untracked files:** `all-edit-with-results-20260804-1748.md` (user-supplied merged input); plus build outputs under `build/debug/` (untracked, expected).
- **Diff baseline:** `HEAD~1..HEAD` (`e0968ee..216d931`), 76 files (+5065/−666).
- **Changed files:** see `CHANGE_INVENTORY.md`. All claimed changes are in this single commit. (`Audit.txt` +819 and `dist/FastFiles.exe` binary are unrelated in-commit artifacts.)

## 3. Previous Audit Artifacts Reviewed

| Session or Report | Repository Revision | Validation Method | Relevant Conclusions | Current Relevance |
| ----------------- | ------------------- | ----------------- | -------------------- | ----------------- |
| First `CODEBASE_AUDIT_REPORT.md` (in merged input) | `216d931` | Static only (no build/test) | 17 findings incl. CRITICAL-20260804-B3E8-001 (placeholder pins), HIGH-002 (degraded active), HIGH-003 (LocalSystem). Build/test NOT executed. | Superseded for build/test by this session. Security findings still valid. |
| `codebase-audit/CODEBASE_AUDIT_REPORT.md` (opencode) | `216d931` | Static + attempted build | Build **PARTIAL** (31 .obj ok, all failed at LINK — missing Windows SDK libs); tests **NOT EXECUTED**. 11+ INFO findings. | Build-failure was **environment-only** (no VS dev shell). Superseded: this session links cleanly. Other findings still valid. |
## 4. Supplied Claim Validation Summary

| Claim | Result | Confidence | Key Evidence |
| ----- | ------ | ---------- | ------------ |
| Item 1 (intake-gate test registered) | Confirmed (number inaccurate) | High | `tests/uia-driver/CMakeLists.txt` 16–21; `ctest -N` = Test #27; runs + passes |
| Item 2 (storage task refs) | Confirmed | High | diff; repo search: no stale refs in source |
| Item 3 (PrivilegeVerification comment) | Confirmed (file); remaining stale refs elsewhere | High | diff; `ServiceRegistration.h` LocalSystem; `CLAUDE.md`/`CODE_INDEX.md` still stale |
| Item 4 (design.md) | Confirmed | High | diff; scanner implemented in `ServiceConnection.cpp` |
| M4 (SetEngineActive) | Confirmed | High | `WindowShell.cpp` handler + `StorageAnalysis` setter |
| L7 (Move…→Cut) | Confirmed | High | `StorageAnalysis.cpp:783,803` `L"Cut"` → `file.cut` |
| L10 (unfocused text brush) | Confirmed | High | `ColumnView.cpp` `isSelected && isFocusedColumn ? textOnAccent : text` |
| L1 (dead SystemAnimationsEnabled removed) | Confirmed | High | dead `WindowShell::` member removed; live `ffui::` retained |
| Debug build clean | Confirmed | High | 159/159 steps, exit 0, no warnings |
| `/W4 /WX` strict | Confirmed | High | `build.ninja:170` + `CMakeLists.txt` |
| "26/26" CTest | **Contradicted** | High | Actual 27/27, Test #27 |
| New test registration | Confirmed | High | Test #27 in fresh discovery |
| Code-signing blocker | Confirmed (still open) | High | `PinnedSignatures.h` all-zero; fail-closed |
| Non-elevated runtime blocker | Confirmed (still open) | High | no elevated run |
| Clean-host blocker | Confirmed (still open) | Medium | matrix evidence doc |
| Windows UI validation blocker | Confirmed (still open) | High | SKIPPED tags; manual matrix |
## 5. Detailed Change Review

**Item 1 — Intake-gate test.** Registration correct, gated on `find_program(pwsh)`, TIMEOUT 120. Script dot-sources `verify/intake.ps1` and asserts on `Resolve-ValidateGateOutcome` for four synthetic verdicts incl. the CRITICAL unrepresented-edit case. Meaningful, fail-capable (`$ErrorActionPreference='Stop'`, `exit 1`). Verified passing. Only inaccuracy: Test #27, not #26.

**Item 2 — `storage-analysis/tasks.md`.** Two edits replace `TreemapView::Squarify`/`HitTest` → `ffui::Squarify`/`HitTest` (in `TreemapLayout.h`). Free functions exist and are unit-tested. No stale refs in source. Documentation-only, no mismatch.

**Item 3 — `PrivilegeVerification.h` comment.** Old ("virtual account… stubbed") → new ("LocalSystem… constrained broker… raw-volume scanner"). Accurate: `ServiceRegistration.h` selects LocalSystem; `ServiceConnection.cpp` dispatches real scanner; matrix shows LocalSystem opens the volume. **Remaining contradiction:** `CLAUDE.md:41-42` and `CODE_INDEX.md:148-149` still say "virtual service account… never LocalSystem".

**Item 4 — `design.md`.** Non-Goals superseded marker, D5 correction, thorough D4 rewrite to LocalSystem constrained-broker. Accurate and historically honest. `index-storage-and-scanning` confirmed to implement the scanner.

**M4 — Engine status propagation.** `WM_APP_ENGINE_STATUS` handler now calls `storageAnalysis_.SetEngineActive(engineActive_)` + `RequestRepaint()`. Single handler = single source of truth; storage panel gets the same value at every transition. Default `engineActive_=false`. UI-thread handler — no thread-affinity issue.

**L7 — "Move…"→"Cut".** `StorageAnalysis.cpp:783` `L"Cut"`; `case 4` invokes `file.cut`. Label matches cut semantics. No other surface labels `file.cut` as "Move…".

**L10 — Unfocused selection text.** Removed `opacity 1.0/0.35` + `SetOpacity`. Now `(highContrast||isFocusedColumn)?selectionBrush_:selectionSoftBrush_` fill + `isSelected && isFocusedColumn ? textOnAccentBrush_ : textBrush_` text. Focused→white-on-accent; unfocused→normal text on soft fill. HC falls back to system colors. Plausible improvement; no visual measurement.

**L1 — Dead `SystemAnimationsEnabled` removal.** Dead `WindowShell::SystemAnimationsEnabled() const` member (decl+defn) removed. **Live** `ffui::SystemAnimationsEnabled()` (`UiAnimation.h`/`.cpp`, `SPI_GETCLIENTAREAANIMATION`) remains and gates `FloatAnimation::AnimateTo`. Reduced-motion preserved; `test_ui_style.cpp` exercises it. AGENTS.md documents the live helper.

**New supporting code (same commit):** `TreemapLayout.h` (pure math, tested), `IconCache` (bounded/DPI-aware/off-thread), `UiStyle` helpers, `WriteDeadline` watchdog, `ConnectionRegistry` hardening + its unit test. All build and pass; the connection-scoped-handle test is meaningful and security-relevant.

## 6. Build Validation

- **Commands:** wiped `build/debug`; `$cmake --preset debug` (exit 0, ~4.2s); `$cmake --build --preset debug`. VS 2022 DevShell loaded (MSVC 19.44.35208 + Windows SDK); `FASTFILES_NINJA_EXE` = VS-bundled Ninja.
- **Build:** exit 0, 159/159 steps, ~20.6s. All executables linked (`FastFiles.exe`, `FastFilesIndexSvc.exe`, `FastFilesEngine.exe`, `FastFilesSetup.exe`, `fftest.exe`, 23 test exes). **No link failures** (contrasting opencode's environment-only link failure).
- **Warnings:** `Select-String build.log 'warning C|error C'` → none. Clean under `/W4 /WX`.
- **Strict flags:** `build.ninja:170` `FLAGS = ... /W4 /WX /permissive- /sdl /guard:cf /Zc:__cplusplus`; `CMakeLists.txt` also `/DYNAMICBASE /NXCOMPAT`. No target disables WaE. Changed files compiled. Clean rebuild — no incremental hiding.
- **`cmake --preset analyze`: attempted.** Configure succeeded (exit 0, `FASTFILES_STATIC_ANALYSIS=ON`). The build **FAILED at LINK** with `LNK2019: unresolved external __std_regex_transform_primary_wchar_t` — a host **toolchain mismatch**: this host also has a newer "VS 18 BuildTools" (MSVC 14.50) installed, and the analyze build's link picked up its linker (`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\...`) mixed with the VS 2022 runtime. **The compile stage succeeded** (all .obj built; `/analyze` produced no static-analysis errors), and the non-regex targets linked fine. This is an **environment issue, not a code defect** — the documented `/W4 /WX` debug build is clean. The `verify.ps1 doctor` capability confirms the toolchain is otherwise present (VS vc-toolset/cmake/ninja/SDK PASS; `verify.ps1 build` is long-running and timed out, superseded by the direct debug build).

## 7. Test Validation

- **Discovery (`ctest -N`):** `Total Tests: 27`; `ffintake_gate_ps_tests` = **Test #27**; `ffuia_driver_ps_tests` = #26 (pre-existing).
- **Full suite:** `100% tests passed, 0 tests failed out of 27`; ~22.14s; exit 0. Every test `Passed`.
- **New test direct:** 10/10 checks `ok`; `RESULT: all intake gate regression tests passed`; `Passed 1.12 sec`; exit 0.
- **Fresh configuration:** reproduced after wiping `build/debug` — not a stale-cache artifact.
- **Quality:** intake-gate test has meaningful, fail-capable assertions guarding a CRITICAL regression; cannot silently pass. `test_connection_registry.cpp` adds real security coverage. No flaky behavior; no skips. See `TEST_REVIEW.md`.

## 8. Deferred UI Items
## 9. Regression Assessment

- **Functional:** None. All 27 tests pass; the new intake-gate test and connection-registry test add coverage. M4/L7/L10/L1 changes are localized and consistent.
- **Security:** No regression. Closed command protocol intact (no `NotYetImplemented`/open-path primitive). Connection-scoped handles now *enforced + unit-tested* (`test_connection_registry.cpp` — non-owner Stop/Close rejected). Placeholder Authenticode pins still fail-closed (unchanged). Scanner still forwards only `$STANDARD_INFORMATION`/`$FILE_NAME`. DLL hardening present. `WriteDeadline` watchdog is a hardening addition.
- **Reliability:** No regression. Device-loss recovery (`D2DERR_RECREATE_TARGET`) fanned out via dirty-marking. Degraded-mode path remains the active/safe path (unchanged).
- **Performance:** No regression observed (tests within expected durations; `FloatAnimation` allocation-free; `IconCache` off-thread/bounded).
- **UI / accessibility:** L10 and L1 are improvements; reduced-motion preserved through the live `ffui::SystemAnimationsEnabled()`. High-Contrast fallbacks retained. No visual regression confirmable without an interactive desktop (limitation).
- **Documentation:** Net improvement (PrivilegeVerification.h, design.md, AGENTS.md corrected). Residual contradiction in CLAUDE.md/CODE_INDEX.md (pre-existing stale text not yet swept).
- **CTest discovery / ordering:** New test appended as #27; existing order (#1–26) unchanged. No test silently disabled.
- **PowerShell execution:** `pwsh` correctly discovered; both PS tests register and pass. Quoting handles spaces.

## 10. Outstanding Blockers

See `BLOCKERS.md`. Summary: all four external blockers (code-signing pins, non-elevated runtime, clean-host matrix, Windows UI validation) are **confirmed still open** with direct evidence. The placeholder Authenticode pins (`PinnedSignatures.h:16-17`, all-zero) remain and fail-closed — the privileged path is not production-validated; this is the documented current state, unchanged by this session. Internal code defects (stale CLAUDE.md/CODE_INDEX.md; committed binary + prompt template; reporting inaccuracies) are separated as low-severity items.

## 11. Reconciliation with Previous Findings

| Previous Finding | Previous Status | Current Evidence | Updated Status | Reason |
| ---------------- | --------------- | ----------------- | -------------- | ------ |
| CRITICAL-20260804-B3E8-001 (placeholder pins) | Open | `PinnedSignatures.h:16-17` still all-zero; fail-closed | Still Open | Confirmed still open. |
| HIGH-20260804-B3E8-002 (degraded active) | Open | Scanner implemented but pins placeholder; AGENTS.md says degraded active | Still Open | Scanner impl confirmed; operational validation pending. |
| HIGH-20260804-B3E8-003 (LocalSystem) | Open (by design) | `ServiceRegistration.h` LocalSystem; AGENTS.md/design.md corrected | Still Open (by design) | Docs now accurate; blast-radius unchanged. **New:** stale CLAUDE.md/CODE_INDEX.md (LOW-…-001). |
| opencode "build PARTIAL / tests NOT EXECUTED" | Open | Clean build (159/159, exit 0); 27/27 tests pass | Resolved (environment) | Was environment-only (no VS dev shell). Superseded. |
| LOW-20260804-K7P2VN-001 (query length limit) | Open | `History.cpp` changed but no length guard added | Still Open | Not addressed. |
| LOW-20260804-K7P2VN-002 (non-constant-time compare) | Open | `ClientAuthentication.cpp` changed (+9) but still `==` compare | Still Open | Not addressed. |
| INFO-20260804-K7P2VN-002 (USN journal cleanup) | Open | `UsnJournalReader.cpp` changed (+27) but no explicit cleanup-on-shutdown | Still Open | Not addressed. |
## 12. Production-Readiness Assessment

| Area | Rating | Evidence | Required Action |
| ---- | ------ | -------- | --------------- |
| Functional correctness | Ready with Minor Concerns | 27/27 tests pass; claimed changes verified correct. **New:** L9 "% of Parent" has a real calculation defect (wrong denominator; files show "—"; doesn't sum to 100%); M3 overlap at narrow widths. | Fix L9 denominator; reserve M3 pane width. |
| Build health | Ready | Clean `/W4 /WX` build, exit 0, no warnings | None. |
| Test health | Ready | 27/27 pass; meaningful new tests; no flaky/skip | Correct "26/26" → 27/27. |
| Security | Ready w/ Minor Concerns (degraded) / Not Ready (privileged) | Invariants intact; pins placeholder/fail-closed | Populate pins; constant-time compare (LOW-K7P2VN-002). |
| Reliability | Ready with Minor Concerns | Device-loss recovery; degraded path solid | None for degraded. |
| Accessibility | Requires Visual Validation | L10/L1 plausible; reduced-motion preserved | Visual confirm contrast/HC/reduced-motion. |
| UI validation | Requires Visual Validation | No interactive desktop this session | Manual smoke matrix. |
| Documentation accuracy | Requires Remediation | design.md/AGENTS.md/PrivilegeVerification.h corrected; CLAUDE.md/CODE_INDEX.md stale | Update CLAUDE.md/CODE_INDEX.md. |
| Deployment readiness | Not Assessed (privileged) | No install/elevated run | Signed install + service validation under elevation. |
| Privileged-path readiness | Not Ready | Placeholder pins; no operational validation | Populate pins; live-service validation. |
| Degraded-mode readiness | Ready with Minor Concerns | Build/test green; degraded path well-tested | Minor doc/hygiene fixes. |
| Operational readiness | Not Ready (privileged) | as above | as above |
| Clean-host reproducibility | Requires Clean-Host Validation | matrix negative result robust but not matrix-complete | Re-run matrix on clean host + secondary OSes. |

## 13. Required Next Actions

**Immediate (low effort):**
1. Correct addendum's "26/26"/"test #26"/"previously 25" → **27/27 / Test #27 / previously 26**.
2. Correct addendum's "uncommitted working tree" → "committed in `216d931`".
3. Update `CLAUDE.md:41-42` and `CODE_INDEX.md:148-149` to the LocalSystem constrained-broker model.

**Before merge:**
4. Decide whether `dist/FastFiles.exe` (committed binary) and `Audit.txt` (prompt template) belong in git; consider removing/gitignoring the binary.
5. **Fix L9 "% of Parent"** (`StorageAnalysis.cpp:681-691` display + `524-530` sort): use the actual parent total as the denominator (include files; not just sibling directories); compute files' % too; use `double` in the sort key to avoid uint64 overflow for >~18 TB folders.
6. **Fix M3 details-pane overlap** (`WindowShell.cpp:880-893`): reserve the 300px details-card width in the ColumnView viewport (or collapse the pane below a minimum width).

**Before release (privileged path):**
7. Provision a code-signing certificate and populate the two thumbprints in `PinnedSignatures.h`.
8. Complete live-service operational validation (signed install + service start + privileged scan end-to-end).
9. Address LOW-20260804-K7P2VN-002 (constant-time thumbprint compare).

**Environment-dependent / visual validation:**
10. Interactive desktop smoke matrix for UI changes (M4/L7/L10/L1) and deferred items (M1, M3, M5-display, L9-display, L11).
11. Clean-host matrix rerun (`Run-PrivilegeCandidateMatrix.ps1`).
12. Re-attempt `cmake --preset analyze` on a host without the VS "18" BuildTools/VS 2022 toolchain clash (or pin the toolset), to confirm `/analyze` static analysis is clean.

**Optional improvement:**
13. Add IPC concurrency/partial-read tests; expand selection-model tests; consider `cmake --preset analyze` in CI.

## 14. Limitations

- **No elevated execution** — privileged-path *runtime* (service install/start, signed-install, privileged scan end-to-end) not validated. Static inspection + build/test of binaries only.
- **No interactive desktop UI session** — UI rendering (contrast ratios, layout overlap, dark-theme coverage, sidebar highlight) not visually inspected. Headless UIA-driver + animation/layout/style tests pass; visual approval requires an interactive Windows desktop.
- **No clean-host/fresh-OS matrix run.**
- **`cmake --preset analyze` build attempted** — configure succeeded; the build failed at LINK due to a host toolchain mismatch (a newer "VS 18 BuildTools" MSVC 14.50 linker mixed with the VS 2022 runtime: `__std_regex_transform_primary_wchar_t` unresolved). The compile stage (incl. `/analyze`) succeeded with no static-analysis errors; only a few regex-using link targets failed. This is an environment issue, not a code defect; the documented `/W4 /WX` debug build is clean.
- Prior session directories (`20260804-154500-b3e8d2`, `-172401-K7p2vN`, `-172438-OsqrKC`) were empty of report files; their findings are known only via the merged-input file, bounding full cross-session reconciliation.
- `Audit.txt` (prompt template) and `dist/FastFiles.exe` (binary) inspected only superficially (not part of any supplied claim).

## 15. Final Verdict

**Validated with Minor Concerns.**

Every material *code-level* claim (Items 1–4, M4, L7, L10, L1; clean `/W4 /WX` build; new test registered and passing) is **confirmed** by direct code inspection and independent execution. The build (159/159, exit 0, no warnings) and the full test suite (**27/27** pass, exit 0) were independently reproduced from a clean configuration, superseding the prior opencode environment-only link failure. No regressions were found in build, tests, or security invariants.

Two supplied *result* claims are **contradicted** by current evidence: the test count is **27/27 (not "26/26")**, the new test is **Test #27 (not #26)**, and the changes are **committed** (not an uncommitted working tree). These are reporting inaccuracies, not code defects.

One **remaining documentation contradiction** (stale "virtual service account" in `CLAUDE.md`/`CODE_INDEX.md`) and minor hygiene items (committed binary/prompt template) are low-severity and do not block the degraded-mode path. The privileged path remains **Not Ready** (placeholder Authenticode pins, pending operational validation) — unchanged and as documented. UI changes require visual validation on an interactive desktop.

**One functional defect was found in a deferred item (L9 "% of Parent"):** the denominator is the sum of sibling *directories'* subtree sizes (excluding files and not the parent's total), so the column is mislabeled, files always show "—", and percentages don't sum to 100% when the parent contains files; the sort key also risks uint64 overflow for folders >~18 TB. This is a Medium-severity functional defect in the *storage-analysis* surface (not in any supplied claim's changed files — the supplied L1/M4/L7/L10 changes are correct), surfaced by the required independent static inspection of deferred L9. **M3** (details-pane overlap at narrow widths) is confirmed applicable via static layout. Neither was introduced by the supplied changes; both predate them. The `cmake --preset analyze` build failed only due to a host toolchain mismatch (VS "18" BuildTools vs VS 2022), not a code defect; the `/analyze` compile stage was clean.

Per the strict evidence rules, "Validated" (unqualified) is not used because the test-count/working-tree claims are contradicted, an L9 functional defect exists, and visual validation was not possible; the appropriate verdict is **Validated with Minor Concerns** (the supplied changes themselves are correct and safe to merge for the degraded-mode path, with L9/M3 to be addressed separately).

| Deferred M5/L5/L9 | "Deferred higher-risk" | M5 likely resolved (task 11.2); L5 resolved (metrics aliased); **L9 = real defect** (wrong denominator, excludes files, mislabeled; sort-key uint64 overflow risk for >~18 TB) | M5/L5 Superseded/Resolved; **L9 Still Open (functional)** | Broader modernize-ui-appearance addressed M5/L5; L9 independent static inspection found a genuine calculation defect. |
| Deferred M3/L11 | "Deferred higher-risk" | **M3 confirmed applicable (static)** — 300px right-anchored details card overlaps columns at narrow widths (no width reservation); L11 not specifically addressed | Still Open (require visual) | Unchanged; M3 now has static evidence. |
| Supplied "26/26" | Claimed | Actual 27/27 | Contradicted | Count/number inaccurate. |
| Supplied "uncommitted working tree" | Claimed | Tree clean; committed in `216d931` | Contradicted | Working-tree claim inaccurate. |


| Item | Current status (independent) | Exact remaining validation |
| ---- | ---------------------------- | ------------------------- |
| M1 (loading vs empty) | Empty state exists (`ColumnView.cpp:695` "This folder is empty"); loading-state distinction not specifically addressed. Still applicable. | Visual: confirm distinguishable loading vs empty state. |
| M3 (details-pane overlap) | **Confirmed still applicable (static).** `RenderDetails` (`WindowShell.cpp:880-893`) is a fixed 300px (`kPanelWidth`) right-anchored floating card (`left = max(0, viewportWidth-300)`) that does **not** reserve width in ColumnView → the rightmost columns render *under* the card at narrow widths. Restyle (task 5.5) changed visual treatment, not width reservation. | Visual: confirm overlap at narrow widths; reserve pane width in ColumnView. |
| M5 (dark-theme coverage) | **Likely already resolved** by `settings-and-appearance` task 11.2 (details pane, sidebar, search edit, treemap now theme-aware). | Visual: confirm dark rendering across all surfaces. |
| L5 (metric consolidation) | **Already resolved** — `ColumnView.h:143-145` aliases metrics to `UiMetrics::` ("cannot diverge"). False positive. | None (code-level resolved). |
| L9 (% of Parent) | **Defect found (not just visual).** `StorageAnalysis.cpp:681-691` (display) & `524-530` (sort) compute `% = item.totalSizeBytes / (sum of sibling *directories'* totalSizeBytes) * 100`. Denominator excludes files and isn't the parent's total → mislabeled; files always show "—"; percentages don't sum to 100% when the parent has files. Sort key `totalSizeBytes * 1000000ULL` can overflow uint64 for folders >~18 TB. `FormatPercent` math itself is correct; the `whole` passed is wrong. | Functional fix: use the actual parent total as denominator; include files; use `double` in sort key. Then visual confirm. |
| L11 (sidebar highlight) | Sidebar restyled (task 5.2) but specific highlight not targeted. Still applicable. | Visual: confirm selected-navigation highlight rendering. |

None of the deferred items were accidentally modified by the supplied changes (verified via diff). No fix was applied to them (per instructions).

| Uncommitted working-tree claim | **Contradicted** | High | tree clean; committed in `216d931` |

| Session `20260804-172401-K7p2vN` | `216d931` | Static | LOW-…-001 (query length limit), -002 (non-constant-time compare), INFO-001..003. | All still open; not affected by this session's changes. |
| Session `20260804-172438-OsqrKC` | `216d931` | Static coverage matrix | File-by-file pass matrix (static). | Confirms scope; no execution. |
| "Additional Results Addendum" (merged input 3445–3514) | claimed uncommitted; actually committed `216d931` | Claims clean build + 26/26 + Items 1–4/M4/L7/L10/L1 + deferred list + blockers | The claims this session validated. | Build/Items confirmed; test-count + working-tree + deferred-list framing contradicted. |
| `Audit.txt` (committed) | `216d931` | — | An autonomous-audit *prompt template*, not a report. | Not a prior audit; in-commit artifact (hygiene). |
