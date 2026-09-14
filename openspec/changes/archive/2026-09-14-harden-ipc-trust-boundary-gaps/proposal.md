## Why

Foundation design D3 chose named pipes as the only kernel object so one ACL story could be gotten right; D4 requires symmetric mutual authentication **re-validated periodically**, not only at handshake. Two gaps remain in the current tree:

1. PA-016: `PipeListener::CreateInstance` never sets `PIPE_REJECT_REMOTE_CLIENTS`. Repo-wide zero hits. The privileged control pipe and the per-session UI pipe therefore accept remote SMB named-pipe clients. Peer image+pin checks still fail a remote privileged client closed, but the kernel hardening layer the two-seam threat model relies on is missing, and the UI pipe has no peer check.
2. PA-034: the service re-validates the client on every Heartbeat (`RevalidateClient`); the engine verifies the service only inside `ConnectVerifyAndHandshake`. AGENTS.md and D4 claim both directions.

Neither gap is owned by an existing OpenSpec change. Production signing/pins remain a separate external campaign — this change does not provision pins.

## What Changes

- `PipeListener` SHALL create every instance with `PIPE_REJECT_REMOTE_CLIENTS` in `dwPipeMode` (Vista+; FastFiles is Windows-only and already assumes modern Win32).
- On an Active privileged connection, the engine SHALL re-run service image-path + pinned-signature verification on the heartbeat cadence (or an equivalent periodic bound) and treat failure like a disconnect → degraded mode.
- Tests: listener creation mode includes the flag; engine heartbeat path rejects a swapped/unpinned peer without waiting for process restart.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `privileged-index-service`: Named Pipe Access Control includes remote-client rejection; Symmetric Mutual Authentication includes engine→service periodic re-validation.
- `index-engine`: an Active privileged connection that fails periodic service-peer re-validation SHALL transition to Disconnected / degraded.

## Impact

- `src/ipc/src/PipeListener.cpp` (single production `CreateNamedPipeW`); both service and UI pipes inherit the flag.
- `src/engine/src/PrivilegedConnection.cpp` heartbeat loop.
- Tests: `tests/ipc/test_pipe_listener_lifecycle.cpp` (or a focused pipe-mode test); engine handshake/resilience tests.
- Foundation D3/D4 apply. No new command types. No pin provisioning.
- Non-goals: per-user UI pipe names; UI-side peer authentication (same-privilege, current-user DACL + FIRST_INSTANCE remain); production Authenticode pin values; `fftest` probe pipes (optional follow-up).

## Open Questions

- None. Remote-reject is a pipe-mode bit; engine revalidation reuses `VerifyServiceIdentity` on the heartbeat cadence.
