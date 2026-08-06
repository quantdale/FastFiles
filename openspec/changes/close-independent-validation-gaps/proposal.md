## Why

Independent validation session `20260804-180500-v7k4p2` confirmed the FastFiles implementation changes (Items 1–4, M4, L7, L10, L1) are correct and that the `/W4 /WX` debug build is clean with **27/27** CTest passing. That same validation surfaced a small set of *remaining* gaps that are now the only confirmed work items: a real functional defect in Storage Analysis' "% of Parent" (`MED-20260804-v7k4p2-006`), a responsive-layout defect in the details pane (`LOW-20260804-v7k4p2-007`), stale service-account documentation (`LOW-20260804-v7k4p2-001`), the external privileged-path release blockers (placeholder Authenticode pins, non-constant-time compare, pending elevated/clean-host/interactive validation), and a repository-hygiene decision (a committed binary and an audit-prompt artifact). This change closes the confirmed functional/documentation/security-readiness gaps and drives the release-gate closures to evidence, without weakening the degraded-mode path or the existing security boundaries.

## What Changes

- **Fix Storage Analysis "% of Parent"** (`MED-20260804-v7k4p2-006`): redefine percentage-of-parent so the denominator is the complete size of the immediate parent (files + directories); give file rows and directory rows correct values; handle zero/unavailable parents and zero-size children; add deterministic, overflow-safe percentage sorting that never overflow-cross-multiplies two `uint64_t` values; define root-row behavior; add unit tests that require no live Direct2D desktop.
- **Make the details pane layout responsive** (`LOW-20260804-v7k4p2-007`): reserve details-pane width before computing available column-view width so column content and interactive elements never render underneath the pane; select and document a narrow-width strategy; preserve dual-pane, DPI (100%/150%), live-resize, pane open/close, selection-change, clipping, and hit-testing; avoid idle repaint loops and animation regressions; preserve high-contrast and reduced-motion behavior.
- **Reconcile stale service-account documentation** (`LOW-20260804-v7k4p2-001`): make `CLAUDE.md` and `CODE_INDEX.md` consistently state that `FastFilesIndexSvc` runs as LocalSystem as the constrained privileged broker, that raw MFT/USN scanning is implemented, that the degraded path is the active and safe production path while pins and privileged-path validation are incomplete, and that the LocalSystem blast radius is deliberate and must not be minimized. Superseded statements remain only when explicitly marked historical.
- **Define privileged-path release-gate closure** (external blockers): a secure pin-provisioning and rotation strategy that keeps private material out of source control, keeps fail-closed behavior when pins are absent, adds constant-time thumbprint comparison, and defines the signed-install, service-start/connection-state, end-to-end scanning, negative-authentication, `fftest` diagnostics, protocol-fuzz, and clean-host matrix evidence gates. This is an *external/authorized-environment* closure plan; it does not claim these validations can be completed in a normal non-elevated session.
- **Resolve repository hygiene** (Low, explicit decision per item): decide the fate of the committed `dist/FastFiles.exe` build artifact and the `Audit.txt` audit-prompt artifact — treat each on its own merits (remove/ignore vs. rename/retain/move), not as an assumption that both must be deleted.

### Explicitly excluded (already resolved, contradicted, or unrelated)

- Items 1–4 and M4, L7, L10, L1 from the supplied implementation summary — confirmed correct; **not** re-worked.
- Test count figures "26/26", "Test #26", "previously 25" — correct result is **27/27, Test #27, previously 26**; the reporting error is *resolved*, not a code defect.
- The "uncommitted working tree" statement — the changes are committed in `216d931`; resolved.
- L5 metric consolidation — metrics already alias `UiMetrics`; no work.
- M5 dark-theme implementation as a code defect — only a *visual* verification remains (static evidence indicates it may already be addressed); no code change unless current code proves otherwise.
- The `cmake --preset analyze` link failure as a source defect — attributed to a mixed host toolchain (compile + `/analyze` succeeded); toolchain normalization is a separate, optional follow-up, not a code change here.
- Previously empty audit-session directories — not populated, not modified.

## Capabilities

### New Capabilities
- `responsive-details-pane-layout`: responsive contract for the details pane so column content and interactive elements never render underneath it; covers width reservation, narrow-width strategy, dual-pane, DPI, live resize, clipping/hit-testing, and accessibility.
- `pinned-authentication-provisioning`: how real engine/service thumbprints are provisioned and rotated securely, how fail-closed and constant-time comparison hold, and the privileged-path release-gate evidence procedures (signed install, service-start/connection-state, end-to-end scan, negative auth, `fftest` diagnostics, protocol fuzz, clean-host matrix).
- `service-account-documentation-consistency`: repository-wide documentation requirement that every description of the privileged service account match the LocalSystem constrained-broker model, with a search-based acceptance check.

### Modified Capabilities
- `storage-drilldown-and-treemap` (from `storage-analysis`): correct "% of Parent" semantics and denominator, file/directory/zero/root row behavior, and overflow-safe percentage sorting. This is a spec-level behavior change to the existing "Hierarchical Drill-Down with Percentage Context" requirement.

## Impact

- **Affected code:** `src/ui/src/StorageAnalysis.cpp` and `.h` (percentage semantics, denominator, sort comparator), `src/ui/src/WindowShell.cpp` and `ColumnView.*` (details-pane width reservation / layout), and tests under `tests/ui/`. Documentation: `CLAUDE.md`, `CODE_INDEX.md`. Security: `src/setup/include/ffsetup/PinnedSignatures.h`, `src/setup/src/AuthenticodeVerification.cpp`, `tests/engine/test_authenticode_verification.cpp`. Repository-hygiene decision touches repo layout only.
- **Dependencies (declared, not duplicated):**
  - `establish-architecture-foundation` — authoritative security model (D4 symmetric mutual auth, closed command protocol) referenced for all pin/fail-closed work.
  - `resolve-raw-volume-privilege-insufficiency` — the LocalSystem constrained-broker decision and its evidence matrix; the service-account documentation must match it.
  - `storage-analysis` — owns `storage-drilldown-and-treemap`; this change modifies that capability's percentage requirement.
  - `modernize-ui-appearance` — owns the details-pane visual surface/tokens; the responsive layout builds on it without re-skinning.
  - `autonomous-runtime-verification` — owns the validation capabilities (`windows-install-and-service-validation`, `windows-privilege-and-ipc-validation`, `windows-ui-automation-validation`, `windows-build-validation`) the release-gate closure references.
  - **Interaction (flagged, not modified):** the new `responsive-details-pane-layout` spec adds a "Details Pane Works With Dual-Pane Mode" requirement that specifies how dual-pane mode consumes the remaining width after the details pane is reserved. That requirement references, but does not modify, the `dual-pane-mode` capability owned by `navigation-and-workspace` (which explicitly treats column-view geometry and hit-testing as a black box). The proposal notes the interaction here per blast-radius disclosure; modifying `dual-pane-mode` itself is out of scope. Reviewers should confirm the new requirement does not contradict any `navigation-and-workspace` requirement before this change is archived into the merged spec.
- **Release-blocker dependency:** completing the privileged-path release gate requires a provisioned code-signing certificate, an authorized elevated host, an interactive Windows desktop for visual validation, and a clean-host/privilege-candidate-matrix environment. A normal non-elevated development session cannot complete these. Degraded-mode release is independent and unaffected.
- **Effects on readiness:** degraded-mode remains releasable and unaffected; privileged-path readiness advances only as the external blockers are closed with evidenced runs.

## Open Questions

- Exact narrow-width strategy for the details pane (resize vs. collapse vs. hide vs. stack vs. temporary overlay) — a single strategy is selected and documented in design; the geometry contract is definitive and visual validation confirms it.
- Ownership of the committed `dist/FastFiles.exe` and `Audit.txt` — resolved as an explicit decision per item in this change, not assumed.

## Non-Goals / Follow-Up (do not implement here unless scoped)

Related previously-reported findings that inspection confirms are still open but are *not* part of closing these validated readiness gaps; tracked as follow-ups so this change does not balloon into a repository-wide cleanup:
- Query-history/query-input length limit (`LOW-20260804-K7P2VN-001`) — follow-up.
- Explicit USN journal cleanup during shutdown (`INFO-20260804-K7P2VN-002`) — follow-up.
- IPC concurrency / partial-read / broken-pipe tests (INFO-004 in prior audit) — follow-up test-hardening change.
- Selection-model test depth (INFO-005 in prior audit) — follow-up test-hardening change.
- Same-SQLite-file concurrency coverage — follow-up test change.
