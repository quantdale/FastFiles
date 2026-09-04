# Design: External Certification Execution Engine and Frontier Truthfulness

## 1. Execution-truth principle

A runner is execution-ready iff: with every in-scope prerequisite supplied,
executing its mapped command performs the task's required procedure and yields
enough evidence to decide PASS/FAIL. Existence, parsing, launch, preflight
inspection, and structurally-valid JSON are necessary but never sufficient.

Mutation test for every new certification test: delete the execution step, leave
only a PASS object — the test must fail. Behavioral verification only; no
checkbox tests (function-name/parameter/field existence alone).

Owner scope directive (2026-09-04): vendor-specific VM-provider execution and
testing are out of scope for FastFiles local completion. Generic provider
state-machine, provenance, cleanup, and fake-adapter contracts remain useful,
but no specific VM provider, guest image, guest credential path, or
provider-specific end-to-end execution is a local completion gate unless the
owner explicitly reopens that scope.

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

## 3. Generic provider lifecycle state machine

14 phases: DISCOVER, PREFLIGHT, PROVISION, ACTIVATE, WAIT_READY,
ESTABLISH_SECURE_GUEST_CHANNEL, TRANSFER_PAYLOAD, EXECUTE_PAYLOAD, COLLECT_LOGS,
COLLECT_STRUCTURED_RESULTS, VERIFY_RESULT_PROVENANCE, RESTORE/ROLLBACK, CLEANUP,
RESIDUE_CHECK. Phase record: start/end UTC, status, reason, error, provider
identity, guest identity, artifact refs, cleanup result. Terminal
classification for an explicitly invoked external adapter:

* any EXECUTE-path phase failure => FAIL (with phase attribution);
* preflight-only success => REQUIRED-BUT-UNAVAILABLE, disposition
  BLOCKED_EXTERNAL — never PASS;
* provenance mismatch / malformed result / missing execution proof => FAIL;
* cleanup/residue failure => FAIL (visible, never masked by test success).

Seam: `-ProviderAdapter` (scriptblock/module) plus deterministic fake fixture
covering success, preflight-unavailable, provision/activation/channel/transfer/
execution/collection failures, malformed result, provenance mismatch, timeout,
rollback failure, cleanup failure, cancellation/interruption.

This generic state machine is infrastructure, not a requirement to implement or
test a specific VM provider. Provider-specific provisioning/channel/guest
execution/rollback validation remains outside FastFiles local completion scope
by owner direction.

Provenance record for adapters that are actually executed: host commit+tree SHA,
payload digest, candidate digest, provider, VM/guest identity, guest OS, guest
timestamp, executable hash, command, schema version, result artifact hash, test
counts, exit code, cleanup disposition.

## 4. NTFS ground-truth leg (read-only raw rule)

Preserve: elevation check, filesystem-type check, read-only raw handle
acquisition, handle closure. Add: fixture leg creating campaign-owned files/dirs
via normal Win32 APIs, capturing normal-filesystem truth (names, sizes, parent
relationships, file IDs, stable timestamps), querying the same objects through
the privileged/raw path, comparing field-by-field with per-field verdicts.
Raw-open alone, or parser unit tests alone, cannot yield PASS. Unreliable fields
are documented per-field, never silently weakened. Seams (volume-open,
privilege/fs detection, MFT/USN reader, fixture-truth provider) enable
unelevated deterministic contract tests. Layers: A parser tests (existing), B
runner contract tests (fakes), C local elevated integration (where available),
D clean/elevated external execution (never inferred from A–C). Capabilities:
raw-volume-read on handle open; mft-ground-truth / usn-ground-truth only on
executed+matching fixture comparison.

## 5. Capability-aware frontier contract

Task record gains: certificationLane, executionMode, scenarioIds,
requiredDimensions/requiredEnvironmentMatrix, requiredAssertions,
requiredArtifacts, minimumIterations, resultSchema, successPredicate,
operatorInputs, cleanupRequirements, plus requiredCapabilities[] from the
in-scope vocabulary (uia-core-smoke, uia-keyboard-navigation,
visual-light-theme, visual-dark-theme, dpi-100, dpi-150, high-contrast,
window-narrow, window-wide, dual-pane, details-pane, storage-parent-percent,
production-signing, clean-install, installed-runtime, uninstall-cleanup,
raw-volume-read, mft-ground-truth, usn-ground-truth).

Generic provider capability names may remain in implementation metadata for
backward compatibility, but vendor-specific provider capabilities are excluded
from local completion accounting.

Each applicable runner emits `capabilities[]` for what the invocation actually
proved. Inventory validation for in-scope tasks requires runner capabilities ⊇
required capabilities; mismatch => inventory FAIL naming task, missing
capability, command. UIA runner accepts explicit requested dimensions (theme,
DPI, contrast, viewport, pane topology, details pane, animations) and records
requested-vs-observed per permutation; unsupported/unrealized dimensions are
classified honestly, never assumed. Task-required matrix derives the minimum
permutation set (no brute force).

## 6. UIA/soak semantics

Rapid-input: dual-pane on => pane-count==2 ∧ Pane.0 ∧ Pane.1; off => count==1 ∧
Pane.0 ∧ ¬Pane.1. Search => opened ∧ Filters located/expanded/verified ∧ closed ∧
absence-verified. Report strings equal asserted predicates. Artifacts retain:
iterations, compressed per-iteration ledger, phase durations, first failure,
assertion counts, cleanup, orphan census, semantic postconditions.
Soak: private-bytes gate independent of working set (slope + baseline→final +
half-median split, documented threshold; regression: WS-stable + PB-leak => FAIL).
Modes: Idle (functionalActivity=SKIPPED, valid), InteractiveActivity (all
transitions with open+disappearance/restoration assertions; required-transition
failure fails the leg), HeadlessSemantic only for genuine non-UI product
workloads. Cleanup is a first-class pass-condition in every in-scope runner
(processes, services, temp dirs, sessions, fixtures, handles, installer state).

## 7. Status truth model

Per-task readiness for in-scope certification work: NOT_IMPLEMENTED (local
machinery incomplete → local work), READY_EXTERNAL (runner complete+
contract-tested, only external capability missing), EXECUTED (ran in real
environment with valid evidence). Status generation maps:
external+incomplete-runner => active local open; external+complete-runner =>
external-only; executed+valid => completed where OpenSpec permits.

Vendor-specific VM-provider execution/testing is owner-excluded and must not
surface as active local completion work solely because a provider-specific path
is absent or unexecuted.

## 8. Evidence plan

Behavioral fail-closed regression tests; generic provider failure-permutation
matrix; soak PB-leak fixture; capability-mismatch fixtures; long validation
block (Debug ×3, release-test ×2, cert tests, UIA lifecycle ×100 / rapid ×50,
10-min idle soak, IPC handshake/framing ×100, UiServer ×25); exact-tree
regeneration after commit; hosted CI on final SHA.
