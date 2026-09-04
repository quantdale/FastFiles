# FastFiles Verification Harness

`verify/` is the runtime-verification harness for FastFiles. It discovers capabilities, runs them through a selectable generic Environment Provider, writes isolated run evidence, diagnoses failures, supports bounded repair where allowed, and projects release/OpenSpec truth from machine-readable results.

Entry point: `verify/verify.ps1` (PowerShell 7+).

## Owner scope directive — 2026-09-04

Vendor-specific VM-provider execution and testing are explicitly **out of scope** for FastFiles local completion and future verification campaigns unless the owner explicitly reverses this directive.

Generic provider lifecycle/provenance/cleanup code and deterministic fake-adapter contract tests may remain repository infrastructure. They do **not** create a completion requirement for a specific VM provider, guest image, guest credential, guest channel, or end-to-end VM run.

Do not add vendor-specific VM testing to current remaining-local-work lists, verification ladders, release-readiness gates, future-agent prompts, or operator runbooks under the current scope.

## Verbs

| Verb | Purpose |
| --- | --- |
| `build` | Configure/build/test supported local configurations. |
| `install` | Run approved install/upgrade/repair/uninstall validation. |
| `run` | Execute a capability suite and write a run tree. |
| `diagnose` | Collect bounded diagnostics for a selected run. |
| `report` | Project a run tree to human/machine reports. |
| `repair` | Run the bounded capability-owned repair loop where supported. |
| `gate` | Resolve a change's gate policy against an explicitly selected run. |
| `list` | Enumerate discovered capabilities and load diagnostics. |
| `doctor` | Read-only environment/tool readiness inspection. |
| `release-readiness` | Compose release-facing build/test/policy/runtime/external prerequisites. |
| `release-candidate` | Produce unsigned-validation candidate evidence or validate externally signed production inputs. |
| `status` | Generate current project/frontier state from Git, OpenSpec, tests, and available evidence. |

Exit codes remain repository-defined: PASS, FAIL, SKIPPED/BLOCKED, HARNESS_ERROR, and explicitly not-yet-implemented states where applicable. Always trust the selected result artifact rather than console color alone.

## Elevated execution

Tier-1 validation can require administrator privileges for protected objects, install/service lifecycle, and privileged NTFS observation.

- Interactive elevation requires an explicit `-Elevate` request and normal UAC approval.
- Unattended elevation must use an already approved operator-owned mechanism.
- The harness never silently elevates itself.
- Every run records the actual execution context and applies capability availability/gating against that context.

## Provider contract

The generic Environment Provider lifecycle is:

`preflight → provision → activate → collectLogs → cleanup`

with restore/rollback only where the selected external adapter explicitly supports it.

Provider failures and cleanup failures are first-class evidence. An unavailable external provider never silently falls back to local execution. The `local` provider must remain safe and idempotent.

Under current owner scope, generic provider contract/fake-adapter tests are allowed; **provider-specific VM execution is not required**.

## Hardening and destructive-operation rule

The harness must never disable, weaken, or bypass product hardening to obtain a PASS. Real unsigned binaries, bad ACLs, stale evidence, malformed results, trust failures, or cleanup failures remain visible.

Destructive behavior must be explicit, bounded, and scoped to campaign-owned resources. Never delete foreign files, terminate unrelated processes, clear machine-wide logs, overwrite unknown installations, write raw disk sectors, or manufacture credentials.

## Adding a capability

A capability under `verify/capabilities/<id>/` should declare:

- stable id and supported interface version;
- tier/context requirements;
- repair posture;
- positive timeout;
- optional acyclic dependencies;
- availability, run, and diagnostics entry points;
- optional baseline/repair helpers.

Availability reports precise missing context. Execution emits one valid result envelope with identity, status, timing, evidence/artifacts, and cleanup state. Missing, duplicate, malformed, late, or cross-run results fail closed.

## Run trees and evidence

Each selected run owns its own directory under `verify/runs/`. The manifest/result/envelopes/artifacts/index/repair log/cleanup evidence must all bind to that run identity. Reporting must be deterministic from the selected run tree.

Do not guess the latest run. `report`, `gate`, and existing-run diagnosis require an explicit run timestamp/id or exact result path where supported.

Authoritative evidence claiming committed provenance must bind to the intended clean source commit/tree and executable hashes. Development evidence from a dirty tree remains development evidence.

## Status and lifecycle truth

`verify.ps1 status` / `verify/status/Get-ProjectStatus.ps1` generate the current machine-readable project/frontier state. Current generated status and `openspec/lifecycle.json` take precedence over stale historical narrative.

For in-scope certification work, distinguish:

- `NOT_IMPLEMENTED` — local runner/machinery still incomplete;
- `READY_EXTERNAL` — local machinery complete; only a real external prerequisite remains;
- `EXECUTED` — real external evidence exists and validates.

Owner-excluded vendor-specific VM work must not be projected as active local work solely because a provider-specific path is absent or unexecuted.

## Release readiness

Release-facing composition must never infer production readiness from unsigned-validation, skipped lanes, test PKI, or unavailable external prerequisites.

Production signing uses only explicitly supplied authorized external material. Privileged NTFS certification must remain raw-read-only and requires real default MFT/USN observation plus fixture ground-truth comparison. Clean-host certification separates audit readiness from an actually executed candidate journey. Real vision review requires a configured real visual provider and sanitized inputs.

## Common commands

```powershell
pwsh ./verify/verify.ps1 build
pwsh ./verify/verify.ps1 doctor
pwsh ./verify/verify.ps1 list
pwsh ./verify/verify.ps1 run -Change <change-id> -SkipAnalyze
pwsh ./verify/verify.ps1 install -Change <change-id> -Elevate
pwsh ./verify/verify.ps1 repair -Change <change-id>
pwsh ./verify/verify.ps1 report -Change <change-id> -RunTimestamp <timestamp>
pwsh ./verify/verify.ps1 gate -Change <change-id> -RunId <run-id>
pwsh ./verify/verify.ps1 status
pwsh ./verify/verify.ps1 release-readiness -Mode report
pwsh ./verify/verify.ps1 release-readiness -Mode compose -Format json
pwsh ./verify/verify.ps1 release-candidate -ReleaseMode unsigned-validation
pwsh ./verify/status/New-ReleaseReadinessReport.ps1 -SkipCtest
```

For fast test iteration:

```powershell
ctest --test-dir build/debug -R <name> --output-on-failure
```

Use production Release only in the authorized release workflow with real signing prerequisites.

## FastFiles closure priorities

Local verification follows the lifecycle-active campaign recorded in
docs/agent/WORKSTATE.md and openspec/lifecycle.json. The NTFS observer,
visual-matrix, and soak-gate closure work described there is complete
locally; the remaining frontier is external-only (elevated NTFS,
physical display/DPI, provider/guest, production signing, clean-host).

Vendor-specific VM-provider testing is intentionally skipped and must not be reintroduced without an explicit owner scope change.
