# FastFiles Audit Remediation Workstate

Last updated: 2026-08-13T17:51:17+08:00
Status: HISTORICAL CAMPAIGN RECORD — READY WITH EXTERNAL BLOCKERS; all local code/build/test gates recorded here passed, while production signing and clean-host evidence remained external

## Durable-state protocol

This file is a historical record of the earlier audit campaign, not a live
repository database. The repository-wide and post-push handoff protocols are
the current authority for recovery.

- The branch, `HEAD`, `origin/main`, dirty status, ancestry, and latest hosted
  Actions run recorded below are dated observations. Fetch and derive live
  values at resume time; never treat these values as current.
- A tracked workstate may record a validated implementation baseline and
  commit-bound historical CI evidence, but must not require the SHA of the
  commit that contains the workstate or create a recursive latest-CI claim.
- Any session-owned edit must be validated, committed, pushed to
  `origin/main`, and followed by a clean worktree check. “Intentionally dirty”
  is not a successful FastFiles handoff.

## Mission

Comprehensively remediate the supplied latest deep-audit findings FF-AUDIT-01
through FF-AUDIT-10 plus validation gaps A–E, with secure architecture,
regression coverage, measurable scale behavior, deterministic CI/release
validation, and a durable handoff. Do not push or alter unrelated work.

## Historical Repository Checkpoint (2026-08-13)

The values in this section were observed during the earlier audit mission.
They are retained for provenance only and must not be used as live state.

- Observed branch: `main`
- Observed HEAD: `190adf3` (`docs: synchronize historical benchmark notes`; latest remediation commit at that checkpoint)
- Observed `origin/main`: `403f8490aab019e02b5d82cef734ea76223d3396`
- Starting worktree: clean (`git status --short` produced no entries).
- Dirty files present before task: none.
- Remediation-owned modified files: this workstate and the historical benchmark-notes documentation; all application, test, build, release-tooling, and version-resource changes are committed.
- Unrelated dirty files to preserve: none observed at start; re-check before every commit.
- Historical repository workstate: `docs/agent/WORKSTATE.md` (contains older release/UI checkpoints and was stale relative to the starting HEAD). Mission plan: `.agent/execplans/fastfiles-audit-remediation.md`.
- Mission workstate: `.agent/workstate/fastfiles-audit-remediation-state.md`.

## Finding Matrix

| ID | Severity | Finding | Status | Implementation | Validation | Commit |
|----|----------|---------|--------|----------------|------------|--------|
| FF-AUDIT-01 | CRITICAL | Elevated installer trusts mutable sibling executables | VALIDATED | Shared `ffsetup` package-trust seam binds manifest hash, approved Authenticode signer, stable file identity, non-reparse handle, `CREATE_NEW` staging, and same-volume promotion | `ffsetup_package_trust_tests` plus final Debug/release-test/Analyze/Release CTest 52/52 PASS; clean/elevated live install remains external | `a3680bf`, `32938ca` |
| FF-AUDIT-02 | HIGH | Elevated uninstall executes a relay from user-writable `%TEMP%` | VALIDATED | Removed temp relay and detached helper; installed image performs uninstall and schedules only its own deferred deletion | `ffsetup_installer_source_policy_tests` plus final 52/52 profile suites PASS; live in-use/elevated uninstall pending external host | `a3680bf` |
| FF-AUDIT-03 | CRITICAL release blocker | Production privileged path lacks final signed clean-host proof | BLOCKED_EXTERNAL | Local release workflow is fail-closed and deterministic; external signing/clean-host evidence unavailable | `verify.ps1 release-readiness -Mode report`: artifact integrity PASS; signing/pins/privileged path/clean host BLOCKED_EXTERNAL, exit 2 | `a414cab` |
| FF-AUDIT-04 | HIGH | Fixed two-slot snapshot creates excessive memory/commit pressure | VALIDATED | Immutable generation-sized mappings, explicit format v2, bounded 256 MiB payload rejection, checked two-pass direct serialization, and in-place UI parsing are committed | Focused tests 3/3; 100K/500K/1M full corpus runs PASS; 2M reached durable insert, rebuild, actual 230,709,659-byte snapshot publication, and retained last-stage diagnostics before the projection probe exceeded the 900 s benchmark bound | `d69401f` |
| FF-AUDIT-05 | MEDIUM | USN incremental updates erase valid creation/access timestamps | VALIDATED | Explicit `EntryFieldMask` partial-update semantics now merge SQLite and projection state; USN marks creation/access absent | Focused regression and final Debug/release-test/Analyze/Release CTest 52/52 PASS; restart/deletion/reconciliation coverage and verifier PASS | `a47b705` |
| FF-AUDIT-06 | MEDIUM | Indexed searches are stale-result-safe but not cancellable | VALIDATED | Added closed `CancelIndexedSearch` UI command, per-connection worker/token supersession, projection/search-loop cancellation checks, teardown cancellation, and a lock-backed UI callback lifetime guard | Final Debug/release-test/Analyze/Release CTest 52/52; direct engine test reports final superseded-search latency 2 ms; UI server test proves two-client isolation, prompt cancellation, and no stale result write | `18a361e`, `39cb55` |
| FF-AUDIT-07 | MEDIUM | UI↔Engine IPC uses `PeekNamedPipe` polling | VALIDATED | UI client/server now use overlapped reads/writes; UI listener retains accepted threads and cancellation duplicates; product polling path removed; listener is unconditionally overlapped | Final Debug/release-test/Analyze/Release CTest 52/52; framing/UI lifecycle tests cover disconnect and shutdown; Service/Engine/UI targets build | `f262219`, `99610e2` |
| FF-AUDIT-08 | MEDIUM | Synchronous timeout can exceed deadline after helper join | VALIDATED | Shared framing, `PrivilegedConnection`, every live index-service write, and `PipeListener` now use event-backed overlapped operations with absolute deadlines; no helper-thread join or synchronous cancellation fallback remains | Final Debug/release-test/Analyze/Release CTest 52/52, ASAN 1/1, and focused deadline-policy/framing/UI tests 3/3 PASS | `f262219`, `2154f62`, `99610e2` |
| FF-AUDIT-09 | MEDIUM | Upgrade rollback may reactivate unauthenticated prior binaries | VALIDATED | Backup/restore requires schema-2 manifest, exact inventory, approved signatures, identity/hash validation, and handle-bound staged promotion | Rollback candidate matrix in `ffsetup_package_trust_tests` plus final 52/52 profile suites PASS; injected live upgrade failure pending external host | `a3680bf` |
| FF-AUDIT-10 | LOW | Release binaries lack embedded version-resource metadata | VALIDATED | Single repository `VERSION` source feeds generated C++ metadata and VERSIONINFO resources for all four product executables plus `fftest`; release-kit/readiness paths validate final metadata | Final Release build PASS; direct `FileVersionInfo` 5/5; release-tooling 31/31; setup/drift and readiness metadata checks PASS | `054128b` |
| GAP-A | HIGH | Release CI reproducibility/signing pins | IMPLEMENTED | Added explicit optimized unsigned `release-test` profile; production `release` remains pin-enforced; package manifest validation is required by kit/clean-host/privileged paths | Final release-test 52/52, Release 52/52, release-tooling 31/31; authorized CI signing execution remains external | `a414cab` |
| GAP-B | HIGH | Installer regression coverage | VALIDATED | First-class setup package-trust and installer source-policy targets cover manifest, signer, hash, identity, reparse, staging, promotion, rollback candidates, and relay removal | Focused setup CTest 4/4 plus final Debug/release-test/Analyze/Release CTest 52/52 | `a3680bf` |
| GAP-C | MEDIUM | ASAN not continuously gated | IMPLEMENTED | Added a CI ASAN protocol-fuzz job with VS developer-runtime activation; exclusions are limited to the applicable core target | Final local ASAN fuzz pass 1/1; CI execution remains external | `a414cab` |
| GAP-D | MEDIUM | Large-index benchmark lacks actionable progress | VALIDATED | Stage/progress output, 2M default tier, analytical capacity search, per-stage snapshot memory fields, and retained-log execution method are committed | 100K/500K/1M complete; 2M retained log identifies insert 192.94 s, rebuild 62.59 s, snapshot export 65.58 s, and projection probe 61.23 s before the 900 s bound | `d69401f` |
| GAP-E | LOW | Direct rendered UI validation gap | BLOCKED_EXTERNAL | Preserve headless/CLI checks and document boundary | Actual rendered Windows validation requires available visual host/tool | — |

## Root-Cause Workstreams

### A. Installer / Privilege Boundary

Unify package manifest/hash, approved signer, stable file identity, reparse
rejection, protected staging, promotion, uninstall, and rollback. Never let a
path-only check turn user-writable content into a trusted service binary.

### B. Production Signing / Release

Keep placeholder pins and production assertions fail-closed. Automate artifact
manifest, signature matrix, pin derivation/review, clean-host preflight, and
evidence import without private keys or fake production evidence.

### C. Incremental Index Data Correctness

Represent USN deltas as explicit partial updates. Merge only valid fields in
both SQLite and the in-memory projection; retain authoritative timestamps and
test deletion/restart/reconciliation.

### D. Snapshot / Large-Index Architecture

Measure bytes, copies, latency, private bytes/working set/commit where
available. Prefer immutable versioned file-backed or segmented generations with
bounded retention and atomic handoff; never publish partial/truncated data.

### E. Search Cancellation

Use connection/client-scoped request supersession and cancellation tokens that
are checked inside expensive projection/search loops. Teardown cancels before
destroying owners; callbacks retain request ownership checks.

### F. IPC Modernization

Consolidate named-pipe framing around explicit overlapped read/write state,
absolute deadlines, `CancelIoEx`, serialized writes, deterministic teardown,
and no detached connection threads or polling. Both product seams will use
handles opened with `FILE_FLAG_OVERLAPPED`; synchronous framing remains only as
an explicitly bounded test/compatibility primitive where no live product path
depends on it. The final service audit must also remove the remaining
watchdog/helper-thread write fallback and route every live service reply through
the bounded overload.

### G. Release Metadata / Migration Readiness

One repository `VERSION` source feeds CMake-generated headers, all shipped
VERSIONINFO resources, installer display metadata, and release package
validation. Any snapshot/protocol or SQLite change must explicitly define
versioning, compatibility, migration, and fail-closed recovery.

### H. CI / Validation Coverage

Separate optimized unsigned test-release artifacts from production signed
release jobs. Add applicable ASAN/static analysis, installer security tests,
incremental benchmark diagnostics, drift/OpenSpec checks, and honest rendered
UI external-validation notes.

## Architectural Decisions

- Use dedicated mission state under `.agent/` while preserving the existing
  `docs/agent/WORKSTATE.md`; both paths are recorded here and in the ExecPlan.
- Treat the supplied ten findings as the current remediation scope even though
  the older `address-engineering-audit-findings` OpenSpec change is complete.
  Do not rewrite historical OpenSpec checkboxes to manufacture traceability.
- Preserve degraded mode and all runtime pinned-authentication checks. Real
  production signing and clean elevated-host MFT/USN evidence can only be
  `BLOCKED_EXTERNAL` until supplied by an authorized environment.
- Installer trust will use a release-kit package manifest plus approved
  Authenticode signer pins, but the payload copy itself will consume an
  already-open, non-reparse source handle. The source file identity and hash
  are checked before and after signature verification; staging uses
  `CREATE_NEW`; promotion disallows cross-volume copy and re-verifies the
  promoted file. This makes verification and consumption refer to the same
  file object and fails closed on a substitution race.
- The four shipped payloads use the two existing production pins: Engine and
  IndexSvc require their role-specific pins; UI and Setup must match one of
  those approved pins. No new signing secret or production thumbprint is
  introduced. The release kit will carry a deterministic hash manifest.
- Upgrade backups are created only when no prior `Backup` directory exists;
  a stale/preplanted backup is rejected. The previous installed package
  manifest and every four payloads are validated before the service is
  replaced, and restoration revalidates them again before promotion.
- Uninstall keeps only the currently executing installed Setup image long
  enough to schedule its own deletion via `SetFileInformationByHandle` or
  `MoveFileEx(..., MOVEFILE_DELAY_UNTIL_REBOOT)`; no user-writable helper
  process or `%TEMP%` path participates.
- Production `release` remains pin-enforced. A separate `release-test`
  `RelWithDebInfo` profile is explicitly unsigned/test-only for CI and local
  optimized regression coverage; it does not change runtime authentication or
  production release assertions. Release kits now require a deterministic
  package manifest and every release/clean-host/privileged entry point checks it
  against the artifact manifest before execution.
- CI gates the applicable ASAN protocol fuzz target under a Visual Studio
  developer environment so the sanitizer runtime is available; broader Windows
  integration tests remain outside the sanitizer job until compatibility is
  demonstrated.
- Snapshot design remains deliberately undecided; measure and inspect current
- Snapshot redesign decision: use immutable, generation-named mappings sized to
  `sizeof(header) + actual serialized payload`, with one current mapping held by
  the engine and old mappings released by the engine after handoff. A GUID epoch
  is included in the prefix so a restarted engine cannot reuse a still-open
  generation name. The UI opens a generation only after the engine has fully
  written and committed its header, and keeps its last good mapping if a newer
  mapping cannot be opened. Keep a 256 MiB maximum payload as an explicit
  fail-closed bound, but remove the fixed 512 MiB reservation. Format version
  changes from 1 to 2; incompatible mappings are rejected.
- Alternatives considered: file-backed generation files could reduce pagefile
  commit further but add crash cleanup, ACL, and file-lifecycle complexity; a
  segmented/chunked format would reduce peak serialization but requires a new
  reader and publication topology. The first implementation uses dynamic
  generations because it removes the dominant fixed commit envelope while
  preserving the existing zero-IPC reader model and atomic notification seam.
- IPC modernization: use native overlapped named-pipe handles for both product
  seams, with one frame reader/writer implementation in `ffipc`. Absolute
  deadlines govern handshake/idle reads and bounded writes; `CancelIoEx` is the
  teardown path, and each complete frame write remains serialized per
  connection. This removes the synchronous helper-thread join hazard and the
  UI `PeekNamedPipe` polling loop.
- Listener ownership: `PipeListener` will retain accepted connection threads
  and cancel their tracked pipe handles before joining them. This preserves the
  existing security-descriptor injection while removing detached connection
  teardown races. A listener-owned callback remains responsible for closing
  the accepted handle after its work returns.
- Alternatives considered for IPC: keep synchronous reads plus a polling
  header probe; reject because it burns idle wakeups and cannot support a clean
  overlapped cancellation model. Keep `CancelSynchronousIo` helper threads
  for handshake; reject because the join is not a hard deadline. Duplicate UI
  and service frame implementations; reject because framing, bounds, and
  cancellation semantics would drift again.

## Changes Completed

1. Reconciled actual Git state against historical workstate: clean `main` at
   `403f849`, equal to `origin/main`.
2. Read repository instructions, README/testing/verifier guidance, relevant
   architecture context, existing audit/workstate artifacts, and OpenSpec
   inventory. Created the mission ExecPlan and workstate.
3. Implemented explicit partial-update masks for `EntryChange`, used them in
   the SQLite UPSERT and projection merge paths, and marked USN-derived
   creation/access fields absent instead of overwriting authoritative values
   with zero. Added store/projection tests plus an engine sequence covering
   MFT metadata, rename/write/attribute USN deltas, restart, full
   reconciliation, and deletion.
4. Read the Windows file-operation guidance for `FILE_FLAG_OPEN_REPARSE_POINT`
   and `MoveFileEx`; selected handle-bound reads plus hash/signature rechecks,
   protected scratch staging, and same-volume atomic promotion for installer
   payloads.
5. Added `ffsetup::PackageTrust`, a release-kit manifest writer, installer
   payload/rollback integration, reparse-safe scratch roots, no-relay
   uninstall behavior, and focused adversarial setup tests.
6. Committed the installer/package-trust/uninstall/rollback slice as
   `a3680bf` without staging the in-progress USN data-correctness files.
7. Checkpointed the installer slice and updated this workstate at
   `9e51d07`.
8. Started release reproducibility remediation: added the explicit optimized
   unsigned `release-test` preset, changed ordinary CI/verifier defaults to use
   it, retained production Release pin enforcement, added package-manifest
   validation to release kit/clean-host/privileged workflows, and added the
   applicable ASAN CI job. Documentation and focused release-tooling tests are
   in progress.
9. Completed the release/CI slice: package-manifest validation now checks both
   the manifest inventory and actual kit files; the optimized `release-test`
   profile is used by ordinary CI/verifier builds; production `release` still
   fails closed without authorized pins; and CI runs the applicable ASAN fuzz
   target under the VS runtime environment.
10. Fixed installer trust-helper `/analyze` findings by moving 64 KiB hash/copy
    buffers to heap storage and making invalid-handle cleanup explicit; committed
    as `32938ca`.
11. Committed the release/CI reproducibility slice as `a414cab`: explicit
    optimized unsigned `release-test`, production pin enforcement, package
    manifest actual-file validation, ASAN CI coverage, and synchronized docs.
12. Committed the USN partial-update data-correctness slice as `a47b705`.
13. Snapshot measurement/design frontier: the old `SnapshotPublisher` created
    one 512 MiB pagefile-backed mapping per engine (two 256 MiB slots), the UI
    copied the selected slot into a vector, and serialization created another
    full buffer. The selected redesign is immutable generation-sized mappings
    with a GUID epoch and version 2 header. The current implementation adds a
    checked two-pass serializer that writes directly into the generation and
    parses that immutable payload in place in the UI. Benchmark diagnostics
    cover 10K/100K/500K/1M/2M tiers.
14. Committed the snapshot architecture slice as `d69401f`: immutable
    generation-named mappings, GUID restart epoch, version-2 header, direct
    two-pass serialization, in-place UI parsing, safe last-good remap
    behavior, and benchmark stage/memory diagnostics.
15. FF-AUDIT-06 implementation: indexed search is now dispatched through one
   worker per UI connection. A newer request or explicit cancel sets only that
   connection's atomic token; `IndexPipeline` checks it during projection
   enumeration, candidate materialization, and the existing search loops.
   Cancelled work never serializes a result. UI callbacks use a weak lifetime
   state whose mutex is held across owner access; destruction acquires the same
   mutex before marking the state dead, so a late reader callback cannot race
   into a destroyed `SearchPanel`.
16. FF-AUDIT-07/08 implementation: converted live UI↔Engine and Engine↔Service
   pipe handles to `FILE_FLAG_OVERLAPPED`, removed `PeekNamedPipe` polling and
   the privileged handshake reader thread, and routed frame reads/writes
   through shared event-backed `ffipc` operations. A cancelled operation is
   collected within a bounded grace period or retained in a bounded quarantine
   so returning from a deadline cannot free state still referenced by Windows.
   `PipeListener` now retains accepted threads and duplicate cancellation
   handles and joins them during Stop.
17. FF-AUDIT-10 implementation: added the canonical repository
    `VERSION` file, generated installer version header, generated Windows
    VERSIONINFO resources for FastFiles/FastFilesEngine/FastFilesIndexSvc/
    FastFilesSetup/fftest, and release-tooling checks for product/file,
    identity, and descriptive metadata. Debug/release-tooling validation now
    passes; the focused commit and optimized/final matrix remain.
18. Committed the version metadata/release validation slice as `054128b`.
19. Corrected the optimized-only cancellation regression test by holding the
     obsolete search callback in flight until cancellation is published; this
     removes a timing-dependent false failure without weakening cancellation
     assertions. Focused optimized CTest and ten direct repetitions pass.
20. Committed that deterministic test synchronization as `39cb55b`.
21. Final source audit found that the service's scan/journal write watchdog
    still retained the exact helper-thread timeout hazard under FF-AUDIT-08.
    Replaced it with a per-frame absolute-deadline wrapper around shared
    overlapped framing, bounded handshake/control replies, and added a source
    policy regression test. Focused validation passes; the full post-commit
    configuration matrix is the remaining check.
22. Committed the residual FF-AUDIT-08 correction as `2154f62`: every live
    index-service reply, scan batch, journal batch, and handshake/control frame
    now uses a fresh absolute-deadline shared overlapped write; the helper
    thread/watchdog fallback and stale detached-thread comment are removed.
23. Made `PipeListener` unconditionally overlapped and removed its dormant
    synchronous accept/cancellation mode; committed as `99610e2`.
24. Restored the tracked historical benchmark JSON after the 10K diagnostic
    smoke test wrote a temporary result over it, corrected historical benchmark
    notes, and committed the documentation-only update as `190adf3`.

## Validation Evidence

- FF-AUDIT-10 configure checkpoint: `cmake --preset debug` PASS; generated
  `FastFilesVersion.h` contains `0.1.0`, and five generated RC files contain
  `FILEVERSION 0,1,0,0` / canonical identity fields. Final target and
  `FileVersionInfo` inspection are complete below.
- FF-AUDIT-10 validation: Debug target build PASS after CMake regeneration;
  direct `System.Diagnostics.FileVersionInfo` validation PASS 5/5 for
  FastFiles/FastFilesEngine/FastFilesIndexSvc/FastFilesSetup/fftest;
  `Test-ReleaseTooling.ps1` PASS 31/31; focused setup plus drift CTest PASS
  3/3. `New-ReleaseKit.ps1` generated a kit with version metadata PASS and
  remains `productionReady=false` because signing is still external.
- `ReleaseReadiness.ps1 -Mode report` against rebuilt Debug artifacts reports
  artifact integrity/version metadata PASS and expected overall exit 2
  (`BLOCKED_EXTERNAL`) for unsigned artifacts and the contaminated/non-elevated
  host; it does not turn external signing into a local PASS.
- Post-remediation Debug full CTest: `ctest --preset debug --output-on-failure`
  PASS, 51/51 in 31.06 seconds, including IPC cancellation/timeout, USN
  metadata preservation, installer adversarial tests, snapshot tests, and
  documentation drift.
- First post-remediation optimized gate: `cmake --preset release-test &&
  cmake --build --preset release-test` PASS (99 build steps); CTest is 50/51
  with only `ffengine_index_pipeline_tests` failing. The failure is at
  `superseded indexed search exits as cancelled`; the same test continued to
  pass its later cooperative-cancellation, final-latency, and result-ownership
  assertions. This is recorded as unresolved until synchronization/root cause
  is established; no retry is being counted as validation yet.
- Root cause: `tests/engine/test_index_pipeline.cpp` observed the obsolete
  callback once but did not hold it in-flight, so RelWithDebInfo could finish
  before the cancellation store. The test now gates that callback until the
  caller publishes cancellation. Focused optimized CTest passes 1/1, and ten
  direct optimized repetitions pass 10/10 with the cancelled status and final
  latency assertions intact. No production code change was needed.
- Rerun optimized full CTest after the fix: `ctest --preset release-test
  --output-on-failure` PASS, 51/51 in 20.76 seconds.
- Analyze checkpoint: `cmake --preset analyze && cmake --build --preset
  analyze` PASS; version resources and the optimized cancellation-test fix
  compile cleanly under MSVC `/analyze`.
- Direct Analyze CTest: `ctest --test-dir build/analyze --output-on-failure`
  PASS, 51/51 in 27.74 seconds.
- First ASAN invocation failed before CMake configuration because the command
  used an incorrect Ninja path (omitted `IDE\\`); no product compilation or
  test execution occurred. The known-good VS-bundled Ninja path is recorded in
  AGENTS.md and the corrected rerun is required.
- Corrected ASAN gate: `cmake --preset asan && cmake --build --preset asan
  --target ffprotocol_fuzz_tests` PASS; `ctest --test-dir build/asan -R
  ffprotocol_fuzz_tests --output-on-failure` PASS, 1/1 in 5.76 seconds.
  MSVC emitted only the existing `/W3`-overridden-by-`/W4` command-line note.
- Repository verifier build: `pwsh -NoProfile -File .\\verify\\verify.ps1
  build` PASS (`windows-build-validation`).
- Toolchain doctor: `pwsh -NoProfile -File .\\verify\\verify.ps1 doctor` PASS;
  Visual Studio/Windows SDK/CMake/Ninja and core diagnostics are present.
  Optional WPA/Procmon/Procdump/PageHeap/WinDbg/Hyper-V/Sandbox tools are
  explicitly SKIPPED as unavailable.
- Production Release checkpoint: `cmake --preset release && cmake --build
  --preset release` PASS (112 build steps), using existing locally cached
  thumbprint inputs only. The build contains the canonical version resources;
  cached pins are not production signing evidence.

- Initial `git status --short`: PASS (clean).
- Initial branch/HEAD/origin checks: PASS; all point to `main`/`403f849`.
- `openspec list --json`: PASS; `close-independent-validation-gaps` is
  28/34, `address-engineering-audit-findings` is complete, and recovery is
  complete.
- `cmake --preset debug`: PASS; `cmake --build --preset debug`: PASS/no work;
  `ctest --preset debug --output-on-failure`: PASS, 49/49, 26.89 seconds.
- `cmake --preset release` and `cmake --build --preset release`: PASS/no work.
- `cmake --preset analyze` and `cmake --build --preset analyze`: PASS/no work.
- `cmake --preset asan` and `cmake --build --preset asan --target
  ffprotocol_fuzz_tests`: PASS under `VsDevCmd.bat -arch=x64`; direct ASAN fuzz
  executable also PASS. Running the executable without the VS runtime path
  returned `0xC0000135`, an environment invocation issue, not a test PASS.
- `pwsh ./verify/verify.ps1 doctor`: PASS; optional WPA/Procmon/Procdump/
  WinDbg/Hyper-V/Sandbox tools SKIPPED as unavailable.
- `pwsh ./verify/verify.ps1 build`: PASS (`windows-build-validation`).
- `pwsh ./verify/verify.ps1 release-readiness -Mode report`: expected
  `BLOCKED_EXTERNAL` (exit 2); artifact integrity PASS, signing/pins/privileged
  path/clean host blocked, rendered visual out of scope.
- `openspec validate --all --strict`: PASS, 45/45.
- Focused Debug build under `VsDevCmd.bat -arch=x64`:
  `cmake --build --preset debug --target ffindexstore_store_tests
  ffindexstore_projection_tests ffengine_index_pipeline_tests`: PASS.
- Focused CTest under the same developer environment:
  `ctest --test-dir build/debug -R
  "ffindexstore_store_tests|ffindexstore_projection_tests|ffengine_index_pipeline_tests"
  --output-on-failure`: PASS, 3/3.
- End-to-end USN metadata regression:
  `cmake --build --preset debug --target ffengine_index_pipeline_tests &&
  ctest --test-dir build/debug -R ffengine_index_pipeline_tests
  --output-on-failure`: PASS, 1/1, 1.16 seconds.
- Installer/security slice:
  `cmake --build --preset debug --target FastFilesSetup
  ffsetup_package_trust_tests ffsetup_installer_source_policy_tests` under
  `VsDevCmd.bat -arch=x64`: PASS.
- Installer/setup CTest:
  `ctest --test-dir build/debug -R
  "ffsetup_(package_trust|installer_source_policy|peer_image_match|security_descriptor)_tests"
  --output-on-failure`: PASS, 4/4, 0.26 seconds. The package-trust test
  includes unsigned/wrong/unapproved/approved signer decisions, malformed and
  mismatched hashes, source substitution, preplanted staging/destination,
  staged mutation cleanup, optional reparse fixtures, and rollback candidate
  inventory/signature/hash cases.
- Release tooling regression:
  `pwsh -NoProfile -File .\verify\release\tests\Test-ReleaseTooling.ps1`: PASS,
  26/26, including package-manifest round trip/tampering/missing-file checks
  and explicit `release-test` versus production `release` preset policy.
- Optimized test-release configure/build:
  `cmake --preset release-test && cmake --build --preset release-test`: PASS
  under `VsDevCmd.bat -arch=x64`; all 234 build steps completed.
- Optimized test-release CTest: 50/51 passed. The only failure was
  `ffdoc_drift_tests`, which correctly found mixed CRLF/LF in remediation-touched
  C++/headers; no functional test failed.
- After CRLF normalization of the remediation-owned C++/headers,
  `ctest --preset release-test --output-on-failure`: PASS, 51/51, 19.16
  seconds.
- Release tooling regression after actual-file package validation:
  `pwsh -NoProfile -File .\verify\release\tests\Test-ReleaseTooling.ps1`:
  PASS, 27/27. This includes package artifact substitution detection.
- Static-analysis correction: the first post-remediation Analyze build exposed
  three installer trust-helper diagnostics (two C6262 stack-buffer warnings and
  one C6387 invalid-handle close warning). The hash/copy buffers now use heap
  storage and invalid handles are returned before `CloseHandle`; direct
  `cmake --preset analyze && cmake --build --preset analyze` now passes.
- Verifier isolation after that correction:
  `pwsh .\verify\verify.ps1 build -Configuration analyze -SkipTests`: PASS;
  `pwsh .\verify\verify.ps1 build -Configuration debug -SkipAnalyze`: PASS.
- Full default verifier after the correction:
  `pwsh -NoProfile -File .\verify\verify.ps1 build`: PASS
  (`windows-build-validation`), covering Debug + `release-test` + Analyze.
- Snapshot two-pass/in-place slice:
  `cmake --build --preset debug --target ffprotocol_tests FastFilesEngine
  FastFiles ffengine_snapshot_publisher_tests ffengine_ui_server_lifecycle_tests
  ffengine_bench_large_index_scalability`: PASS under `VsDevCmd.bat -arch=x64`.
  Focused CTest for `ffprotocol_tests`, `ffengine_snapshot_publisher_tests`,
  and `ffengine_ui_server_lifecycle_tests`: PASS, 3/3, 1.56 seconds.
- Snapshot benchmark before the direct-writer change completed the 10K tier and
  probed the payload bound: 10K actual publish serialization 43.99 ms,
  publish 0.50 ms, UI map/parse 18.51 ms, 1,098,609 bytes; exact largest
  successful analytical capacity was 2,200,468 rows at 268,435,456 payload
  bytes, with 2,200,469 rejected. The run reached approximately 1.02 GiB
  private bytes / 1.27 GiB working set while retaining source, serialized,
  mapping, and parsed copies. That peak is the reason the direct two-pass and
  in-place reader change is required; the benchmark must be rerun after it.
- Post-direct-writer benchmark (`ffengine_bench_large_index_scalability.exe
  --scales 10000 --diagnostics`) completed with incremental output and wrote
  `%TEMP%\FastFiles-snapshot-direct-10k.json`. The 10K actual snapshot was
  1,098,609 bytes: model/export 20.44 ms, direct serialization 1.81 ms,
  publication 0.09 ms, and UI map/parse 17.00 ms. The analytical capacity
  probes reached 2,200,468 rows / 268,435,424 bytes in 5.78 seconds total
  (model 5.31 s, direct serialize 405.55 ms, publish 23.92 ms, UI parse
  3.64 s); 2,200,469 rows / 268,435,547 bytes rejected without advancing the
  generation. The largest probe ended at 832,028,672 private bytes and
  1,093,849,088 working-set bytes, down from the prior approximately 1.02 GiB
  private / 1.27 GiB working-set peak. The remaining peak is dominated by the
  synthetic source model and parsed result, not a fixed snapshot reservation.
- Corpus benchmark 100K (`--scales 100000 --diagnostics`) completed in 68.1 s
  and wrote `%TEMP%\FastFiles-large-index-100k.json`. Durable insert was
  3.92 s, projection rebuild 1.82 s, actual 100K snapshot export 206.89 ms,
  direct serialization 21.67 ms, publication 0.14 ms, and UI map/parse
  179.18 ms for 11,250,911 bytes. Search medians were 1.39–133.56 ms across
  the recorded query classes. The benchmark then published the full 2,200,468
  row / 268,435,424 byte probe and rejected 2,200,469 rows as expected.
- Corpus benchmark 500K (`--scales 500000 --diagnostics`) completed in 102.1 s
  and wrote `%TEMP%\FastFiles-large-index-500k.json`. Durable insert was
  30.18 s with five progress checkpoints, projection rebuild 9.91 s, actual
  500K snapshot export 1,137.64 ms, direct serialization 118.41 ms,
  publication 0.15 ms, and UI map/parse 904.30 ms for 57,367,843 bytes.
  Search medians were 5.75–168.00 ms across the recorded query classes. The
  capacity probes again completed and rejected the first over-limit payload.
- Corpus benchmark 1M (`--scales 1000000 --diagnostics`) completed in 300.0 s
  and wrote `%TEMP%\FastFiles-large-index-1m.json`. Durable insert was
  150.37 s with ten progress checkpoints, projection rebuild 46.41 s, actual
  1M snapshot export 4,985.97 ms, direct serialization 382.17 ms,
  publication 0.19 ms, and UI map/parse 4,011.08 ms for 115,160,389 bytes.
  Search medians were 17.99–413.65 ms. The 2.2M capacity probe completed
  within the same run and rejected the over-limit generation; the benchmark
  now gives actionable evidence rather than a silent timeout.
- Corpus benchmark 2M rerun with redirected log reached all durable fixture
  checkpoints and completed durable insert in 192.94 s, projection rebuild in
  62.59 s, and actual snapshot export/publication in 65,577.65 ms with direct
  serialization 443.02 ms, publication 0.15 ms, and UI map/parse 3,818.79 ms
  for 230,709,659 bytes. The projection probe then took 61,232.87 ms and the
  900 s command bound expired before the query suite/cleanup; the retained log
  identifies that exact stage. The two owned leftover benchmark PIDs (24420 and
  25384) were verified by command line and terminated; no benchmark process
  remains.
- Snapshot slice commit: `d69401f17c59719072f812e6f9e35458297e2230`,
  `perf: bound immutable snapshot publication memory`.
- FF-AUDIT-06 focused Debug validation:
  `cmake --build --preset debug --target ffprotocol_tests
  ffengine_index_pipeline_tests ffengine_ui_server_lifecycle_tests FastFilesEngine FastFiles`
  PASS; focused CTest PASS 3/3 in 2.00 s. Direct `ffengine_index_pipeline_tests.exe`
  reports `superseded indexed search final latency: 2 ms` and all checks pass.
  Direct `ffengine_ui_server_lifecycle_tests.exe` proves two independent UI
  clients, newer-request supersession, connection-scoped cancellation, prompt
  stop, and zero stale result writes. Analyze focused CTest PASS 3/3 in 3.84 s.
- After replacing the callback's atomic-only lifetime check with a mutex-backed
  lifetime state, the developer-environment Debug build and focused CTest pass
  again: 3/3 in 1.82 s. `git diff --check` also passes.
- FF-AUDIT-07/08 focused Debug validation: `ffipc_framing_tests` PASS 1/1 in
  0.53 s, including no-peer timeout, partial header, payload stall, peer death,
  and a stalled 1 MiB write. `ffengine_ui_server_lifecycle_tests` PASS 1/1 in
  0.14 s, including listener shutdown and cancellation. The service, engine,
  and UI targets build under Debug; Analyze target build passed before the
  final framing-test additions and must be rerun for this exact diff.
- Residual FF-AUDIT-08 correction focused validation: the Debug build of
  `FastFilesIndexSvc`, `ffindexsvc_write_deadline_policy_tests`, and
  `ffipc_framing_tests` passed under the VS developer environment. Focused
  CTest for the service policy, framing, volume-session, and UI lifecycle
  tests passed 4/4 in 0.85 s. The policy test verifies no service
  `CancelSynchronousIo`, helper-thread timeout wrapper, or unbounded direct
  service write remains.
- Post-commit Debug gate: `cmake --preset debug && cmake --build --preset
  debug && ctest --preset debug --output-on-failure` PASS; all 52/52 tests
  passed in 25.64 seconds. This includes the new service write-deadline policy
  test and the full installer, IPC, engine, UI, and documentation-drift suite.
- Post-commit optimized test-release gate: `cmake --preset release-test &&
  cmake --build --preset release-test && ctest --preset release-test
  --output-on-failure` PASS; all 52/52 tests passed in 19.67 seconds. The
  optimized cancellation synchronization and the new bounded service-write
  test both pass under `RelWithDebInfo`.
- Post-commit Analyze gate: `cmake --preset analyze && cmake --build --preset
  analyze && ctest --test-dir build/analyze --output-on-failure` PASS; all
  52/52 tests passed in 23.71 seconds, including the new service policy test.
- Post-commit production Release gate: `cmake --preset release && cmake
  --build --preset release && ctest --preset release
  --output-on-failure` PASS; refreshed Release artifacts after the service
  write-deadline correction and all 52/52 tests passed in 17.53 seconds.
- Post-commit ASAN gate: under the VS developer environment, `cmake --preset
  asan && cmake --build --preset asan --target ffprotocol_fuzz_tests && ctest
  --test-dir build/asan -R ffprotocol_fuzz_tests --output-on-failure` PASS;
  applicable sanitizer coverage passed 1/1 in 5.53 seconds.
- Post-commit verifier build: `pwsh -NoProfile -File .\verify\verify.ps1
  build` PASS (`windows-build-validation`). The verifier completed its
  Debug/release-test/Analyze build and test capability; its run tree is
  recorded by the command output and is not read as source evidence.
- Post-commit doctor: `pwsh -NoProfile -File .\verify\verify.ps1 doctor`
  PASS for VS/SDK/CMake/Ninja/core diagnostics. Optional WPA/Procmon/
  Procdump/PageHeap/WinDbg/Hyper-V/Sandbox capabilities remain explicitly
  SKIPPED because the tools are unavailable.
- Final source audit found the public `PipeListener` still carried an unused
  synchronous accept mode with `CancelSynchronousIo`, although both product
  callers already selected overlapped mode. The listener is being made
  unconditionally overlapped so the shared IPC abstraction has one lifecycle
  and no dormant timeout fallback.
- Focused validation of the listener correction: Debug build of
  `FastFilesEngine`, `FastFilesIndexSvc`, `ffindexsvc_write_deadline_policy_tests`,
  `ffipc_framing_tests`, and `ffengine_ui_server_lifecycle_tests` passed;
  focused CTest passed 3/3 in 0.88 seconds. The policy test now also proves
  `PipeListener.cpp` contains no `CancelSynchronousIo` fallback.
- Committed the unconditionally-overlapped listener correction as `99610e2`;
  the final full validation matrix must be rerun against this exact HEAD.
- Final-HEAD Debug: `cmake --preset debug && cmake --build --preset debug &&
  ctest --preset debug --output-on-failure` PASS, 52/52 in 85.26 s.
- Final-HEAD optimized test Release: `cmake --preset release-test && cmake
  --build --preset release-test && ctest --preset release-test
  --output-on-failure` PASS, 52/52 in 20.45 s.
- Final-HEAD Analyze: `cmake --preset analyze && cmake --build --preset
  analyze && ctest --test-dir build/analyze --output-on-failure` PASS, 52/52
  in 26.75 s.
- Final-HEAD production Release: `cmake --preset release && cmake --build
  --preset release && ctest --preset release --output-on-failure` PASS, 52/52
  in 18.91 s. This confirms the production configuration still builds and
  tests with its signing-pin assertions intact; it is not signed release
  evidence.
- Final-HEAD ASAN: `cmake --preset asan && cmake --build --preset asan
  --target ffprotocol_fuzz_tests && ctest --test-dir build/asan -R
  ffprotocol_fuzz_tests --output-on-failure` PASS, 1/1 in 5.28 s.
- Final-HEAD verifier: `pwsh -NoProfile -File .\verify\verify.ps1 build` PASS
  (`windows-build-validation`); the generated run tree is execution evidence
  and is intentionally not read as source.
- Final-HEAD doctor: `pwsh -NoProfile -File .\verify\verify.ps1 doctor` PASS
  for available core diagnostics; optional WPA/Procmon/Procdump/PageHeap/
  WinDbg/Hyper-V/Sandbox are explicitly SKIPPED as unavailable.
- Release tooling: `Test-ReleaseTooling.ps1 -BuildRoot build/release` PASS,
  31/31. Release kit generation PASS with five valid VERSIONINFO resources and
  `productionReady=false` as required without signed production artifacts.
- Release readiness: `ReleaseReadiness.ps1 -Mode report -ArtifactRoot
  build/release` returns the expected external-blocker exit 2. Artifact
  integrity and version checks PASS; final Release artifacts are unsigned and
  clean-host/privileged evidence is unavailable.
- OpenSpec: `npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate
  --all --strict --no-interactive` PASS, 45/45.
- Benchmark smoke: the final 10K diagnostic run emitted incremental stage
  output and PASSed: model/export 2.17 ms, direct serialization 0.57 ms,
  publication 0.10 ms, UI map/parse 1.87 ms, 1,098,609 bytes. Analytical
  capacity reached 2,200,468 rows / 268,435,424 bytes and rejected the next
  oversize generation without advancing the published generation.

## Final Re-audit

The final read-only source/test re-audit was performed against code `99610e2`
(the subsequent `190adf3` commit only updates historical documentation).

| Item | Final status | Evidence |
|------|--------------|----------|
| FF-AUDIT-01 | VALIDATED | `PackageTrust` enforces manifest/hash/signer/stable identity/non-reparse checks, handle-bound copy, protected staging, and promotion; adversarial setup tests cover unsigned, wrong-signer, mismatch, reparse, replacement, stage, and destination cases. Live elevated install remains external. |
| FF-AUDIT-02 | VALIDATED | No product temp relay remains; uninstall is initiated from the installed image with deferred self-deletion. Source-policy and relay-adversary tests pass. Live in-use/elevated uninstall remains external. |
| FF-AUDIT-03 | BLOCKED_EXTERNAL | Runtime pins, image identity, group authorization, heartbeat revalidation, and fail-closed checks remain present. Release readiness reports unsigned artifacts and no clean elevated host/provider; no production PASS was claimed. |
| FF-AUDIT-04 | VALIDATED | Snapshot v2 uses immutable generation-sized mappings, two-pass direct serialization, in-place reader parsing, bounded 256 MiB payloads, and stale-generation retention. 100K/500K/1M completed; 2M publication completed before a diagnosed projection/query bound. |
| FF-AUDIT-05 | VALIDATED | Explicit field masks preserve creation/access metadata across rename/write/attribute deltas, restart, deletion, and reconciliation in both SQLite and projection tests. |
| FF-AUDIT-06 | VALIDATED | `CancelIndexedSearch` is connection-scoped, cooperative inside expensive loops, teardown-safe, and callback-lifetime-safe; two-client isolation and direct 2 ms supersession latency tests pass. |
| FF-AUDIT-07 | VALIDATED | Product source has no `PeekNamedPipe` polling path; both seams and the listener use overlapped I/O, serialized writes, cancellation, and joinable teardown. |
| FF-AUDIT-08 | VALIDATED | Product source has no `CancelSynchronousIo` or helper-thread timeout path; all service writes use absolute-deadline overlapped framing. Deadline policy, framing, disconnect, and shutdown tests pass. |
| FF-AUDIT-09 | VALIDATED | Rollback candidates require manifest version/inventory, approved signatures, file identity/hash, completeness, and protected-location checks; unsigned/wrong-signer/corrupt/mixed candidates are rejected in setup tests. |
| FF-AUDIT-10 | VALIDATED | Canonical `VERSION` drives five executable resources; final Release metadata inspection and release-tooling 31/31 pass. |
| GAP-A | IMPLEMENTED | Explicit unsigned optimized `release-test` is separated from pin-enforced production Release; artifact/manifest/readiness tooling is deterministic. Authorized CI signing execution is external. |
| GAP-B | VALIDATED | First-class installer security targets and adversarial source-policy/package-trust tests are registered and included in final 52/52 suites. |
| GAP-C | IMPLEMENTED | Applicable ASAN protocol-fuzz validation is locally wired and PASSes 1/1; hosted CI execution remains an external environment result. |
| GAP-D | VALIDATED | Benchmark progress and per-stage diagnostics identify fixture, durable insert, rebuild, snapshot, UI parse, projection, search, and cleanup boundaries; the 2M bound produced actionable output. |
| GAP-E | BLOCKED_EXTERNAL | Headless geometry/state checks remain; actual rendered Windows behavior still needs a usable visual/native UI validation host. |

## Known Failures

No current local product or harness failure is open. The final Debug,
release-test, Analyze, Release, ASAN, verifier, doctor, release-tooling, and
OpenSpec results are recorded above. The items below are historical
invocation/diagnostic failures retained for provenance, or external evidence
that is intentionally not represented as PASS.

- A focused Debug build of the changed indexstore/engine targets from the
  plain PowerShell shell failed before semantic compilation because the shell
  lacked MSVC/Windows SDK include environment (`cstdint`, `cstdio`, and
  `algorithm` were not found). This is an invocation/environment failure;
  rerun under `VsDevCmd.bat -arch=x64` before classifying product code.
- Historical workstate reports unsigned artifacts, placeholder production pins,
  and missing clean-host/elevated runtime evidence; final readiness rechecked
  the current HEAD and confirms those are external blockers, not local code
  failures.
- The first compile of the new USN engine regression exposed test-only type
  errors (UTF-16 literal vs. Windows wide string and const-map indexing). The
  test was corrected; the developer-environment rebuild and test pass above
  are the authoritative result.
- The installer package-trust library and dedicated adversarial test target
  are now present. Live
  install/uninstall/upgrade execution still requires a clean elevated host
  and production-signed artifacts.
- One plain-PowerShell CTest invocation failed because `ctest` is not on the
  shell PATH; the developer-command-prompt invocation above is authoritative.
- One build attempt used an incorrect `FASTFILES_NINJA_EXE` path and failed at
  CMake generator startup; no product compilation occurred.
- The first optimized-profile launch used invalid PowerShell quoting around the
  `Program Files (x86)` path and failed before CMake. The corrected developer
  command is authoritative.
- `ctest --preset release-test --output-on-failure` initially failed only at
  `ffdoc_drift_tests` (50/51); normalize the remediation-owned C++/header line
  endings and rerun the full release-test suite.
- The first post-remediation Analyze build failed on installer trust-helper
  C6262/C6387 diagnostics; those were product-code issues, not harness noise,
  and are fixed in committed `32938ca`. Subsequent Analyze and full verifier
  runs pass.
- The first 3M-capacity benchmark attempt was stopped after it produced no
  progress because the old analytical size search constructed large maps;
  the size formula/progress boundaries were then fixed. The completed run
  above is evidence, not a failure of product behavior.
- First 2M corpus attempt (`--scales 2000000 --diagnostics`) hit the 900 s
  command bound with exit 124 and no JSON output; the benchmark process was no
  longer present afterward. Because the command wrapper buffered stdout, this
  attempt did not retain the last stage in the tool result. This is a harness
  evidence gap, not a product PASS or FAIL; the next attempt must redirect
  stdout/stderr to an owned log file so progress survives a timeout.
- A post-audit focused configure from a plain `cmd.exe` environment failed
  before CMake generation because `FASTFILES_NINJA_EXE` was not inherited.
  The corrected VS developer-environment command configured successfully and
  is authoritative.
- The first compile of the new service write-deadline policy test passed
  service production sources but failed its test-only dynamic message type;
  the test now stores descriptions before passing `c_str()`, and the focused
  rebuild/test pass is authoritative.
- The redirected 2M attempt also hit exit 124 at the benchmark wrapper bound,
  but its log is actionable and the owned benchmark children were terminated
  after verification. This is an expected scale-limit observation, not a
  silent test result: 2M snapshot publication itself succeeded before the
  projection-query bottleneck.
- IPC slice commit: `f262219`, `refactor: use bounded overlapped pipe I/O`.
- Version-resource slice is implemented and focused-validated; its commit SHA
  is `054128b` (`build: embed canonical Windows version metadata`).
- Cancellation synchronization commit: `39cb55b`; both Debug and optimized
  full CTest gates now pass 51/51.

## External Blockers

- Authorized production signing provider/material and production leaf pins.
- Genuinely clean elevated Windows host for install → authenticated service →
  MFT/USN → uninstall evidence.
- Direct rendered UI validation if no usable visual automation/native pipe is
  available.

## Commits

- `a3680bf3858b063087eeddfbca0cd58a388afc3a` — bind installer payloads to
  trusted package identity, remove the elevated temp uninstall relay, and
  authenticate rollback candidates; add durable plan/workstate and setup
  security tests.
- `9e51d07d7ec9794e5bcb3ac9c3bf220cd52d49cd` — checkpoint installer-slice
  state and exact validation evidence.
- `32938ca` — keep installer trust hash/copy helpers `/analyze` clean and
  fail closed on invalid handles.
- `a414cab` — separate optimized test-release CI/verifier coverage from the
  production signed Release profile and validate package manifests end to end.
- `a47b705` — preserve authoritative metadata across USN rename/write/attribute
  deltas using explicit field masks in SQLite and projection state.
- `d69401f17c59719072f812e6f9e35458297e2230` — replace fixed double-slot
  snapshot publication with immutable generation-sized mappings and remove
  serialization/UI payload copies.
- `18a361e` — cancel superseded indexed searches with per-connection worker
  tokens, cooperative loop checks, and teardown-safe UI callback ownership.
- `f262219` — use bounded event-backed overlapped framing for both live pipe
  seams, remove UI polling and handshake helper-thread timeouts, and join
  listener-owned connection threads.
- `2154f62` — route every live index-service write through absolute-deadline
  overlapped framing and remove the write watchdog/helper-thread fallback.
- `054128b` — embed canonical Windows version metadata in all shipped
  executables and validate final artifacts.
- `39cb55b` — make the optimized cancellation regression deterministic.
- `99610e2` — make the shared pipe listener unconditionally overlapped and
  remove dormant synchronous cancellation code.
- `190adf3` — synchronize historical benchmark notes with the completed
  snapshot/cancellation remediation.

## Current Risks

- This is a broad multi-subsystem remediation; keep exactly one active slice
  and checkpoint before changing subsystem.
- Existing `docs/agent/WORKSTATE.md` is large and contains historical state;
  Git/source remain authoritative if it conflicts with this file.
- Installer and IPC changes can affect privileged security and teardown; no
  production claim is allowed until focused and full validation pass.
- FF-AUDIT-03 and GAP-E remain external: production signing/provider and a
  genuinely clean elevated host/visual validation environment are unavailable.
- The 2M benchmark reaches successful snapshot publication, but the synthetic
  projection/query stage exceeded the 900 s command bound after reporting
  61.23 s for the projection probe; this is a documented scalability limit,
  not a silent timeout.
- The tracked historical benchmark JSON is restored to its committed artifact;
  the Markdown corrections are documentation-only.

## Next Exact Action

If authorized production signing material and a clean elevated Windows host
become available, run `verify/release/privileged-release.ps1` with the signed
kit and import the install → authenticated service → MFT/USN → uninstall
evidence. Otherwise, no further local implementation action remains; retain
the external blockers as the release gate.
