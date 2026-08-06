# Findings — Independent Validation

Globally unique IDs: `<SEVERITY>-<YYYYMMDD>-<SESSION_SHORT_ID>-<NUMBER>` with session short id `v7k4p2`. Prior findings (e.g. `CRITICAL-20260804-B3E8-001`, `HIGH-20260804-B3E8-002/003`, `LOW-20260804-K7P2VN-001/002`, `INFO-20260804-K7P2VN-001/002/003`) are referenced, not edited.

### `[LOW-20260804-v7k4p2-001] Stale "virtual service account" documentation in CLAUDE.md and CODE_INDEX.md`
- **Severity:** Low
- **Category:** Documentation accuracy / consistency
- **Confidence:** Confirmed
- **Status:** Open
- **Relationship:** Extends prior `HIGH-20260804-B3E8-003`. The corrected authoritative docs (design.md D4, AGENTS.md) and `PrivilegeVerification.h` (Items 3 & 4) make this a *remaining* contradiction.
- **Related previous finding IDs:** HIGH-20260804-B3E8-003, CRITICAL-20260804-B3E8-001
- **Fingerprint:** docs|CLAUDE.md|CODE_INDEX.md|stale-virtual-account
- **Affected locations:** `CLAUDE.md:41-42`; `CODE_INDEX.md:148-149`
- **Description:** The supplied changes corrected the "virtual service account / stubbed scanner" wording in `PrivilegeVerification.h`, `design.md` (D4/D5/Non-Goals), and `AGENTS.md`. The near-duplicate guidance files `CLAUDE.md` and `CODE_INDEX.md` were NOT updated and still assert the superseded model: `CLAUDE.md:41-42` says `FastFilesIndexSvc` "Runs under a dedicated **virtual service account** with **`SeBackupPrivilege` only** (never LocalSystem/admin)" — the opposite of the implemented LocalSystem constrained-broker model.
- **Evidence:** `search_codebase` for `virtual account|NT SERVICE\\FastFilesIndexSvc` → `CLAUDE.md:42` and `CODE_INDEX.md:149`. `ServiceRegistration.h:19-20` `LocalSystem // constrained-broker`. `AGENTS.md` (corrected in `216d931`) states LocalSystem.
- **Impact:** Documentation contradiction. A reader following CLAUDE.md/CODE_INDEX.md would misjudge the blast-radius/security model. Low severity because authoritative design.md/AGENTS.md are correct and the code uses LocalSystem.
- **Reproduction or trigger:** Read `CLAUDE.md` 41–42; compare to `design.md` D4 and `AGENTS.md`.
- **Root cause:** The correction sweep touched AGENTS.md and design.md but missed the parallel guidance files.
- **Recommended remediation:** Update `CLAUDE.md:41-42` and `CODE_INDEX.md:148-149` to match AGENTS.md/design.md (LocalSystem constrained broker; scanner implemented; degraded mode active due to placeholder pins).
- **Validation after remediation:** Repo-wide search for "virtual service account" returns only historically-scoped (superseded-marker) text in design.md.

### `[LOW-20260804-v7k4p2-002] Reported test count/number inaccurate ("26/26", "test #26", "previously 25")`
- **Severity:** Low
- **Category:** Reporting accuracy
- **Confidence:** Confirmed
- **Status:** Resolved by this validation (accurate figures established)
- **Relationship:** Contradicts the supplied addendum's validation-results row.
- **Related previous finding IDs:** none
- **Fingerprint:** reporting|ctest|test-count|27-not-26
- **Affected locations:** `all-edit-with-results-20260804-1748.md:3487`
- **Description:** The addendum reports `ctest` as "26/26 tests passed; previously 25 tests" and the intake-gate test as "test #26". Independent execution from a clean configure shows **27/27 tests pass** and the intake-gate test is **Test #27**. `ffuia_driver_ps_tests` (Test #26) pre-existed before HEAD (`2f8061e`), so "previously 25" is also inaccurate (it was 26 before the intake-gate test). Likely cause: the count was taken where `pwsh` was not found by `find_program`, so only 25 C++ tests registered; with `pwsh` available, both PowerShell tests register → 27.
- **Evidence:** `ctest -N` → `Total Tests: 27`; full run → `100% … out of 27`; `ctest -R ffintake_gate_ps_tests -V` → `Test #27`. `git log -- tests/uia-driver/CMakeLists.txt` → `ffuia_driver_ps_tests` added in `2f8061e`.
- **Impact:** Low. The suite genuinely passes; only the number is wrong. No code defect.
- **Root cause:** Environment-dependent discovery (PowerShell presence) or a miscount.
- **Recommended remediation:** Correct the addendum's figures to 27/27, Test #27. Ensure future validation runs with `pwsh` on PATH.
- **Validation after remediation:** `ctest -N` from a clean configure shows 27; full run 27/27.

### `[LOW-20260804-v7k4p2-003] Working-tree state claim contradicted`
- **Severity:** Low
- **Category:** Reporting accuracy
- **Confidence:** Confirmed
- **Status:** Resolved by this validation
- **Relationship:** Contradicts the addendum's "Working-Tree State" section.
- **Related previous finding IDs:** none
- **Fingerprint:** reporting|working-tree|committed-not-uncommitted
- **Affected locations:** `all-edit-with-results-20260804-1748.md:3500`
- **Description:** The addendum asserts changes were uncommitted in the working tree. Independent `git status --porcelain=v2` shows the working tree is CLEAN for tracked files; all claimed changes ARE committed in HEAD `216d931`. The commit presumably happened after the addendum was authored.
- **Evidence:** `git status --porcelain=v2` → only `? all-edit-with-results-20260804-1748.md`. `git log -- <each claimed file>` → all last touched in `216d931`.
- **Impact:** Low. Changes exist and are correct; only the "uncommitted" framing is wrong. Important for audit traceability.
- **Recommended remediation:** Record that the changes are committed in `216d931`.
- **Validation after remediation:** `git status` clean; `git show --stat 216d931` contains all claimed files.

### `[MED-20260804-v7k4p2-006] L9 "% of Parent" calculation is semantically incorrect (not just visual)`
- **Severity:** Medium
- **Category:** Functional correctness (UI)
- **Confidence:** Confirmed
- **Status:** Open
- **Relationship:** Reconciles the deferred L9 item — independent static inspection (the task required this even without visual testing) found a real calculation defect, not merely an unvisualized one.
- **Related previous finding IDs:** (deferred L9)
- **Fingerprint:** functional|StorageAnalysis.cpp|L9|percent-of-parent-wrong-denominator
- **Affected locations:** `src/ui/src/StorageAnalysis.cpp:681-691` (display, LVN_GETDISPINFO case 4); `src/ui/src/StorageAnalysis.cpp:524-530` (sort key, case 4)
- **Description:** The "% of Parent" column denominator is `parentTotal = sum of all sibling *directories'* totalSizeBytes` (lines 683-686 / 526-527), iterated as `for (sibling : items_) if (sibling.isDirectory) parentTotal += sibling.totalSizeBytes;`. This is NOT percentage-of-parent in the conventional sense:
  1. Files are excluded from the denominator (only directories contribute), so for a parent that contains files, the directory percentages do not sum to 100% and a file's own cell always shows `L"—"` (case 4 only computes for directories).
  2. The denominator is the sum of *children subtree sizes*, not the parent's own total size — so the label is misleading (it is effectively "% of sibling-directories-subtree-sum").
  3. Sort and display use the *same* denominator, so they are mutually consistent — but both are mislabeled.
  4. Latent overflow: the **sort key** computes `item.totalSizeBytes * 1000000ULL / parentTotal` (uint64); for folders > ~18 TB this multiplication can overflow uint64 (max ~1.8e19). The **display** path uses `FormatPercent` with `double` (no overflow), so display is safe; only the sort key is at risk for very large folders.
- **Evidence:** `StorageAnalysis.cpp:681-691` and `524-530` (read this session). `FormatPercent` (`StorageAnalysis.cpp:57-60`) itself is correct (`part/whole*100` with a zero guard) — the defect is the *wrong `whole`* passed to it, not the percentage math.
- **Impact:** Misleading storage-analysis percentages; users comparing folder shares may see numbers that don't sum to 100% and files with no %. Medium severity (wrong user-facing data in a WizTree-like tool, a category the design doc explicitly calls out as "cannot afford to silently misreport").
- **Reproduction or trigger:** Open Storage Analysis drill-down on a folder containing both subfolders and files; observe the "% of Parent" column — files show "—", and folder percentages exclude file sizes from the denominator.
- **Root cause:** The denominator conflates "parent total" with "sum of sibling directory subtrees".
- **Recommended remediation:** Use the actual parent folder's total size as `whole` (requires passing the parent's aggregate, which is already requested via `RequestFolderAggregate`); include files in the denominator; compute files' % too. Use `double` (as `FormatPercent` does) for the sort key to avoid uint64 overflow.
- **Validation after remediation:** Percentages of all children (files + folders) sum to 100% of the parent; files show a % value; sort key matches displayed order for >18 TB folders.

### `[LOW-20260804-v7k4p2-007] M3 details-pane overlap confirmed applicable (static layout)`
- **Severity:** Low
- **Category:** UI layout
- **Confidence:** Confirmed (static)
- **Status:** Open (requires visual validation)
- **Relationship:** Confirms the deferred M3 item is a real, still-open layout concern (not a false positive).
- **Related previous finding IDs:** (deferred M3)
- **Fingerprint:** ui-layout|WindowShell.cpp|RenderDetails|overlap-300px-card
- **Affected locations:** `src/ui/src/WindowShell.cpp:880-893` (`RenderDetails`)
- **Description:** The details/preview pane is a fixed-width (`kPanelWidth = 300.0f`) floating card right-anchored to the viewport (`left = max(0, viewportWidth - 300)`), inset by `kSpaceS`, from `cardTop` (below badge) to `cardBottom` (above the 26px status bar). The ColumnView receives the *full* viewport width and does **not** reserve/exclude the 300px details-card width, so when the window is narrow the rightmost columns render *under* the floating details card → overlap. The recent restyle (task 5.5, "elevated card") changed the visual treatment but did not add width reservation.
- **Evidence:** `WindowShell.cpp:880-893` read this session; no ColumnView width adjustment for the details pane found.
- **Impact:** Visual overlap of details pane over column content at narrow window widths. Low severity (cosmetic/usability, not data correctness).
- **Reproduction or trigger:** Resize the window narrow with a long column path open + a selection (details pane visible); observe the rightmost column under the card.
- **Root cause:** The details pane is an overlay, not a reserved layout region.
- **Recommended remediation:** Either reserve the details-pane width in the ColumnView viewport (subtract `kPanelWidth + inset*2` when the pane is visible) or hide/collapse the pane below a minimum width.
- **Validation after remediation:** No overlap at any window width; columns reflow when the pane appears/disappears.

### `[LOW-20260804-v7k4p2-004] Committed binary and prompt-template artifacts in HEAD`
- **Severity:** Low
- **Category:** Repository hygiene
- **Confidence:** Confirmed
- **Status:** Open
- **Relationship:** Not part of any supplied claim; observed in `git show --stat HEAD`.
- **Related previous finding IDs:** none
- **Fingerprint:** hygiene|dist/FastFiles.exe|Audit.txt|committed-artifacts
- **Affected locations:** `dist/FastFiles.exe` (Bin 862208 bytes); `Audit.txt` (819 lines)
- **Description:** HEAD `216d931` adds a compiled binary `dist/FastFiles.exe` and an 819-line autonomous-audit prompt template `Audit.txt`. Binaries should generally not be committed (reproducibility/bloat). Neither is referenced by any supplied claim or build/test target inspected.
- **Evidence:** `git show --stat HEAD` lists both as added in `216d931`.
- **Impact:** Low. Repository bloat and potential staleness of a committed exe. No functional/security impact.
- **Recommended remediation:** Consider removing `dist/FastFiles.exe` from git (build output); decide whether `Audit.txt` belongs in-tree.
- **Validation after remediation:** `git show --stat HEAD` no longer lists the binary; build still produces it out-of-tree.

### `[INFO-20260804-v7k4p2-005] Several "deferred" UI items appear already resolved by the broader modernize-ui-appearance change`
- **Severity:** Informational
- **Category:** Reconciliation / false-positive
- **Confidence:** Medium (static; needs visual confirm)
- **Status:** Superseded / requires visual validation
- **Relationship:** Reconciles the addendum's "deferred" framing against the broader commit scope.
- **Related previous finding IDs:** (deferred M5/L5/L9)
- **Fingerprint:** reconciliation|deferred|already-resolved|M5|L5|L9
- **Affected locations:** `src/ui/src/ColumnView.h:143-145` (L5); `src/ui/src/StorageAnalysis.cpp:57-60` (L9); `openspec/changes/settings-and-appearance/tasks.md:78` task 11.2 (M5)
- **Description:** The addendum lists M5 (dark-theme coverage), L5 (metric consolidation), L9 (% of Parent) as deliberately-deferred higher-risk items. Static inspection indicates these were substantially addressed by the broader `modernize-ui-appearance`/earlier `settings-and-appearance` change, NOT merely deferred: (L5) `ColumnView`'s `kColumnWidth/kRowHeight/kBadgeHeight` now alias `UiMetrics::` ("cannot diverge"); (L9) `FormatPercent` computes `part/whole*100.0` with a zero-division guard; (M5) task 11.2 (complete) states dark-theme coverage fixed across details pane, sidebar, search edit, treemap.
- **Evidence:** see affected locations.
- **Impact:** Informational. The deferral list may overstate still-open work. Visual validation still required for the *displayed* behavior (L9 shown value; M5 actual dark rendering).
- **Recommended remediation:** Re-classify L5 as resolved; L9 as implemented-needs-visual-confirm; M5 as likely-resolved-needs-visual-confirm. Keep M1/M3/L11 as requiring visual validation.
- **Validation after remediation:** Visual smoke matrix on an interactive desktop.

### Positive findings (revalidated)
- **Build health:** Clean `/W4 /WX` build reproduced independently (159/159 steps, exit 0, all executables linked). Supersedes the opencode audit's link-failure (environment-only) result.
- **Test health:** 27/27 CTest pass (100%), exit 0, ~22.1s — independently reproduced.
- **Intake-gate test quality:** Meaningful, fail-capable assertions guarding a CRITICAL regression (unrepresented-edit gate FAIL must fail validate, not silently PASS). Not a rubber-stamp test.
- **Security invariants intact:** Closed command protocol (no `NotYetImplemented`/open-path primitive); connection-scoped handles (now unit-tested by `test_connection_registry.cpp`); placeholder Authenticode pins fail closed; scanner forwards only `$STANDARD_INFORMATION`/`$FILE_NAME`; DLL hardening present. No regression introduced by the UI/infra changes.
- **Documentation accuracy (core):** `PrivilegeVerification.h`, `design.md` (D4/D5/Non-Goals), and `AGENTS.md` now accurately describe the LocalSystem constrained-broker model and the implemented scanner.
