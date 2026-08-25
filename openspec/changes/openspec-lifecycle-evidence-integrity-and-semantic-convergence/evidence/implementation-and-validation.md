# Implementation and validation

This file is updated at handoff with exact implementation and hosted-CI
evidence. During implementation it also serves as the durable record target
for the prior hosted-run binding and remains tracked repository evidence.

Required final evidence:

- schema-v2 lifecycle model and live manifest pass structural and semantic
  validation;
- focused status/lifecycle fixtures pass with exact reason-code assertions;
- status and release-readiness projections agree and remain non-production;
- strict OpenSpec, Debug, release-test, Analyze, ASAN/applicable verifier,
  policy, doc-drift, and diff checks are recorded;
- final `main`/`origin/main` parity and exact hosted CI result are recorded
  without a self-referential SHA claim.

Implementation checkpoint (before the final handoff):

- The focused `verify/status/tests/Test-ProjectStatus.ps1` suite passes with
  exit 0, including exact reason-code checks for traversal, absolute, missing,
  junction/reparse, legacy string, non-integer schema version, Git, hosted-run,
  semantic, redaction, ordering, schema, and release fail-closed cases.
- `npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all
  --strict --no-interactive` passes 71/71. The diagnostic-policy source gate
  passes with zero failures, and `git diff --check` passes.
- `pwsh ./verify/verify.ps1 build` passes 9/9 sub-gates across Debug,
  release-test, and Analyze; Debug and release-test CTest are green and the
  Analyze build is warning-clean. The focused
  `windows-protocol-robustness` verifier passes all three protocol suites.
- Release-readiness composition also passed its Debug/release-test CTest,
  Analyze, protocol/ASAN, strict OpenSpec, policy, and doc-drift gates. Its
  overall result is intentionally not a release PASS while the worktree is
  dirty, unsigned/clean-host prerequisites are unavailable, and an unrelated
  runtime capability reports the pre-existing `PSObject op_Addition` harness
error. No privileged or trust-store action was attempted.
- The first pushed hosted run exposed that the default shallow CI checkout could
  not satisfy the new full-reachable historical Git evidence contract. All CI
  checkout steps now use `fetch-depth: 0`; the corrected pushed SHA is the
  authoritative hosted validation target.

Final implementation evidence:

- The corrected implementation SHA is
  `09bac46a5371ecaad58b7c38ab044deb2c5d8163`. It contains the full-history CI
  checkout fix and the lifecycle evidence-integrity implementation/evidence
  state. The preceding implementation wave is
  `cd8b0b1bd60fbb2af25944f172eca1f5b1614684`.
- Hosted Actions run `32865014793` completed with overall `success` for the
  exact corrected SHA, with the Debug, release-test, Analyze, ASAN protocol,
  repository-hygiene, and unsigned release-qualification jobs successful
  (17m44s). This exact run is bound offline by the typed lifecycle manifest to
  this tracked record and SHA.
- The first hosted run `32864154982` is retained as diagnostic history: its
  hygiene job exposed the shallow-checkout incompatibility, which was fixed in
  the corrected implementation wave. No failure was suppressed or reclassified.
- The final documentation handoff is intentionally not bound to its own
  future SHA or CI result. After this evidence commit is pushed, live Git
  parity, cleanliness, and the pushed handoff run are derived and reported
  externally rather than written back into a self-referential record.
