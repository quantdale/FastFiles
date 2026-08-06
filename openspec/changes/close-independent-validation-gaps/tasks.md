# Tasks — close-independent-validation-gaps

Implementation tasks for the confirmed gaps from independent-validation session `20260804-180500-v7k4p2`. Behavior contracts live in the corresponding specs; decisions D1–D5 live in `design.md`. Tasks marked **[external]** require authorized, elevated, interactive-desktop, or clean-host prerequisites and cannot be completed in a normal non-elevated development session. Baseline before new work: clean `/W4 /WX` debug build; **27** tests discovered, **27/27** passing. After this change the count may increase; all discovered tests must pass.

## 1. Storage "% of Parent" correctness (finding MED-20260804-v7k4p2-006; spec `storage-drilldown-and-treemap`)

- [ ] 1.1 Add a headless, pure percentage-computation helper (takes `rowSize`, `parentTotal` as `uint64_t`; returns a formatted display string and a numeric sort key) with no Direct2D dependency, per design D1. Target: a `tests/ui` seam (e.g., a new `StoragePercentage.h` or a helper in `src/ui`).
- [ ] 1.2 Add unit tests: parent with files+directories; parent with only files; parent with only directories; zero-size parent; individual zero-size child displays "—"; root/no-parent row displays "—"; large synthetic sizes (multi-ten-TB) with no overflow; rounding tolerance (children sum to 100% within tolerance). Use the `Check()` CTest style.
- [ ] 1.3 Wire the display path (`StorageAnalysis.cpp` LVN_GETDISPINFO case 4, currently `:681-691`) to compute the denominator as the **complete immediate parent size** (files + directories, or the parent aggregate already available via `RequestFolderAggregate`), replacing the current sibling-directories-only sum. File rows must show a correct percentage (not "—") when their size is known.
- [ ] 1.4 Preserve "—" for zero/`Pending`/`NotFound` parent totals and re-render in place when the aggregate resolves (reuse `HandleAggregateResult` invalidation).
- [ ] 1.5 Replace the overflow-prone sort key (`StorageAnalysis.cpp` case 4, currently `:524-530`, `totalSizeBytes * 1000000ULL / parentTotal`) with an overflow-safe comparator per design D2 (exact `__int128` cross-multiplication, or a documented `double`-with-epsilon fallback) and a stable secondary tie-break key. Update `SortItems` so sort uses the same parent-relative values the display shows.
- [ ] 1.6 Add unit tests: equal percentages, nearly-equal percentages, and multi-ten-TB values that would overflow `size * 1000000ULL`; assert deterministic order and no overflow.
- [ ] 1.7 Run `cmake --preset debug`, `cmake --build --preset debug`, `ctest -N`, and full `ctest`; confirm all discovered tests pass.

## 2. Responsive details-pane layout (finding LOW-20260804-v7k4p2-007; spec `responsive-details-pane-layout`)

- [ ] 2.1 Add a pure geometry helper that computes `{availableColumnWidth, detailsRect, columnClipRect}` from `(viewportSize, detailsPaneVisible, dpiScale)` per design D3 (reserve pane width before column-view width; DIP-based; never overlap). Target: a headless testable seam under `src/ui`.
- [ ] 2.2 Add unit tests: pane visible vs hidden changes available width; narrow width (below threshold) collapses/hides the pane and never overlaps; dual-pane splits remaining width; 100% and 150% DPI scaling; clipping/hit-test bounds exclude the pane.
- [ ] 2.3 Make `ColumnView` consume the computed `availableColumnWidth`/clip rect instead of the raw viewport, and make hit-testing (client→normalized) operate within that area.
- [ ] 2.4 Implement the selected narrow-width strategy (collapse to a disclosure bar, or hide, per design D3) so no column content is occluded and navigation/selection remain usable; provide a way to restore the full pane.
- [ ] 2.5 Ensure re-layout/repaint happens only on actual geometry or state change (live resize, pane open/close, selection change), reusing dirty-marking + `RequestRepaint`; verify no idle repaint loop or animation timer while idle; honor `SystemAnimationsEnabled()` and High Contrast as the rest of the UI does.
- [ ] 2.6 Run the baseline build + full CTest; confirm all discovered tests pass and no animation/perf regression tests fail.

## 3. Service-account documentation consistency (finding LOW-20260804-v7k4p2-001; spec `service-account-documentation-consistency`)

- [x] 3.1 Update `CLAUDE.md:41-42` and `CODE_INDEX.md:148-149` to state the LocalSystem constrained-broker model (LocalSystem as constrained privileged broker; raw MFT/USN scanning implemented; degraded path active/safe while pins and privileged-path validation are incomplete; blast radius deliberate and not minimized), matching the corrected `AGENTS.md`/`design.md` D4. — **complete (prior session; verified 2026-08-06)**: `CLAUDE.md` is now a stub pointing at `AGENTS.md` and states "runs under **LocalSystem** as a deliberately *constrained privileged broker*"; `CODE_INDEX.md:150` states the production model runs the service under LocalSystem and `:276` states the scan/journal are implemented; `README.md:22` matches.
- [x] 3.2 Run a repository-wide search for superseded descriptors (`virtual service account`, `never LocalSystem`, `stubbed` as current, `SeBackupPrivilege only`) and confirm the only remaining hits are explicitly-marked superseded/historical text (per spec `service-account-documentation-consistency`). — **complete (prior session; re-verified 2026-08-06)**: repo-wide search returns only explicitly-marked/historical hits — audit-session log `all-edit-with-results-20260804-1748.md`, closed-change design/proposal/tasks records, foundation `design.md:57` (explicitly "(superseded by the constrained-broker decision)"), code comments (`VolumeScanner.cpp:98`, `PipeListener.cpp:103`, `Identifiers.h:13`), and the synced main spec's `Minimal Privilege Grant`, which is now explicitly marked superseded with a pointer to the `resolve-raw-volume-privilege-insufficiency` evidence. Sole residual: open change `autonomous-runtime-verification` delta spec mentions "(e.g. virtual service account)" — not guidance documentation, not synced; noted for that change's review.
- [ ] 3.3 Record the documentation-search output as acceptance evidence in the change's evidence notes.

## 4. Pinned-authentication provisioning hardening (external release blockers; spec `pinned-authentication-provisioning`)

- [ ] 4.1 Add a constant-time comparison helper for the fixed 20-byte `Thumbprint` and use it in `VerifyPinnedSignature` (`src/setup/src/AuthenticodeVerification.cpp:81`) instead of `*actual == expected`, per design D5.
- [ ] 4.2 Add/keep negative authentication tests covering: unsigned binary rejected; mismatched/placeholder/expired/untrusted signer rejected; fail-closed when pins are placeholder; **partially-populated pin set (one thumbprint populated, the other still placeholder) rejected for the unpinned peer**; shipped builds disable the compile-time-only diagnostic escape hatch. Extend `tests/engine/test_authenticode_verification.cpp` as needed.
- [ ] 4.3 Run the protocol fuzz tests and the authenticode-verification tests; confirm all pass. Treat a clean fuzz-test run as **release-gate evidence** for the "protocol fuzz-test regression coverage" gate (record under the repository's evidence-retention path when the release gate is exercised).
- [ ] 4.4 Document the pin-provisioning and rotation procedure (private material never committed; pins populated from the signing certificate at release; rotation updates pins and revalidates) in the change design/evidence. **[external]** No certificate value is invented here.
- [ ] 4.5 **[external]** Signed privileged-path install and runtime validation under elevation: install signed service, start it, confirm connection-state transition to `Active` and privileged scan, **and confirm degraded-mode fallback (no privileged scan, degraded path stays active) when the signed service is unavailable or the handshake fails**, per the "Service-startup and connection-state evidence covers the active transition" scenario. Prerequisite: a provisioned code-signing certificate + an authorized elevated host.
- [ ] 4.6 **[external]** End-to-end privileged scanning validation (real MFT/USN) against the documented supported volumes; record evidence.
- [ ] 4.7 **[external]** `fftest` privilege-diagnostics coverage against the running service; record evidence.
- [ ] 4.8 **[external]** Clean-host privilege-candidate matrix across the documented supported Windows environments; record as pending / `REQUIRED-BUT-UNAVAILABLE` with machine evidence until actually executed.

## 5. Interactive Windows UI validation (external visual blocker)

- [ ] 5.1 **[external / interactive Windows desktop]** Run the visual matrix for the M4/L7/L10/L1 and this change's surfaces: Light and Dark themes; 100% and 150% DPI; animations enabled and disabled; High Contrast where supported; narrow, normal, and wide window widths; details pane open and closed; single-pane and dual-pane behavior. Confirm no column content renders under the details pane at any width; confirm the "% of Parent" column values sum to 100% (within tolerance) in a real drill-down.
- [ ] 5.2 **[external]** Reconfirm dark-theme coverage visually (static evidence indicates M5 may already be addressed; this is a visual confirmation task only — no code change unless current code proves otherwise) and record screenshots/observations as evidence.
- [ ] 5.3 **[external]** Capture screenshots/evidence under `verify/runs/` (untracked execution evidence) for the above matrix.
## 6. Repository hygiene decision (Low; not a runtime capability)

- [ ] 6.1 For `dist/FastFiles.exe` (committed build artifact): decide and apply one option — (a) remove the binary from source control and gitignore the output path, ensuring documented builds still generate the binary outside tracked source, or (b) retain it with a documented rationale and a distribution mechanism (e.g., release binaries emitted via a release-artifact mechanism rather than tracked in source). Record the decision.
- [ ] 6.2 For `Audit.txt` (819-line autonomous-audit prompt template): decide and apply one option — (a) remove it as an accidental development artifact, (b) retain it, (c) rename/move it to an appropriate documentation/tooling location, with ownership and maintenance expectations recorded. Do not alter the eight validation reports under `codebase-audit/sessions/20260804-180500-v7k4p2/`.
- [ ] 6.3 Run `git status`, `git diff --name-status`, and `git show --stat HEAD` to confirm the hygiene change (if removal) reduces only the intended files and that prior audit-session directories remain unmodified.

## 7. Final validation and reconciliation

- [ ] 7.1 Run the baseline gates: `cmake --preset debug`, `cmake --build --preset debug`, `cmake -E chdir build\debug ctest -N`, `cmake -E chdir build\debug ctest --output-on-failure`. Confirm clean `/W4 /WX` build and all discovered tests pass (count may exceed 27).
- [ ] 7.2 Run the repository OpenSpec validation for this change and fix any schema/format violations.
- [ ] 7.3 Reconcile every closed finding ID against its requirement/task/validation: `MED-20260804-v7k4p2-006` (storage percentage), `LOW-20260804-v7k4p2-007` (details-pane layout), `LOW-20260804-v7k4p2-001` (documentation consistency), and the external release blockers (pins, constant-time compare, elevated/clean-host/interactive validation). Mark external blockers as open/pending with evidence; do not mark them complete without evidence.
- [ ] 7.4 Confirm no task was marked complete in advance, and that degraded mode remains usable/releasable independently of the privileged-path gates.

## Explicitly deferred / follow-up (not implemented in this change)

- Query-history/query-input length limit (`LOW-20260804-K7P2VN-001`) — follow-up.
- Explicit USN journal cleanup during shutdown (`INFO-20260804-K7P2VN-002`) — follow-up.
- IPC concurrency / partial-read / broken-pipe tests; selection-model test depth; same-SQLite-file concurrency coverage — follow-up test-hardening changes.
- `cmake --preset analyze` host toolchain normalization / CI pinning — separate optional follow-up (the link failure was environmental; compile + `/analyze` succeeded).
