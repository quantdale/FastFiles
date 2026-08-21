## Context

The repository already has a mature PowerShell verification harness and release primitives under `verify/release/`: package manifests, Authenticode/pin validation, clean-host preflight/import, and a privileged driver. The missing layer is a product-acceptance campaign that can be repeated by an ordinary Windows user and a single, discoverable candidate command that composes existing release primitives without weakening their fail-closed behavior. The campaign must work primarily in degraded/unprivileged mode because current pins are placeholders and no production certificate or clean-host VM is available.

## Goals / Non-Goals

**Goals:**

- Make disposable filesystem fixtures and independent filesystem truth reusable by manual dogfood, PowerShell acceptance checks, and future UIA scenarios.
- Record structured acceptance evidence and observed findings without treating unavailable external prerequisites as passes.
- Add bounded performance/resource measurements that are comparable across runs and safe to stop.
- Compose existing build/test/version/package/signing checks into a deterministic release-candidate workflow with explicit unsigned-versus-production classification.
- Provide maintainable release notes, supported-matrix guidance, and a clean-host handoff.

**Non-Goals:**

- Activating the privileged MFT/USN path, changing peer authentication, changing pins, mutating trust stores, or fabricating signing material.
- Replacing the existing installer, protocol, snapshot architecture, or UI automation provider.
- Claiming GUI, clean-host, network, removable-volume, or production-signed evidence when the host cannot execute it.
- Building a large general-purpose test framework or committing generated fixture data.

## Decisions

### D1. Keep campaign data outside the repository

The fixture generator will accept an explicit disposable root, write a deterministic manifest and metadata beside the generated tree, and refuse ambiguous/broad roots. This lets destructive file-operation tests reset safely and keeps the repository small.

**Why:** Real file-operation acceptance needs realistic names, attributes, and scale, but generated files are not source and must never be confused with user data.

**Alternative considered:** Commit a canned fixture tree. Rejected because it is large, cannot represent scale, and makes destructive tests unsafe and non-repeatable.

### D2. Use independent filesystem manifests as the oracle

Acceptance operations will snapshot or compare paths, file bytes, sizes, attributes, and expected absence/presence through PowerShell/Win32 helpers that do not consult FastFiles' model. UI success text is evidence of what the product claimed, not proof of filesystem truth.

**Why:** A file manager must not report success when the filesystem disagrees; independent observation catches both UI and operation-layer defects.

**Alternative considered:** Assert only UI rows and status messages. Rejected because stale snapshots or optimistic Shell callbacks can hide data-integrity failures.

### D3. Layer acceptance around existing semantic seams

Stable scenarios will use the existing UIA driver/provider surfaces and headless CTest seams. Scenarios requiring an unavailable service, signed peers, external Explorer drag source, or interactive system setting will record named unavailable evidence. No hidden test hooks or coordinate-only automation will be added.

**Why:** This preserves the UIA contract and makes automation useful without making the product lie about host capabilities.

**Alternative considered:** Expand pixel/coordinate automation until every scenario runs. Rejected as brittle and contrary to the repository's semantic-uia conventions.

### D4. Add a thin release-candidate orchestrator over existing release primitives

The new entry point will validate clean Git state and version/configuration, invoke the existing build/test and packaging functions/scripts, stage artifacts in a fresh explicit output directory, and emit manifest/checksum/provenance files. It will not duplicate package trust or pin logic already centralized in `ReleaseCommon.psm1`.

**Why:** The repository already has strong release pieces; the user-facing gap is discoverability and composition, not another packaging format.

**Alternative considered:** Rewrite `New-ReleaseKit.ps1` or create a separate installer packager. Rejected because it would duplicate security-sensitive logic and increase drift.

### D5. Separate preparation from production authorization

Unsigned release-test/development artifacts may be staged only with explicit non-production labels. A production request requires the existing certificate/pin inputs and fails before publishing if they are absent, placeholder, malformed, or mismatched. Signing and clean-host evidence remain imported external steps.

**Why:** This is the existing security invariant and prevents a convenient dogfood path from becoming a trust bypass.

**Alternative considered:** Let the candidate pipeline auto-sign with a generated/self-signed certificate. Rejected because it would not represent production trust and could contaminate trust stores.

### D6. Record campaign evidence in tracked summaries, not raw run dumps

Tracked documents will contain scenario IDs, measured summaries, commit/build identity, blockers, and finding references. Large logs, screenshots, generated fixtures, and sensitive signing material remain untracked or external; no credentials enter workstate or campaign docs.

**Why:** Evidence must survive compaction and review without turning Git into an execution database or leaking user data.

## Risks / Trade-offs

- [Risk] Windows UI and service prerequisites vary across hosts → Mitigation: every scenario records environment requirements and bounded unavailable states; degraded-mode scenarios are the portable baseline.
- [Risk] A fixture reset could target user data → Mitigation: require an explicit root under a caller-owned disposable directory, write a marker/manifest, and validate the resolved target before destructive cleanup.
- [Risk] Release metadata can drift from binary resources → Mitigation: make `VERSION` canonical and validate all staged executable metadata plus manifest/release-note values before packaging.
- [Risk] Long performance/soak runs can consume host resources → Mitigation: use explicit item/time budgets, periodic samples, cancellation, and no default million-file generation.
- [Risk] Acceptance automation can become a second product implementation → Mitigation: keep scenarios thin, route actions through existing UIA/product seams, and prefer headless tests for pure behavior.
- [Risk] Unsigned preparation could be mistaken for release readiness → Mitigation: deterministic names, manifest fields, release notes, and command output explicitly state `unsigned-development`/`unsigned-test` versus `production-signed`.

## Migration Plan

1. Add the campaign OpenSpec artifacts, documentation, and disposable fixture/acceptance tools without changing existing product behavior.
2. Add focused tests for fixture safety, independent manifest comparison, release manifest/checksum determinism, and fail-closed signing/version checks.
3. Run the local degraded-mode acceptance and candidate pipeline; record actual metrics and external blockers.
4. Integrate any evidence-backed product fix as a separate logical commit with a regression test, then rerun the focused and full gates.
5. Rollback is file-level: remove the new campaign scripts/docs/tests or stop using the candidate entry point; existing `verify/release` behavior and trust policy remain unchanged.

## Open Questions

- Whether a future release owner wants an SPDX/CycloneDX SBOM emitted by the candidate command can be decided after the dependency inventory is measured; the manifest/checksum/provenance contract does not depend on it.
