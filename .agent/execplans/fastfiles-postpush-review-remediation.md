# FastFiles Post-Push Review Remediation ExecPlan

## Objective

Resolve FF-POST-01 through FF-POST-05 from the latest independent review,
including the underlying installer trust, release identity, USN metadata, and
named-pipe handle-lifetime defects; add adversarial regression coverage; make
hosted CI portable; run the complete local and hosted validation matrix; and
leave a durable, non-self-referential handoff. The final handoff correction is
FF-POST-12: tracked state must describe validated logical/history state while
deriving live Git and CI state at resume time. Preserve fail-closed security,
degraded-mode operation, the three-process boundary, and unrelated user work.

The bounded adjacent audit also tracks FF-POST-06 through FF-POST-11. FF-POST-06
is a semantic-version validation inconsistency found while reviewing the new
release-identity boundary; FF-POST-07 is an explicit null-buffer parser gap;
FF-POST-08 is an ASAN warning-level configuration conflict; FF-POST-09 is an
old-protocol compatibility gap exposed by the new V2 USN message; and
FF-POST-10 is a release-kit/package-root boundary gap exposed by the exact
installer inventory; FF-POST-11 is the pending-accept cancellation race found
during the fresh final audit. Each is resolved only when its implementation and
regression evidence are complete.

## Scope and source of truth

- Campaign state: `.agent/workstate/fastfiles-postpush-review-remediation-state.md`
- Historical evidence: `.agent/workstate/fastfiles-audit-remediation-state.md`
- Historical implementation plan: `.agent/execplans/fastfiles-audit-remediation.md`
- OpenSpec context: `openspec/changes/close-independent-validation-gaps/`
- Architecture authority: `openspec/changes/establish-architecture-foundation/design.md`
- The five findings in the user mission are the authoritative campaign matrix;
  FF-POST-06 through FF-POST-11 are bounded adjacent regression findings, and
  FF-POST-12 covers the durable-state protocol and final handoff.

Tracked state is not a live repository database. A workstate may record a
validated implementation baseline and CI evidence bound to that SHA, but it
must not embed the SHA of the commit that contains the workstate or claim that
the containing commit has the latest green CI. Resume-time Git facts and the
latest hosted run are derived by commands in the workstate and queried
externally.

## Current frontier

The reviewed implementation baseline is
`f9ea9948a65f06aa1366ce488ca38db1c76fdbf3`; the final hosted evidence bound
to that implementation is run `31720435471`, green for Debug, Analyze,
Release-test, and ASAN protocol fuzz. That SHA is a historical validation
anchor, not the SHA of this ExecPlan or its companion workstate. The fresh
audit found no new locally actionable production defect. This handoff slice
addresses FF-POST-12 by repairing the workstate, stating the
non-self-referential resume contract in the plan/guidance, validating the
fresh-agent handoff, and completing the clean commit/push/hosted-CI boundary.
The next major phase is external: authorized production signing/pins,
elevated clean-host install/service/MFT/USN/uninstall evidence, and rendered
UI validation.

## Guardrails

- Never add a generic privileged path/handle operation or weaken pinned
  authentication, package confinement, protocol bounds, or production
  fail-closed behavior.
- Do not use path-only Authenticode verification in the production package
  trust path. The object verified must be the object copied.
- Do not fabricate production signing, elevated clean-host, rendered UI, or
  hosted-CI evidence. Keep genuinely unavailable external evidence distinct
  from implementation and local validation.
- Preserve unrelated work and never force-push or rewrite existing history.
- Update the campaign workstate before/after each meaningful frontier change,
  after local tests/builds, before long validation, and before ending. After a
  final push, do not edit tracked state merely to record that push's hosted
  run; report the live result in the session response and derive it again on
  the next resume.
- Live HEAD/origin/dirty state and latest CI are derived at resume time. Never
  repair a handoff by adding the SHA of the commit that contains the repair;
  never add a recursive “latest CI for this handoff file” claim.
- Before ending, validate, commit all session-owned tracked state, fetch and
  safely reconcile, push `main`, verify local/remote equality, inspect hosted
  CI for the pushed SHA, and leave the tree clean. “Intentional workstate
  dirty” is not an acceptable exit state.

## Execution slices

1. Reconcile repository state, read applicable instructions and architecture,
   create this plan/state pair, and reproduce each finding.
2. FF-POST-01: redesign package signature verification around the already-open
   source handle, protect its trust/copy lifetime, and add deterministic
   substitution/race/reparse/hash/signature regression coverage.
3. FF-POST-02: define and implement an authenticated, versioned, exact package
   identity with component-role metadata, strict inventory/version policy,
   anti-mixing and anti-downgrade checks, release-kit generation/validation,
   and negative tests. Reconcile this design with FF-POST-01.
4. FF-POST-04: version the USN delta representation or equivalent explicit
   field-presence model, preserve successful MFT enrichment for new rows, and
   test store/projection/snapshot/restart/reconciliation/fallback behavior.
5. FF-POST-05: replace raw shared HANDLE registries with explicit ownership and
   synchronized cancellation state, cover pending accept and callback teardown,
   audit EngineClient lifetimes, and add deterministic/stress lifecycle tests.
6. FF-POST-03: remove brittle Visual Studio/Ninja assumptions from presets and
   GitHub Actions, use normal CMake/toolchain discovery or dynamic vswhere
   discovery, and validate every required hosted job.
7. Perform bounded adjacent audits of security, release, USN, and IPC paths;
  fix any newly discovered locally actionable defect and extend the matrix.
  The current adjacent defects are FF-POST-06 (strict-version alignment),
  FF-POST-07 (explicit null-buffer parser rejection), FF-POST-08 (ASAN
  preset warning-level conflict), FF-POST-09 (legacy USN wire-format
  negotiation), FF-POST-10 (release-kit package-root separation), and FF-POST-11
  (pending accept cancellation initiation); do not close them until the
  regression tests and final build matrix are green.
8. Run focused tests, all supported Debug/Release-test/Analyze/Release/ASAN,
   verifier/doctor, release tooling, OpenSpec, drift, and hygiene gates.
9. Freshly review the complete campaign diff for security/concurrency/CI
   regressions, commit coherent slices, safely push `main`, monitor GitHub
   Actions to green, and synchronize this handoff.
10. FF-POST-12: rewrite the tracked state around logical/history evidence,
    derive live repository/CI facts at resume time, reconcile this ExecPlan
    with the state, and perform the final clean commit/push/CI handoff without
    self-referential hashes.

## Validation commands

Use the repository-supported commands discovered from `AGENTS.md`,
`CMakePresets.json`, and the verification scripts. The implementation baseline
already has exact evidence for focused setup/protocol/index/IPC tests; Debug
and Release-test CTest; Analyze and production Release builds where pins
permit; ASAN protocol fuzz; `verify/verify.ps1 build` and `doctor`; release
tooling; strict OpenSpec; drift; `git diff --check`; and hosted run
`31720435471` with all required jobs green. Because FF-POST-12 changes only
tracked guidance/state, validate the new documents with diff/hygiene,
documentation drift, source-policy, PowerShell/JSON/Markdown parsing as
applicable, strict OpenSpec, and the actual post-push hosted run. Do not claim
that the baseline executable matrix was rerun for a docs-only correction.

## Commit policy

Prefer focused imperative commits by coherent theme, for example:

- `security: bind package signer verification to source handle`
- `security: authenticate release package identity`
- `fix: preserve enriched metadata for new USN entries`
- `refactor: make pipe cancellation handle ownership race-free`
- `ci: discover Windows build toolchain dynamically`
- `test: add adversarial post-push regression coverage`
- `docs: close post-push remediation campaign`

Do not create checkpoint-only commits; durable state records intermediate
progress. Before every commit, confirm status, diff scope, and `git diff
--check`. A state file may record prior baseline hashes and commit-bound
historical CI, but must not record the hash of the commit that contains the
state file. The session response records that final pushed hash instead.

## Exit criteria

FF-POST-01 through FF-POST-11 are `VALIDATED` with adversarial tests and exact
evidence; FF-POST-12 is `VALIDATED` when the state/plan/guidance model is
non-self-referential, the fresh-agent acceptance passes, the docs-only gates
are green, the correction is pushed, the remote is verified, and the hosted
run for that pushed SHA is inspected. No unrelated changes may be present.
The tracked handoff must state the exact external production-signing,
clean-host, and rendered-validation phase without embedding its own final SHA.
