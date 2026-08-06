## Context

Independent validation session `20260804-180500-v7k4p2` confirmed the baseline is healthy: clean `/W4 /WX` debug build (159/159), **27/27** CTest passing, `ffintake_gate_ps_tests` = Test #27. The validated work items are: (1) a real functional defect in Storage Analysis' "% of Parent" — `StorageAnalysis.cpp:681-691` (display) and `:524-530` (sort) compute the denominator as the *sum of sibling directories' subtree sizes* rather than the complete parent total, so files always show "—", percentages don't sum to 100%, and the sort key `totalSizeBytes * 1000000ULL` can overflow `uint64_t` for folders > ~18 TB; (2) the details pane is a fixed 300-DIP right-anchored card (`WindowShell.cpp:880-893`) that does not reserve width in the column-layout calculation, so columns render underneath it at narrow widths; (3) stale "virtual service account" documentation in `CLAUDE.md:41-42` and `CODE_INDEX.md:148-149` contradict the LocalSystem constrained-broker model; (4) the external privileged-path release blockers: `PinnedSignatures.h` all-zero placeholders, non-constant-time thumbprint comparison (`AuthenticodeVerification.cpp:81` `*actual == expected`), and pending elevated/clean-host/interactive validation; (5) a repository-hygiene decision for the committed `dist/FastFiles.exe` and `Audit.txt`.

The security model is authoritative in `establish-architecture-foundation/design.md` (D1–D6) and the LocalSystem decision in `resolve-raw-volume-privilege-insufficiency/evidence/matrix-execution-and-selection.md`. This design references, never rewrites, those decisions.

## Goals / Non-Goals

**Goals:**
- Define semantically correct "% of Parent" (denominator = complete immediate-parent size including files), overflow-safe percentage sorting, and headless unit-testable seams.
- Define a responsive details-pane layout contract that never lets column content render underneath the pane, with an explicit narrow-width strategy, and preserves dual-pane, DPI, resize, clipping/hit-testing, and accessibility.
- Define a repository-wide documentation-consistency contract for the service account.
- Define secure pin provisioning/rotation, fail-closed and constant-time comparison, and the privileged-path release-gate evidence procedures.
- Make an explicit per-item repository-hygiene decision.

**Non-Goals:**
- No weakening of fail-closed authentication to force the privileged connection active.
- No re-architecture of the security model, IPC, or closed command protocol.
- No re-skining of the details pane beyond the layout contract.
- No re-work of confirmed items (Items 1–4, M4, L7, L10, L1), L5, or M5 (M5 retains only a visual verification task).
- No "fixing" of the `cmake --preset analyze` link failure in source (environment/toolchain only; optional follow-up).
- No completion of external validations within a normal non-elevated session — only the procedures and evidence requirements are defined here.
## Decisions

### D1: "% of Parent" semantics — denominator is the complete immediate-parent size

For every non-root row in a storage drill-down, "% of Parent" SHALL mean `(row subtree size) / (complete immediate parent size) * 100`, where "complete immediate parent size" includes the sizes of all of the parent's children — files and directories — and is the parent's own total (its aggregate subtree size), not the sum of only directory child subtrees.

**Why:** The validated defect (MED-20260804-v7k4p2-006) is exactly that the current code sums only sibling *directories'* subtree sizes, excluding files and the parent's own size. This makes file rows show "—" and makes directory percentages not sum to 100% when the parent contains files. Redefining the denominator to the parent's complete aggregate yields percentages that accurately describe parent composition.

- **Alternatives considered and rejected:** (a) Keep the sibling-directory-subtree denominator — rejected: reproduces the defect. (b) Use "sum of all sibling rows (files + dirs) sizes" as a proxy for parent size — rejected: excludes the parent's own size at deeper levels and can drift from the true parent total; the parent's aggregate is already available via the existing `RequestFolderAggregate`/index path, so a proxy is unnecessary. (c) Show two percentages ("of siblings" and "of parent") — rejected in this change for scope; the existing spec and UI label is "% of Parent", so a single, correct parent-relative value is the contract.

**Computation ownership:** A pure, headless function owns the percentage computation (and its rounding), taking `(rowSize, parentTotal)` as `uint64_t` and returning a formatted display string **and** a comparable numeric value for sorting. `FormatPercent` may be retained as the formatting helper, but the *values passed to it* are produced here. This makes the logic unit-testable without a Direct2D desktop.

- **Root rows / no-parent rows:** A row with no normal parent (the top of a drill-down scope) SHALL render "—" and sort as if its percentage were 0, matching current "—" behavior — while the *children* of that root are parent-relative to the root.

- **Zero-size child within a known non-zero parent:** The child SHALL render as "—" — matching the product decision that zero-size children are visually consistent with other unknown/zero-value rows and are not displayed as "0.0%". "—" thus covers both unknown-denominator contexts (zero-size/pending/NotFound parent, root row) and the zero-size-child case. The percentage helper still computes a numeric value (0) for sorting so the row sorts as if its percentage were zero.

- **Rounding tolerance referenced by the spec scenarios ("within the documented rounding tolerance"):** the cumulative rounding error of the percentage's display precision. With the existing one-decimal-place display, the tolerance is ±0.1 percentage points per child, so the summed children-of-one-parent percentages fall in `[100% − 0.1·n%, 100% + 0.1·n%]` for `n` children. Display precision is an implementation detail owned by the percentage helper; the requirement is that the sum not drift outside the cumulative rounding error of the chosen precision. Two asserting parties should not panic over sub-rounding-unit differences.

### D2: Overflow-safe percentage comparator

Sorting by "% of Parent" must never overflow when computing a comparison key. The current sort key `rowSize * 1000000ULL / parentTotal` overflows `uint64_t` once rowSize exceeds `~1.8e13` (≈ 18 TB).

**Why:** Storage-analysis must not misreport or mis-sort on large (multi-ten-TB) volumes; the design explicitly rejects silently misreporting disk usage.

- **Approach:** Compare on exact `__int128` cross-multiplication of `rowSize * parentTotal` for sorting (overflow-free and deterministic), with a stable secondary tie-break key (e.g., name ordinal) so equal/nearly-equal percentages yield a deterministic order.
- **Fallback:** `double`-based percentage comparison with an explicit epsilon is documented as acceptable where `__int128` is not desirable; the decision is captured here and covered by an overflow unit test.
- **Alternatives considered and rejected:** (a) `long double` ratios — rejected: platform-dependent precision; not guaranteed stable across toolchains. (b) Keep `uint64_t * 1000000ULL` — rejected: overflows on large volumes. (c) Normalize by min-size-relative scaling — rejected: adds rounding noise.

### D3: Details-pane responsive layout — reserve width before computing column-view width

The details pane SHALL be treated as a reserved right-hand region in the layout calculation: `availableColumnWidth = viewportWidth - detailsPaneReservedWidth` (when the pane is visible), so the column view and its hit-testing operate only within the reserved area and never underneath the pane.

**Why:** The validated defect (LOW-20260804-v7k4p2-007) is that `RenderDetails` floats a fixed 300-DIP card over the full-width column view. Reserving the width at layout time is the definitive fix: client-to-normalized hit-testing, clipping, and scroll bounds then already exclude the pane.

- **Narrow-width strategy (selected):** below a documented minimum `viewportWidth` that cannot comfortably host both the column view and the 300-DIP pane, the details pane SHALL **collapse to a thin disclosure bar** (or hide, if collapse is not feasible) rather than overlay content. The strategy is a single documented choice; the geometry contract (never overlap columns) is what matters. This preserves navigation/selection and avoids permanent loss of the pane.
- **Dual-pane:** in dual-pane mode the reserved width applies to the combined viewport; each pane's content width derives from the remaining `availableColumnWidth`, split per existing dual-pane rules. The details pane is shared (single card) as today.
- **DPI (100%/150%):** all widths use DIP units via `UiMetrics` / effective-DPI scaling; the 300-DIP nominal width and the minimum-width threshold scale with DPI as the rest of the token system does.
- **Live resize / pane open-close / selection change:** any change recomputes `availableColumnWidth` and re-lays out; the pane shows content based on the current selection. No idle repaint loop: only trigger re-layout/repaint on actual geometry or state change (reuse the existing dirty-marking + `RequestRepaint` fan-out; honor `SystemAnimationsEnabled()`/High-Contrast as the rest of the UI does).
- **Ownership:** `WindowShell` owns the overall layout (it owns `RenderDetails` and the column-view frame); an explicit pure geometry helper computes `{availableColumnWidth, detailsRect, columnClipRect}` from `(viewportSize, paneVisible, dpiScale)` so it is unit-testable (no D2D needed). `ColumnView` consumes `availableColumnWidth`/clip instead of the raw viewport.
- **Alternatives considered and rejected:** (a) Overlay with a dim/occlusion — rejected: still hides interactive columns. (b) Modal drawer — rejected: heavier interaction model than the product needs. (c) Auto-stacking pane below content — rejected: vertical stacking is unusual for a details pane and would require a parallel layout path.

### D4: Service-account documentation consistency

All repository guidance that describes the `FastFilesIndexSvc` account SHALL state the LocalSystem constrained-broker model consistently: LocalSystem as the constrained privileged broker, raw MFT/USN scanning implemented, degraded path the active and safe production path while pins/privileged-path validation are incomplete, and the LocalSystem blast radius deliberate and not minimized.

**Why:** `CLAUDE.md:41-42` and `CODE_INDEX.md:148-149` still assert the superseded virtual-account model, contradicting the corrected `AGENTS.md`, `design.md` (D4), implementation, and validation finding LOW-20260804-v7k4p2-001.

- **Approach:** Update the stale lines to mirror `AGENTS.md`; superseded statements are allowed only with an explicit "superseded/historical" marker. Acceptance is a repository-wide search check (see spec `service-account-documentation-consistency`).
- **Alternatives considered and rejected:** Deleting the files affected — rejected: they are living guidance; consistency is the requirement, not removal.

### D5: Pinned-signature provisioning, rotation, and constant-time comparison

Real engine and service Authenticode thumbprints are provisioned only in a controlled release build, kept out of source control, and applied to `PinnedSignatures.h` (or an injected/config build-time source). Absent/placeholder pins keep `VerifyPinnedSignature` failing closed. Rotation is a controlled release-step that updates pins and revalidates; stale self-detection already exists for binary drift.

**Why:** The placeholder all-zero pins (`PinnedSignatures.h:16-17`) and fail-closed behavior (`AuthenticodeVerification.cpp:77-79`) are the current correct security posture; provisioning must not weaken it. Non-constant-time comparison (`AuthenticodeVerification.cpp:81` `*actual == expected`) is a hardening gap (LOW-20260804-K7P2VN-002) directly tied to closing the release gate.

- **Approach:** (a) Constant-time comparison helper for the fixed 20-byte thumbprint. (b) A documented provisioning/rotation procedure that never commits private material. (c) Negative tests: unsigned, mismatched, placeholder, and rotated-pin binaries rejected. (d) Fail-closed unchanged when pins absent; `FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED` stays compile-time-only and never in a shipped binary.
- **Alternatives considered and rejected:** (a) Committing real thumbprints to source — rejected: couples releases to source history and complicates rotation. (b) Removing fail-closed to make the privileged connection active during development — rejected: violates the security invariant and is explicitly disallowed.

- **Rotation is single-pin, no dual-acceptance window:** At any given moment exactly one thumbprint is pinned per peer. Rotation is the transactional operation "update the pin source → redeploy/restart → old peers now fail closed." Within a single connection's lifetime the pins are static; rotation surfaces as a new generation of the pinned source and a restart. There is deliberately no runtime state where both old and new certificates are simultaneously trusted — a dual-acceptance window would expand the attack surface (a leaked old cert would remain valid through the window) and is out of scope. If a rolling deployment needs zero-downtime rotation, the mechanism is sequenced deployment (new cert + new pins deployed together), not dual pinning.

## Risks / Trade-offs

- **[Risk] Details-pane layout regressions (overlap persists in some path)** → Mitigation: the geometry helper is pure and unit-tested across widths incl. narrow/dual-pane/DPI; the interactive visual matrix is a required task.
- **[Risk] Misleading percentages if the parent aggregate is stale or zero** → Mitigation: when the parent aggregate is zero or `Pending`/`NotFound`, "% of Parent" renders "—"; the existing `HandleAggregateResult` invalidation path re-renders in place when it resolves.
- **[Risk] Certificate rotation failure disabling auth** → Mitigation: rotation is a controlled release procedure that updates pins and revalidates; absence of pins keeps fail-closed rather than opening the boundary.
- **[Risk] Accidental weakening of authentication while wiring provisioning** → Mitigation: constant-time comparison and fail-closed behavior are carved out as invariants with negative tests; the `FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED` escape hatch stays compile-time-only and is never enabled in a shipped binary.
- **[Risk] Sorting determinism across equal percentages** → Mitigation: stable secondary tie-break key; tested with equal and nearly-equal synthetic values.
- **[Risk] Repository-hygiene decision removes a needed artifact** → Mitigation: explicit per-item decision (remove/ignore vs. rename/retain/move) with rationale and a follow-up that the documented build still produces the binary out-of-tree.

## Migration Plan

- Rolling change: percentage semantics and details-pane layout are UI-affecting; implement behind the existing surfaces with unit tests first. No schema change, no wire-protocol change, no installer change. Rollback = revert the UI/source delta while keeping the external-validation procedures documented (they do not roll back product behavior).
- The privileged-path release-gate closure is procedural/evidence-based, not a code migration; it gates a future privileged release, not the degraded-mode release.

## Open Questions

- Exact minimum-width threshold value for pane collapse — set a documented default in the geometry helper and confirm in the visual matrix.
- Whether to use `__int128` cross-multiplication or `double`-with-epsilon for the sort comparator — settled in implementation, unit-tested for overflow; either is acceptable and both are documented here.
- Retention of `Audit.txt` and `dist/FastFiles.exe` — decided per item in tasks (remove/ignore vs. rename/retain/move).

- No population or modification of previously empty audit-session directories, and no alteration of the eight validation reports under `codebase-audit/sessions/20260804-180500-v7k4p2/`.
