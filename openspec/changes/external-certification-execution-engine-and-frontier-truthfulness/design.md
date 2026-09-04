# Design: External Certification Execution Engine and Frontier Truthfulness

## 1. Execution-truth principle

A runner is execution-ready iff: with every prerequisite supplied, executing its
mapped command performs the task's required procedure and yields enough evidence to
decide PASS/FAIL. Existence, parsing, launch, preflight inspection, and
structurally-valid JSON are necessary but never sufficient.

Mutation test for every new certification test: delete the execution step, leave only
a PASS object — the test must fail. Behavioral verification only; no checkbox tests
(function-name/parameter/field existence alone).

## 2. Clean-host contract

```
AuditOnly (default, no -InstallCandidate):
  checks: os-compat, elevation, host-conflict-audit, candidate-provenance(SKIPPED)
  terminal: AUDIT_READY | BLOCKED_ENVIRONMENT | BLOCKED_NOT_ELEVATED |
            REQUIRED_BUT_UNAVAILABLE. NEVER product PASS.
InstallCertification (-InstallCandidate -CandidatePath <p>):
  legs (each a separate check): artifact-exists, hash-captured, provenance,
  precondition-clean, elevation, invoke-install, exit-status, files-created,
  launch-from-installed-location, binary-identity, readiness, smoke,
  graceful-shutdown, orphan-census, uninstall-performed, uninstall-result,
  residue-inspection, classification. Rollback failure => FAIL/BLOCKED.
  No installer in repo => BLOCKED_MISSING_INSTALLABLE_CANDIDATE (honest).
  Portable/staged execution counts as installation ONLY with repo evidence that
  portable deployment is the supported model; staging dir is campaign-owned.
Guardrails: refuse targets outside campaign-owned temp/staged dirs; refuse on
contaminated host (BLOCKED_ENVIRONMENT); never uninstall/delete/stop foreign state.
Capabilities: install mode advertises clean-install, installed-runtime,
uninstall-cleanup; audit mode advertises none of them.
```

## 3. Provider lifecycle state machine

14 phases: DISCOVER, PREFLIGHT, PROVISION, ACTIVATE, WAIT_READY,
ESTABLISH_SECURE_GUEST_CHANNEL, TRANSFER_PAYLOAD, EXECUTE_PAYLOAD, COLLECT_LOGS,
COLLECT_STRUCTURED_RESULTS, VERIFY_RESULT_PROVENANCE, RESTORE/ROLLBACK, CLEANUP,
RESIDUE_CHECK. Phase record: start/end UTC, status, reason, error, provider identity,
guest identity, artifact refs, cleanup result. Terminal classification:

* any EXECUTE-path phase failure => FAIL (with phase attribution);
* preflight-only success (no guest image) => REQUIRED-BUT-UNAVAILABLE,
  disposition BLOCKED_EXTERNAL — never PASS;
* provenance mismatch / malformed result / missing execution proof => FAIL;
* cleanup/residue failure => FAIL (visible, never masked by test success).

Seam: `-ProviderAdapter` (scriptblock/module) with real Hyper-V default and fake
fixture covering: success, preflight-unavailable, provision/activation/channel/
transfer/execution/collection failures, malformed result, provenance mismatch,
timeout, rollback failure, cleanup failure, cancellation/interruption.
Hyper-V specifics: validate VHDX template, disposable differencing VM with unique
campaign name, never mutate source image, start/wait/channel/transfer/execute/
retrieve/stream, graceful stop, discard differencing state, remove campaign VMs,
residue check.

Provenance record: host commit+tree SHA, payload digest, candidate digest, provider,
VM/guest identity, guest OS, guest timestamp, executable hash, command, schema
version, result artifact hash, test counts, exit code, cleanup disposition.

## 4. NTFS ground-truth leg (read-only raw rule)

Preserve: elevation check, filesystem-type check, read-only raw handle acquisition,
handle closure. Add: fixture leg creating campaign-owned files/dirs via normal Win32
APIs, capturing normal-filesystem truth (names, sizes, parent relationships, file
IDs, stable timestamps), querying the same objects through the privileged/raw path,
comparing field-by-field with per-field verdicts. Raw-open alone, or parser unit
tests alone, cannot yield PASS. Unreliable fields are documented per-field, never
silently weakened. Seams (volume-open, privilege/fs detection, MFT/USN reader,
fixture-truth provider) enable unelevated deterministic contract tests. Layers:
A parser tests (existing), B runner contract tests (fakes), C local elevated
integration (where available), D provider clean/elevated execution (never inferred
from A–C). Capabilities: raw-volume-read on handle open; mft-ground-truth /
usn-ground-truth only on executed+matching fixture comparison.

## 5. Capability-aware frontier contract

Task record gains: certificationLane, executionMode, scenarioIds,
requiredDimensions/requiredEnvironmentMatrix, requiredAssertions,
requiredArtifacts, minimumIterations, resultSchema, successPredicate,
operatorInputs, cleanupRequirements, plus requiredCapabilities[] from the fixed
vocabulary (uia-core-smoke, uia-keyboard-navigation, visual-light-theme,
visual-dark-theme, dpi-100, dpi-150, high-contrast, window-narrow, window-wide,
dual-pane, details-pane, storage-parent-percent, production-signing, clean-install,
installed-runtime, uninstall-cleanup, raw-volume-read, mft-ground-truth,
usn-ground-truth, hyperv-provision, guest-execution, provider-rollback).
Each runner emits `capabilities[]` for what the invocation actually proved.
Inventory validation: task PASS-mapping requires runner capabilities ⊇ required
capabilities; mismatch => inventory FAIL naming task, missing capability, command.
UIA runner accepts explicit requested dimensions (theme, DPI, contrast, viewport,
pane topology, details pane, animations) and records requested-vs-observed per
permutation; unsupported/unrealized dimensions are classified honestly, never
assumed. Task-required matrix derives the minimum permutation set (no brute force).

## 6. UIA/soak semantics

Rapid-input: dual-pane on => pane-count==2 ∧ Pane.0 ∧ Pane.1; off => count==1 ∧
Pane.0 ∧ ¬Pane.1. Search => opened ∧ Filters located/expanded/verified ∧ closed ∧
absence-verified. Report strings equal asserted predicates. Artifacts retain:
iterations, compressed per-iteration ledger, phase durations, first failure,
assertion counts, cleanup, orphan census, semantic postconditions.
Soak: private-bytes gate independent of working set (slope + baseline→final +
half-median split, documented threshold; regression: WS-stable + PB-leak => FAIL).
Modes: Idle (functionalActivity=SKIPPED, valid), InteractiveActivity (all transitions
with open+disappearance/restoration assertions; required-transition failure fails the
leg), HeadlessSemantic only for genuine non-UI product workloads. Cleanup is a
first-class Pass-condition in every runner (processes, services, temp dirs, VMs,
checkpoints, mounts, sessions, fixtures, handles, installer state).

## 7. Status truth model

Per-task readiness: NOT_IMPLEMENTED (local machinery incomplete → local work),
READY_EXTERNAL (runner complete+contract-tested, only external capability missing),
EXECUTED (ran in real environment with valid evidence). Status generation maps:
external+incomplete-runner => active local open; external+complete-runner =>
external-only; executed+valid => completed where OpenSpec permits. Never force
active-local to zero; the system discovers truth.

## 8. Evidence plan

New regression tests (≥24 listed in campaign §26) as behavioral fail-closed tests;
provider full failure-permutation matrix; soak PB-leak fixture; capability-mismatch
fixtures; long validation block (Debug ×3, release-test ×2, cert tests, UIA
lifecycle ×100 / rapid ×50, 10-min idle soak, IPC handshake/framing ×100, UiServer
×25); exact-tree regeneration after commit; hosted CI on final SHA.
