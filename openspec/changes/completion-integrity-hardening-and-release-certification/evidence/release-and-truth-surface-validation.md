# Completion-integrity and release-evidence validation

## Scope and candidate identity

This record covers the 2026-08-28 continuation of the completion-integrity
campaign. The implementation candidate under review is
`ceffa009ef566edfc9895c363a68fdd005ddc0fa` on `main`; `origin/main` was
`2a3841ceef1b0b99f533ed3bf46d7a63096cc114` when this work began. The only
pre-existing unrelated working-tree item is the untracked
`docs/agent/HANDOFF_12H_2026-08-27.md`, which was preserved.

The implementation candidate is a small fail-closed correction after the
rule-pruning repair. `Projection::RebuildVolumeFromStore` now reports durable
stream failure, removes any partial resident projection, retains the cold
reload marker, and allows a later retry. `IndexPipeline::RebuildAll` propagates
failed rebuild/rule enumeration, and snapshot export refuses to expose an
unreloaded volume. The regression fixture proves failure, cold retention,
close/open recovery, and successful retry.

## Exact hosted baseline

Before the current implementation edit, hosted run `33129228485` passed the
exact SHA `2a3841ceef1b0b99f533ed3bf46d7a63096cc114` across all six jobs:

- ASAN protocol/IPC/lifecycle;
- Debug full CTest;
- release-test full CTest;
- Analyze;
- hygiene; and
- unsigned release qualification.

The run retained the non-empty artifact
`release-qualification-33129228485`. Inspection found exactly two JSON files:
`candidate-manifest.json` (2,286 bytes) and `provenance.json` (1,080 bytes).
Both identify the qualified SHA, classify the build as
`unsigned-validation` with `productionReady=false`, use only kit-relative
version paths, and redact path-valued signing-pin sources. No check-run
annotations were returned for Analyze or unsigned release qualification.

This hosted result is a verified predecessor baseline, not proof of the later
cold-reload implementation. The final candidate must be rebuilt and inspected
at its own exact SHA.

## Local validation

The focused post-repair tests passed:

- `ffindexstore_projection_tests` — 1/1;
- `ffengine_index_pipeline_tests` — 1/1; and
- the new cold-reload regression directly asserted no partial resident rows,
  retained cold state, and close/open retry recovery.

The current validation wave also produced:

- Debug full CTest — 86/86;
- release-test full CTest — 86/86;
- Analyze configure/build — warning-clean;
- authoritative ASAN protocol/IPC/lifecycle slice — 3/3;
- `Test-ReleaseTooling.ps1 -BuildRoot build\\release-test` — `Failures: 0`;
- clean detached-worktree release-candidate qualification — PASS, including
  deterministic archives, checksum/provenance checks, relative-path checks,
  redacted pin state, and missing-signature rejection; and
- the 100,000-entry release-test benchmark — exit 0, with forced eviction
  reducing estimated projection memory from 41,745,068 to 1,618,044 bytes,
  zero unavailable-reader failures, four bounded restarts during sustained
  fallback writes, and zero active readers after cleanup.

The diagnostic UIA build also completed cleanly. The live verifier was repaired
for fixture readiness, duplicate palette toggles, persistent-host scope
selection, separate Win32 helper type guards, and repeated Filters disclosure
clicks. An earlier isolated allocated-size invocation passed; the latest
focused invocation still failed at the Filters disclosure. This capability is
therefore recorded as unverified on this host, not as a product PASS, and the
major UIA journey tasks remain open.

The clean qualification and full suites were run on the immediately preceding
implementation tree before the final gate-only source amendment; the final
committed candidate's hosted build is the exact-tree authority for the
amended source. No certificate, trusted-root store, privileged service, or
production signing material was changed.

## Truth-surface and adversarial review

- The rule-pruning dependency is closed at 22/22 tasks, with exact hosted
  evidence at `f65b3bfc770587b4f28f3fa49f3da68faa4142a3` / run `33122565103`.
- The release-retention gap was reproduced, repaired, and rechecked after the
  artifact-action migration; the release metadata path-leak regression and
  active-empty-ledger regression are covered by tests.
- Current production/test/control-plane marker search found no actionable
  `TODO`, `FIXME`, or `HACK` markers, disabled decisive tests, commented
  decisive assertions, or debug-print bypasses.
- Timeout, skip, and conditional branches were reviewed. Remaining skips are
  explicit capability/environment boundaries; they do not convert a failed
  check into PASS.
- Security review found no regression to the closed command surface, opaque
  connection-scoped handles, frame/record bounds, mutual-authentication pins,
  ACL policy, fail-closed unsigned release path, or DLL-loading hardening.
- The `Store::cachedPageSize_` concern is non-actionable on the current
  single-threaded maintenance call graph and was left unchanged.

## External boundary

The repository is locally supportable only as an unsigned/degraded validation
build. Production signer and authorized pins, signed install/runtime, elevated
raw MFT/USN validation, clean-host/guest/provider evidence, and
physical-display/direct-render evidence remain unavailable. Those conditions
remain explicitly blocked and are not represented as local PASS results.

The final state is therefore intended to be **locally complete; external
certification pending** only after the local final gates and exact final
commit's hosted CI/release artifact are inspected; the live UIA journey is a
separate open host/provider boundary until then.
