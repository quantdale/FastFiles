# FastFiles Verification Harness

`verify/` is the autonomous runtime-verification harness for FastFiles: a fixed set of
non-interactive verbs that discover capabilities, run them against a selectable
**Environment Provider**, capture evidence into per-run artifact trees, repair what it
can, and gate OpenSpec archival on the result.

Entry point: `verify/verify.ps1` (PowerShell 7+). No global PATH dependency: the harness
locates the VS toolset via `vswhere` and drives CMake/Ninja from the toolset.

## Verbs and exit codes

Fixed, non-interactive verb set:

| Verb | Purpose | Mutates host? |
|------|---------|---------------|
| `build` | Configure + build (clean or incremental) against the current source | no |
| `install` | Install / upgrade / repair / uninstall drivers with integrity verification | yes (Tier-1) |
| `run` | Execute a capability suite; write the run tree | only via Tier-1 capabilities (elevated) |
| `diagnose` | On-failure diagnostics for a run | no |
| `report` | Project a run tree to MD/HTML/JSON/JUnit + performance/failure summaries | no |
| `repair` | Bounded autonomous repair loop: starts a fresh run, repairs its failures (Class A auto-applied, Class B flagged), re-runs repaired capabilities | yes (Class A fixes) |
| `gate` | Resolve the change's gate policy against the latest run | no |
| `list` | Enumerate discovered capabilities (incl. rejected ones) | no |
| `doctor` | Probe environment readiness; never provisions anything | no |
| `release-readiness` | Validate the Release artifact set, signatures, pins, clean-host preflight, and privileged-path prerequisites | report: no; privileged: only after all gates are ready |

Exit codes: `0` PASS, `1` FAIL, `2` SKIPPED, `3` HARNESS ERROR, `10` NOT-YET-IMPLEMENTED
(reserved; no verb currently scaffolds).

## One-time elevated execution context (task 11.1)

Tier-1 validation (privilege, object security, engine–service, IPC, install/service)
requires an administrator token to probe protected objects and to exercise install
lifecycle. The harness never runs elevated by default:

- **Interactive:** pass `-Elevate`. `verify.ps1` detects a non-elevated process, relaunches
  itself with a **one-time UAC approval prompt**, and the elevated child verifies it
  actually received an administrator token before doing any work (`Test-IsElevated`,
  `Fingerprint.psm1`). The child rejects a token-less relaunch as a harness error.
- **Unattended:** an approved elevated scheduled task is the supported path (declared in
  the install capability's environment requirements); the harness itself never requests
  elevation without `-Elevate`.
- Every run records its true execution context in `manifest.json`: OS build, toolset/SDK
  versions, `isElevated`, session id/kind, provider id, target identity. Tier gating
  evaluates capabilities against this fingerprint — a Tier-1 capability run without
  elevation is `SKIPPED(reason: elevation-required, requiredContext)`, never a silent pass.

### Teardown / clean-host guarantee

The active Environment Provider owns lifecycle: `provision → activate → collectLogs →
cleanup` (+ `snapshotRestore` where declared). The local provider's cleanup is
**mandatory and idempotent**: it runs even when a capability or provider phase failed, and
a failed teardown is itself a harness error. Every cleanup result is persisted to
`<run>/provider-cleanup.json` so a partially-degraded host is a documented, inspectable
outcome — the developer machine must be left without leftover installation, service, task,
or created account after a run.

## Destructive verbs and hardening rule (task 11.2)

The allowlisted destructive set is exactly: `install` (service registration, install-dir
ACL mutation, task registration) and Tier-1 `run` capabilities (mutating nothing but
exercising privileged probe paths). `repair` may auto-apply **Class A** fixes
(harness/config/environment) and re-run.

Hard rule, stated for reviewers and future contributors: **the harness never disables,
weakens, or bypasses product hardening to make a check pass.** Evidence of a real gap
(e.g. an unsigned installed binary, a permissive ACL) is reported as FAIL with
explanatory diagnostics; masking it is a regression of the harness itself.

Environment Provider selection: `-Provider` defaults to `local` (current machine; Tier-0
in-session, Tier-1 via on-demand elevation). Disposable/isolated adapters (Hyper-V,
VMware, VirtualBox, Windows Sandbox, CI runners) are declared behind the same versioned
interface (tasks 2.3/2.4) and are the recommended context for destructive validation;
the local provider must always remain safe to run on a developer machine.

## Adding a capability (task 11.3)

1. Create `verify/capabilities/<id>/` with:
   - `capability.json` — manifest validated against
     `verify/schemas/capability-manifest.schema.json`:
     `id` (`[a-z][a-z0-9-]*`), `interfaceVersion` (semver), `tier` (integer),
     `repairPosture` (`repair-supported` | `repair-unsupported` | `repair-unavailable`),
     `dependsOn` (optional, acyclic), and `entryPoints`:
     **`availability`, `run`, `diagnostics` required**; `baseline`, `repairHints`,
     `repair` optional.
   - `<Name>.psm1` — module exporting the declared entry points.
2. `availability(Fingerprint)` returns `{ Available, Reason, RequiredContext }`; a
   capability whose prerequisites are missing is `SKIPPED(reason, requiredContext)`.
3. `run(RunContext, Fingerprint, Options)` returns a result envelope (status
   PASS/FAIL/SKIPPED, reason, evidence, artifacts) per `result-envelope.schema.json` and
   writes payloads under `artifacts/<capability>/`; the core indexes envelopes only and
   never parses capability-specific formats.
4. `repair(RunContext, ...)` (only with `repairPosture: repair-supported`) owns that
   capability's fix; the orchestrator only coordinates. Return
   `{ fixClass, rootCause, action, outcome, reason }` — `fixClass` is
   `harness`/`config`/`environment`/`product-source`; a `product-source` fix is
   surfaced for review (`flagged-for-review`), never auto-applied.
5. Register new CTest executables in the component's `CMakeLists.txt` (test names equal
   target names, e.g. `ffprotocol_tests`); wire suites as Tier-0 capabilities emitting
   JUnit-compatible results.

**Interface-version policy:** only manifests whose `interfaceVersion` matches the
harness's supported range load; an incompatible manifest is recorded as a load
diagnostic (visible via `list`) — never silently dropped. Bump the interface version for
contract-breaking changes; additive entry points are backward compatible.

## Run trees, reports, and gate

Each run lives at `verify/runs/<change>/<timestamp>/`: `manifest.json` (fingerprint),
per-capability envelopes, `artifacts/<capability>/` payloads, `index.json`,
`repair-log.jsonl` (repair iterations), and `provider-cleanup.json`. Reports
(MD/HTML/JSON/JUnit + performance and failure summaries) are deterministic projections of
that tree — regenerating from an unchanged tree yields identical verdicts.

Gate policy: `verify/policies/<change>.json` (falls back to `_default.json`) declares
required capabilities, acceptable skip reasons, and whether performance regressions gate.
`gate` resolves each required capability to PASS/FAIL/SKIPPED/REQUIRED-BUT-UNAVAILABLE,
fails on any FAIL or REQUIRED-BUT-UNAVAILABLE, and also fails when the run contains a
product-source edit unrepresented in the change's tasks/specs (git-tracked files under
`src/`, `tests/`, `CMakeLists.txt`, `cmake/` modified after the run started).

## Running

```powershell
pwsh ./verify/verify.ps1 build
pwsh ./verify/verify.ps1 doctor
pwsh ./verify/verify.ps1 run -Change <change-id> -SkipAnalyze
pwsh ./verify/verify.ps1 install -Change <change-id> -Elevate   # one-time UAC
pwsh ./verify/verify.ps1 repair -Change <change-id>
pwsh ./verify/verify.ps1 gate -Change <change-id>
pwsh ./verify/verify.ps1 release-readiness -Mode report
pwsh ./verify/verify.ps1 release-readiness -Mode gate
pwsh ./verify/verify.ps1 release-readiness -Mode privileged -Elevate
```

`release-readiness` returns `2` (`BLOCKED_EXTERNAL`) when the current artifact set
is locally inspectable but authorized signing material, production pins, elevation,
or a genuinely clean host is unavailable. It returns `1` for a local artifact or
validation defect. `-Mode report` is read-only; `-Mode privileged` delegates to
`verify/release/privileged-release.ps1` and refuses to install over a contaminated
host. The signing wrapper accepts only explicitly supplied external certificate-store
or PFX configuration, rejects self-signed development certificates, and never writes
pins or private material into the repository. Every release kit must also contain
`FastFiles.package.manifest`, whose exact inventory, sizes, and SHA-256 hashes are
checked before clean-host or privileged execution.

The CMake `release` preset is the production-signed configuration and fails closed
without authorized pins. CI and the default `verify.ps1 build` use the explicitly
named `release-test` preset (`RelWithDebInfo` plus `FASTFILES_RELEASE_TEST_BUILD=ON`)
for optimized tests only; it does not bypass runtime authentication or represent
production signing evidence. Run `cmake --preset release` only inside the authorized
release workflow.

The portable clean-host kit is generated outside Git with:

```powershell
pwsh ./verify/release/New-ReleaseKit.ps1 -BuildRoot ./build/release -OutputDirectory <kit>
```

The kit's preflight is read-only and reports `CLEAN_HOST_PRECONDITION_FAILED`
instead of cleaning an existing FastFiles installation. A PASS evidence bundle can
be copied into a reviewed evidence directory with
`verify/release/import-clean-host-result.ps1`; that importer never changes OpenSpec
task state.

`verify/runs/` and `verify/baselines/` are execution evidence — untracked, not source.

## Which command when

- `ctest --test-dir build/debug -R <name> --output-on-failure` — fast iteration on a
  single test/target.
- `pwsh ./verify/verify.ps1 build` — the Debug + optimized test-release + Analyze gate; long-running.
- `cmake --preset release && cmake --build --preset release` — production Release, requiring external signing pins.
- `-SkipAnalyze` / `-SkipTests` / `-Configuration <cfg>` — quick-path levers for
  `build` (and `repair`).
- `pwsh ./verify/verify.ps1 gate` verdicts are time-dependent: unrepresented-edit
  detection compares live file mtimes against run start, so re-gating an old run
  later can change the verdict.
- `pwsh ./verify/intake.ps1 autonomous` can exit 0 with a gate FAIL recorded as an
  external blocker — check the run's gate verdict, not just the exit code.
