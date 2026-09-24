## Why

The preceding residual campaign fixed transport-fatal scan writes, but its task 1.4 still lacks deterministic coverage for a frame that fails after only some bytes reach the pipe. The supplied final-hardening mission also calls for an evidence-based provenance decision and a fresh hosted CI status check before any exact-SHA certification claim. Exploration found that strict provenance binding can accept a missing binary record and that the local parity report embeds a dated billing status. Its historical pass counts and billing result are starting checkpoints, not current proof.

## What Changes

- Add deterministic failure-at-byte-boundary coverage through the production IPC serializer and write path, then prove scan, engine, teardown, retry, and reconnect outcomes without weakening the three-process boundary.
- Repair reproduced fail-open provenance-verifier cases and test them independently of the digest decision. Audit commit/tree identity, dirty-state, binary hash, release-manifest, and exact-tree qualification checks against concrete mismatch scenarios. Record `IMPLEMENT`, `ALREADY COVERED`, or `NOT JUSTIFIED`; add an executable-source-tree digest only if it closes a remaining material gap.
- Inspect current GitHub Actions availability and the required job matrix; remove the local parity report's fixed hosted-status claim. If jobs can run, certify the final pushed SHA from actual job results; if account-level execution is blocked, retain `HOSTED_CI = BLOCKED_EXTERNAL` and give the owner a precise unlock action.
- Validate the final candidate with the applicable local ladder, independent adversarial review, and an evidence-bound handoff for remaining external requirements.

## Capabilities

### New Capabilities

- `residual-certification-integrity`: Deterministic transport-failure proof, conditional provenance assurance, and truthful exact-SHA local/hosted certification evidence.

### Modified Capabilities

None. The intended transport outcome is already specified by `residual-defect-closure-and-certification-completion`; this change adds proof and closes its unchecked task 1.4. A digest is conditional on investigation rather than a predetermined product contract.

## Impact

- Likely touch points: `src/ipc/src/PipeFraming.cpp`, `src/indexsvc/src/VolumeScanner.cpp` and `ServiceConnection.cpp`, engine receive/retry paths, focused CTest fixtures, and `verify/core/Provenance.psm1` for confirmed verifier defects. Build identity changes remain conditional on the digest decision.
- Certification touch points: `.github/workflows/ci.yml`, `verify/Invoke-CiParity.ps1`, exact-tree qualification, retained OpenSpec evidence, and the repository handoff. The foundation design's D2–D4 and Risks remain authoritative for process and IPC boundaries.
- No wire-protocol change, broad IPC rewrite, production signing/trust-policy change, account billing change, unrelated feature work, or host display/security mutation is planned.

## Open Questions

- Which narrow injection seam reaches `WriteExactOverlapped` while preserving the real serialization, cancellation, and connection lifecycle?
- Which portable scan-source and verifier seams can exercise batch failure, connection abort, and a fresh authorization decision without raw-volume privilege or production signing pins?
- After repairing direct verifier failures, does a source-tree digest reject a realistic artifact that commit identity, clean-tree qualification, binary SHA-256, and manifest binding would accept?
- Can the final pushed SHA obtain a complete hosted six-job run, or does GitHub still reject jobs before execution?
