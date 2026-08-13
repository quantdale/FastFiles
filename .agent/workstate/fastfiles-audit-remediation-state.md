# FastFiles Audit Remediation Workstate

## Mission

Comprehensively remediate the supplied latest deep-audit findings FF-AUDIT-01
through FF-AUDIT-10 plus validation gaps A–E, with secure architecture,
regression coverage, measurable scale behavior, deterministic CI/release
validation, and a durable handoff. Do not push or alter unrelated work.

## Repository State

- Branch: `main`
- HEAD: `a47b705` (release/CI plus USN data-correctness slices committed; snapshot redesign is the current uncommitted slice)
- `origin/main`: `403f8490aab019e02b5d82cef734ea76223d3396`
- Starting worktree: clean (`git status --short` produced no entries).
- Dirty files present before task: none.
- Remediation-owned modified files: the committed files above plus the current snapshot slice: `.agent/workstate/fastfiles-audit-remediation-state.md`, `src/protocol/include/ffprotocol/SnapshotFormat.h`, `src/protocol/src/SnapshotFormat.cpp`, `src/engine/src/SnapshotPublisher.h`, `src/engine/src/SnapshotPublisher.cpp`, `src/engine/src/UiServer.h`, `src/engine/src/UiServer.cpp`, `src/ui/src/EngineClient.h`, `src/ui/src/EngineClient.cpp`, `tests/engine/test_snapshot_publisher.cpp`, and `tests/engine/bench_large_index_scalability.cpp`.
- Unrelated dirty files to preserve: none observed at start; re-check before every commit.
- Historical repository workstate: `docs/agent/WORKSTATE.md` (contains older release/UI checkpoints and was stale relative to the starting HEAD). Mission plan: `.agent/execplans/fastfiles-audit-remediation.md`.
- Mission workstate: `.agent/workstate/fastfiles-audit-remediation-state.md`.

## Finding Matrix

| ID | Severity | Finding | Status | Implementation | Validation | Commit |
|----|----------|---------|--------|----------------|------------|--------|
| FF-AUDIT-01 | CRITICAL | Elevated installer trusts mutable sibling executables | IMPLEMENTED | Shared `ffsetup` package-trust seam binds manifest hash, approved Authenticode signer, stable file identity, non-reparse handle, `CREATE_NEW` staging, and same-volume promotion | `ffsetup_package_trust_tests` PASS; `FastFilesSetup` Debug build PASS; clean/elevated live install remains external | — |
| FF-AUDIT-02 | HIGH | Elevated uninstall executes a relay from user-writable `%TEMP%` | IMPLEMENTED | Removed temp relay and detached helper; installed image performs uninstall and schedules only its own deferred deletion | `ffsetup_installer_source_policy_tests` PASS; live in-use/elevated uninstall pending external host | — |
| FF-AUDIT-03 | CRITICAL release blocker | Production privileged path lacks final signed clean-host proof | BLOCKED_EXTERNAL | Local release workflow is fail-closed and deterministic; external signing/clean-host evidence unavailable | `verify.ps1 release-readiness -Mode report`: artifact integrity PASS; signing/pins/privileged path/clean host BLOCKED_EXTERNAL, exit 2 | — |
| FF-AUDIT-04 | HIGH | Fixed two-slot snapshot creates excessive memory/commit pressure | IN_PROGRESS | Replacing the fixed 2×256 MiB mapping with immutable generation-sized mappings, explicit format v2, and bounded 256 MiB payload rejection | Existing benchmark history is inconclusive at 1M; new 10K/100K/500K/1M/2M diagnostics pending | — |
| FF-AUDIT-05 | MEDIUM | USN incremental updates erase valid creation/access timestamps | VALIDATED | Explicit `EntryFieldMask` partial-update semantics now merge SQLite and projection state; USN marks creation/access absent | Focused 3/3 tests, full Debug/release-test suites 51/51, full verifier PASS; commit `a47b705` | `a47b705` |
| FF-AUDIT-06 | MEDIUM | Indexed searches are stale-result-safe but not cancellable | OPEN | Scoped cancellation pending | Supersession performance test pending | — |
| FF-AUDIT-07 | MEDIUM | UI↔Engine IPC uses `PeekNamedPipe` polling | OPEN | Shared overlapped framing design pending | IPC baseline pending | — |
| FF-AUDIT-08 | MEDIUM | Synchronous timeout can exceed deadline after helper join | OPEN | Bounded overlapped I/O redesign pending | Deadline/stall tests pending | — |
| FF-AUDIT-09 | MEDIUM | Upgrade rollback may reactivate unauthenticated prior binaries | IMPLEMENTED | Backup/restore requires schema-2 manifest, exact inventory, approved signatures, identity/hash validation, and handle-bound staged promotion | Rollback candidate matrix in `ffsetup_package_trust_tests` PASS; injected live upgrade failure pending external host | — |
| FF-AUDIT-10 | LOW | Release binaries lack embedded version-resource metadata | OPEN | Canonical VERSIONINFO source pending | Artifact inspection pending | — |
| GAP-A | HIGH | Release CI reproducibility/signing pins | IMPLEMENTED | Added explicit optimized unsigned `release-test` profile; production `release` remains pin-enforced; package manifest validation is required by kit/clean-host/privileged paths | Release-test 51/51, release-tooling 27/27; CI execution remains external | — |
| GAP-B | HIGH | Installer regression coverage | IMPLEMENTED | First-class setup package-trust and installer source-policy targets cover manifest, signer, hash, identity, reparse, staging, promotion, rollback candidates, and relay removal | Focused setup CTest PASS, 4/4 | — |
| GAP-C | MEDIUM | ASAN not continuously gated | IMPLEMENTED | Added a CI ASAN protocol-fuzz job with VS developer-runtime activation; exclusions are limited to the applicable core target | Local ASAN fuzz pass; CI execution remains external | — |
| GAP-D | MEDIUM | Large-index benchmark lacks actionable progress | IN_PROGRESS | Adding stage/progress output and 2M default tier to the benchmark | New benchmark run pending after snapshot implementation | — |
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
and no detached threads or polling.

### G. Release Metadata / Migration Readiness

One version source must feed all executable resources. Any snapshot/protocol or
SQLite change must explicitly define versioning, compatibility, migration, and
fail-closed recovery.

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

## Validation Evidence

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

## Known Failures

- A focused Debug build of the changed indexstore/engine targets from the
  plain PowerShell shell failed before semantic compilation because the shell
  lacked MSVC/Windows SDK include environment (`cstdint`, `cstdio`, and
  `algorithm` were not found). This is an invocation/environment failure;
  rerun under `VsDevCmd.bat -arch=x64` before classifying product code.
- Historical workstate reports unsigned artifacts, placeholder production pins,
  and missing clean-host/elevated runtime evidence; these must be rechecked
  against current HEAD and not treated as current PASS/FAIL without commands.
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

## Current Risks

- This is a broad multi-subsystem remediation; keep exactly one active slice
  and checkpoint before changing subsystem.
- Existing `docs/agent/WORKSTATE.md` is large and contains historical state;
  Git/source remain authoritative if it conflicts with this file.
- Installer and IPC changes can affect privileged security and teardown; no
  production claim is allowed until focused and full validation pass.

## Next Exact Action

Rerun the deterministic 10K/100K/500K snapshot benchmark with the direct
two-pass writer and in-place UI parser, recording stage timings and memory;
then commit the validated snapshot architecture slice before starting search
cancellation.
