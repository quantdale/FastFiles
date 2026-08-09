## Why

The engineering audit (commit `52d33da0`) identified 2 CRITICAL, 4 HIGH, 5 MEDIUM, and 2 LOW findings that block production readiness. The most severe issues are: (1) placeholder Authenticode pins that make the privileged MFT/USN path non-functional, (2) engine discovery that launches executables without signature verification, (3) a deterministic infinite-loop bug in `FlatHashMap`, (4) an ARM64 publication-safety race in `SnapshotPublisher`, and (5) unbounded SQLite store growth. These must be addressed before the product can deliver on its architectural promise.

## What Changes

- **Provision and verify real Authenticode pins** for mutual authentication, or remove the placeholder authn code and explicitly document degraded mode as the only supported path.
- **Add signature verification to engine discovery** before launching any engine executable.
- **Fix FlatHashMap grow/occupancy logic** to prevent infinite loops at 100% load factor.
- **Add explicit memory ordering** to `SnapshotPublisher` for ARM64 correctness.
- **Replace busy-wait polling** in `ServiceConnection` with async I/O or event-driven reads.
- **Add SQLite VACUUM/ANALYZE scheduling** to prevent unbounded store growth.
- **Add in-memory projection eviction** to cap memory usage on large volumes.
- **Add snapshot format versioning** and generation mismatch detection.
- **Add structured diagnostics error codes** for observability.

## Capabilities

### New Capabilities

- `authenticated-engine-launch`: Signature verification for engine executables before launch, including path canonicalization and allowlist checks.
- `index-store-maintenance`: Periodic VACUUM, ANALYZE, and size-management for the SQLite store.
- `projection-eviction`: Configurable maximum projection size with LRU eviction policy.
- `structured-diagnostics`: Structured error codes alongside human-readable diagnostics messages.
- `snapshot-versioning`: Format version field and generation mismatch detection in shared memory snapshots.

### Modified Capabilities

- `ipc-reliability`: Async I/O for ServiceConnection and explicit memory barriers for SnapshotPublisher (ARM64 correctness).
- `data-structure-safety`: FlatHashMap grow trigger and max probe distance to prevent infinite loops.
- `security-hardening`: Authenticode pin provisioning and installer upgrade verification.

## Impact

- **Affected components:** `protocol` (ffprotocol), `engine` (FastFilesEngine), `indexstore` (ffindexstore), `ui` (FastFiles), `setup` (FastFilesSetup).
- **Security model:** Changes to mutual authentication and engine discovery affect the constrained-broker architecture documented in `establish-architecture-foundation/design.md`.
- **Build/test:** Requires MSVC `/W4 /WX` clean build; existing CTest tests must continue to pass. New tests for FlatHashMap, IPC, and snapshot publication are required.
- **User-facing:** Degraded mode behavior remains unchanged until real Authenticode pins are provisioned.
