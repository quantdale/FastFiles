# Design: Certification Truthfulness Repair and Recertification

## Architectural Overview

This design addresses certification truthfulness through provable execution, exact provenance, fail-closed contracts, and machine-readable authority.

Owner scope directive (2026-09-04): vendor-specific VM-provider execution/testing is out of scope for FastFiles local completion. Generic provider lifecycle/fake-adapter infrastructure may remain, but no provider-specific VM run is a completion gate unless the owner explicitly reopens it.

## 1. Long-Run Certification Runner

`Invoke-LongRunCertification.ps1` must discover the exact intended CTest target before running it. Zero matches fail as `INVALID_TARGET`; ambiguous matches fail; direct executables are preferred where appropriate. Each iteration records executable identity, timing, stdout/stderr/exit code, and evidence that the intended test actually executed.

## 2. UIA Long-Run Adapter

`Invoke-UiaLongRunCertification.ps1` wraps real semantic FastFiles UIA scenarios:

- `application-lifecycle`: launch → semantic readiness → graceful shutdown → zero-residue check;
- `rapid-input`: repeated command-palette/search/pane transitions with explicit semantic assertions;
- `core-smoke`: bounded semantic smoke execution.

Results record min/median/p95/max timing, per-iteration assertions, first failure, cleanup, and schema-compliant provenance.

## 3. Resource Soak Engine

Use explicit phases:

1. `LAUNCH`
2. `READY_WAIT`
3. `WARMUP`
4. `STEADY_STATE_BASELINE`
5. `MEASURED_SOAK`
6. `SHUTDOWN`
7. `POST_SHUTDOWN_RESIDUE_CHECK`

Measured steady state records working set, private bytes, handles, threads, GDI/USER objects, CPU, and relevant process identity. Interactive mode executes real semantic transitions; idle mode is explicitly idle and must not emit fake activity records. Overall PASS requires survival, required functional activity, resource stability, graceful shutdown, and zero campaign-owned residue.

## 4. Provider Taxonomy and Scope

Keep **generic environment-provider infrastructure** distinct from **real visual-review provider certification**.

- Generic environment-provider state-machine/fake-adapter tests may cover phase ordering, provenance, cleanup, interruption, and failure classification.
- `Invoke-VisionProviderCertification.ps1` / the real visual-provider lane concerns Visual Intelligence review and cannot be satisfied by fake critic output.
- Vendor-specific VM-provider provisioning, guest channels, credentials, guest execution, and rollback testing are owner-excluded from FastFiles local completion.

External-frontier mappings for in-scope work must point to semantically capable runners. Physical/interactive tasks map to real UIA/visual scenarios, not nonexistent generic targets. Owner-excluded vendor-specific VM tasks must not reappear as local completion work merely because provider-specific machinery is unavailable.

## 5. Artifact Integrity

Use `evidence-manifest.json` and/or sidecar hashes computed over final artifact bytes. Avoid naive self-referential hashes. Normalize retained paths to repository-relative logical paths and generate human-readable summaries from authoritative JSON rather than manually transcribing dynamic values.

## 6. Exact-Tree Recertification

Authoritative evidence is generated only after implementation is committed and the source tree is clean. Results bind the intended source commit/tree and executable hashes. Development evidence remains explicitly non-authoritative.

## 7. OpenSpec and Hosted Binding

Lifecycle completion requires durable repository evidence and, where policy requires, exact commit plus hosted-run binding. If a documentation binding commit moves the tip, that new final tip must also be hosted-certified.

## Safety Invariants

- No secret leakage.
- No raw disk writes.
- No synthetic execution PASS.
- No retry-to-green as defect resolution.
- No destructive cleanup of foreign state.
- No vendor-specific VM-provider testing unless the owner explicitly re-enables it.
