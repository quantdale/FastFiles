# Post-review scan-authority repair

## Finding and red reproducer

The external review at `C:\Users\palac\AppData\Local\Temp\grok-palac\grok-review-c0e9f540.md`
identified a liveness defect in the prior local retirement counter.

`TestReplacementOnlyCompletionAfterSupersedeIsAuthoritative` was added before
the repair. Its fixture applies an initial batch, drops a paused batch, resumes
the initial scan, and models the service joining the stopped worker before it
can write a terminal. The only terminal then belongs to the replacement.

The focused manager test failed on the uncorrected implementation with both
of these assertions:

- `the replacement's only terminal completes the recovered initial scan`
- `the replacement-only terminal releases the in-flight scan ownership`

The old worker's stop path is real: `VolumeScanner.cpp::ScanOutput::Finish`
returns `Cancelled` without a terminal when `shouldStop` is set. The service
calls `StopAndJoin` before it launches a replacement in `ServiceConnection.cpp`.
Therefore a local terminal-retirement count cannot distinguish the
replacement's only volume-id-only terminal from an old terminal that was never
written.

## Repair

Protocol `{2,3}` adds the reply-only `ScanAttemptStarted` frame with an exact
`VolumeId` payload. The service writes it under the existing connection write
mutex after `StopAndJoin` returns and before it registers or launches the
replacement worker. The engine keeps a coverage-hole replacement provisional,
rejecting scan batches and terminals until that marker arrives. After the
marker, existing exact-length `ScanComplete` and `ScanFailed` frames retain
their authority unchanged.

A pre-{2,3} peer resets the connection generation for this rare replacement
path. A failed scan-start write follows the same reset path because a failed
bounded pipe write is not proof that the service did not receive a request.

## Focused validation

On 2026-09-28, the Debug build completed for the protocol, service, engine,
and focused test targets. The following CTest selection passed 7/7:

```text
ffprotocol_tests
ffcommand_surface_tests
ffvolumescanner_terminal_tests
ffservice_exception_boundary_policy_tests
ffengine_volume_session_manager_tests
ffengine_legacy_scan_failure_binding_tests
ffengine_privileged_connection_seam_policy_tests
```

The manager regression now drives the actual `PrivilegedConnection` frame
dispatcher for the marker and covers: an old terminal before the barrier, a
terminal-free supersede whose replacement emits the only terminal, a staged
pre-{2,3} peer reset, and a failed scan-start reset. The service source-policy
test verifies `StopAndJoin` -> negotiated barrier -> replacement registration
ordering and the shared write-mutex framing path.
