# FastFiles Audit Remediation Workstate

## Mission

Comprehensively remediate the supplied latest deep-audit findings FF-AUDIT-01
through FF-AUDIT-10 plus validation gaps A–E, with secure architecture,
regression coverage, measurable scale behavior, deterministic CI/release
validation, and a durable handoff. Do not push or alter unrelated work.

## Repository State

- Branch: `main`
- HEAD: `403f8490aab019e02b5d82cef734ea76223d3396`
- `origin/main`: `403f8490aab019e02b5d82cef734ea76223d3396`
- Starting worktree: clean (`git status --short` produced no entries).
- Dirty files present before task: none.
- Remediation-owned modified files: `.agent/execplans/fastfiles-audit-remediation.md`, `.agent/workstate/fastfiles-audit-remediation-state.md`, `src/engine/src/IndexPipeline.cpp`, `src/indexstore/include/ffindexstore/EntryRecord.h`, `src/indexstore/include/ffindexstore/Projection.h`, `src/indexstore/src/Projection.cpp`, `src/indexstore/src/Store.cpp`, `tests/engine/test_index_pipeline.cpp`, `tests/indexstore/test_projection.cpp`, `tests/indexstore/test_store.cpp`.
- Unrelated dirty files to preserve: none observed at start; re-check before every commit.
- Historical repository workstate: `docs/agent/WORKSTATE.md` (contains older release/UI checkpoints and was stale relative to the starting HEAD). Mission plan: `.agent/execplans/fastfiles-audit-remediation.md`.
- Mission workstate: `.agent/workstate/fastfiles-audit-remediation-state.md`.

## Finding Matrix

| ID | Severity | Finding | Status | Implementation | Validation | Commit |
|----|----------|---------|--------|----------------|------------|--------|
| FF-AUDIT-01 | CRITICAL | Elevated installer trusts mutable sibling executables | IMPLEMENTED | Shared `ffsetup` package-trust seam binds manifest hash, approved Authenticode signer, stable file identity, non-reparse handle, `CREATE_NEW` staging, and same-volume promotion | `ffsetup_package_trust_tests` PASS; `FastFilesSetup` Debug build PASS; clean/elevated live install remains external | — |
| FF-AUDIT-02 | HIGH | Elevated uninstall executes a relay from user-writable `%TEMP%` | IMPLEMENTED | Removed temp relay and detached helper; installed image performs uninstall and schedules only its own deferred deletion | `ffsetup_installer_source_policy_tests` PASS; live in-use/elevated uninstall pending external host | — |
| FF-AUDIT-03 | CRITICAL release blocker | Production privileged path lacks final signed clean-host proof | BLOCKED_EXTERNAL | Local release workflow is fail-closed and deterministic; external signing/clean-host evidence unavailable | `verify.ps1 release-readiness -Mode report`: artifact integrity PASS; signing/pins/privileged path/clean host BLOCKED_EXTERNAL, exit 2 | — |
| FF-AUDIT-04 | HIGH | Fixed two-slot snapshot creates excessive memory/commit pressure | OPEN | Measure first; bounded publication redesign pending | Existing benchmark history is inconclusive at 1M | — |
| FF-AUDIT-05 | MEDIUM | USN incremental updates erase valid creation/access timestamps | IMPLEMENTED | Explicit `EntryFieldMask` partial-update semantics now merge SQLite and projection state; USN marks creation/access absent | Store, projection, and end-to-end engine tests pass, including restart, reconciliation, and delete sequence | — |
| FF-AUDIT-06 | MEDIUM | Indexed searches are stale-result-safe but not cancellable | OPEN | Scoped cancellation pending | Supersession performance test pending | — |
| FF-AUDIT-07 | MEDIUM | UI↔Engine IPC uses `PeekNamedPipe` polling | OPEN | Shared overlapped framing design pending | IPC baseline pending | — |
| FF-AUDIT-08 | MEDIUM | Synchronous timeout can exceed deadline after helper join | OPEN | Bounded overlapped I/O redesign pending | Deadline/stall tests pending | — |
| FF-AUDIT-09 | MEDIUM | Upgrade rollback may reactivate unauthenticated prior binaries | IMPLEMENTED | Backup/restore requires schema-2 manifest, exact inventory, approved signatures, identity/hash validation, and handle-bound staged promotion | Rollback candidate matrix in `ffsetup_package_trust_tests` PASS; injected live upgrade failure pending external host | — |
| FF-AUDIT-10 | LOW | Release binaries lack embedded version-resource metadata | OPEN | Canonical VERSIONINFO source pending | Artifact inspection pending | — |
| GAP-A | HIGH | Release CI reproducibility/signing pins | INVESTIGATING | Inspect current release workflows and fail-closed policy | Release readiness baseline is fail-closed; CI inspection pending | — |
| GAP-B | HIGH | Installer regression coverage | IMPLEMENTED | First-class setup package-trust and installer source-policy targets cover manifest, signer, hash, identity, reparse, staging, promotion, rollback candidates, and relay removal | Focused setup CTest PASS, 4/4 | — |
| GAP-C | MEDIUM | ASAN not continuously gated | INVESTIGATING | Inspect presets/CI and wire applicable coverage | ASAN fuzz target built and passed under VS developer environment; CI inspection pending | — |
| GAP-D | MEDIUM | Large-index benchmark lacks actionable progress | OPEN | Incremental stage diagnostics pending | Prior bounded run inconclusive | — |
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
- No code/design decision has yet been made for installer or snapshot changes;
  measure and inspect current abstractions before selecting complexity.

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
- The installer currently has no package-trust library or dedicated
  adversarial test target; this was resolved in the current slice. Live
  install/uninstall/upgrade execution still requires a clean elevated host
  and production-signed artifacts.
- One plain-PowerShell CTest invocation failed because `ctest` is not on the
  shell PATH; the developer-command-prompt invocation above is authoritative.
- One build attempt used an incorrect `FASTFILES_NINJA_EXE` path and failed at
  CMake generator startup; no product compilation occurred.

## External Blockers

- Authorized production signing provider/material and production leaf pins.
- Genuinely clean elevated Windows host for install → authenticated service →
  MFT/USN → uninstall evidence.
- Direct rendered UI validation if no usable visual automation/native pipe is
  available.

## Commits

- None yet.

## Current Risks

- This is a broad multi-subsystem remediation; keep exactly one active slice
  and checkpoint before changing subsystem.
- Existing `docs/agent/WORKSTATE.md` is large and contains historical state;
  Git/source remain authoritative if it conflicts with this file.
- Installer and IPC changes can affect privileged security and teardown; no
  production claim is allowed until focused and full validation pass.

## Next Exact Action

Audit and patch release/CI reproducibility: add an explicit optimized
unsigned-test-release policy separate from production signed Release, wire
package-manifest generation/validation into the release kit, and add the
applicable ASAN job without weakening production pin enforcement.
