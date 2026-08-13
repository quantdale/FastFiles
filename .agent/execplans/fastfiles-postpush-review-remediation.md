# FastFiles Post-Push Review Remediation ExecPlan

## Objective

Resolve FF-POST-01 through FF-POST-05 from the latest independent review,
including the underlying installer trust, release identity, USN metadata, and
named-pipe handle-lifetime defects; add adversarial regression coverage; make
hosted CI portable; run the complete local and hosted validation matrix; and
leave a durable handoff. Preserve fail-closed security, degraded-mode
operation, the three-process boundary, and unrelated user work.

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
  FF-POST-06 through FF-POST-11 are bounded adjacent regression findings.

## Current frontier

All five primary findings and bounded adjacent findings FF-POST-06 through
FF-POST-11 are implemented and validated. The final source tree is pushed at
`1f329bb3e195db1b003a99ab54ab5f5609314c0c`; hosted run `31718337068` is green
for Debug, Analyze, Release-test, and ASAN protocol fuzz. The fresh audit found
no new locally actionable defect. The remaining handoff is external:
authorized production signing/pins, elevated clean-host install/service/
MFT/USN/uninstall evidence, and rendered UI validation.

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
  before commits and pushes, after tests/builds/hosted runs, and before ending.

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

## Validation commands

Use the repository-supported commands discovered from `AGENTS.md`,
`CMakePresets.json`, and the verification scripts. At minimum record exact
results for focused setup/protocol/index/IPC tests; Debug and Release-test
CTest; Analyze and production Release builds where pins permit; ASAN protocol
fuzz; `verify/verify.ps1 build` and `doctor`; release tooling; strict OpenSpec;
drift; `git diff --check`; and the actual GitHub Actions run with all required
jobs green.

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
--check`; immediately record the resulting hash.

## Exit criteria

All five local findings are `VALIDATED` with adversarial tests and exact
evidence, all local validation gates are green, the required hosted CI run is
green, no unrelated changes are present, and the workstate states the exact
external production-signing/clean-host/rendered-validation handoff if those
remain unavailable.
