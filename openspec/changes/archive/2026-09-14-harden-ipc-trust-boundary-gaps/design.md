## Context

See proposal.md. Foundation D3 (named pipes only) and D4 (symmetric mutual auth, periodic). `PipeListener::CreateInstance` (`src/ipc/src/PipeListener.cpp`) is the single production `CreateNamedPipeW`. Service heartbeat already calls `RevalidateClient`. Engine `VerifyServiceIdentity` runs only in `ConnectVerifyAndHandshake`. This change does **not** provision Authenticode pins (placeholder pins still fail closed).

## Goals / Non-Goals

**Goals:**
- `PIPE_REJECT_REMOTE_CLIENTS` on every listener instance.
- Engine re-verifies the service peer on the heartbeat cadence.

**Non-Goals:**
- Per-user UI pipe names / UI-side peer image check (same-privilege, current-user DACL, FIRST_INSTANCE).
- Production pin values; trust-store tests.
- `fftest` probe pipes (optional, not blocking).

## Decisions

### D1: Set the flag in `PipeListener::CreateInstance` dwPipeMode

`PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS`.

**Why:** One seam, both pipes inherit. Vista+ API; FastFiles is Windows-only.

**Rejected:** Per-caller opt-in — every production caller wants this. Test pipes created with `securityAttributes == nullptr` still get the flag (local tests use `\\.\pipe\...` and are local).

### D2: Reuse `VerifyServiceIdentity` after a successful HeartbeatAck

In `ActiveLoop`, after `ackOk`, call `VerifyServiceIdentity(pipe->handle)`. Failure breaks the loop the same way a heartbeat timeout does → Disconnected → degraded.

**Why:** Same checks as connect (image path under install dir + pin). Heartbeat cadence is already ~20s (`kHeartbeatInterval`). Matches "periodically" without a second timer.

**Rejected:** Re-validate only on process-id change — a replaced file at the same path would be missed. Relying on service self-staleness restart — complementary, not a substitute for the engine's D4 duty.

### D3: Test the pipe mode via a test-only getter or by creating a pipe and querying

`GetNamedPipeHandleState` does not return REJECT_REMOTE. Prefer a `FF_PIPE_LISTENER_TESTING` helper that exposes the last `dwPipeMode`, or compile-time assert that the CreateInstance expression includes the flag plus a unit that `#ifdef`s the mode constant. A documentation-only test is insufficient; the task list requires a Check() against the actual mode word used.

## Risks / Trade-offs

- **[Risk] Old Windows without PIPE_REJECT_REMOTE_CLIENTS.** → Mitigation: the flag is documented since Vista; FastFiles already uses Per-Monitor V2 DPI and other post-Vista APIs. If CreateNamedPipe fails, existing "refuse to start" path applies — do not silently retry without the flag.
- **[Risk] Periodic VerifyServiceIdentity hits disk/WinTrust every 20s.** → Mitigation: same cost the service already pays on heartbeat; accept it. If it becomes hot, a longer bound still MUST be finite and documented; do not drop the check.
- **[Risk] Placeholder pins make revalidation fail immediately in production unsigned builds.** → Mitigation: already fail closed at connect; this change does not make that worse. Debug/degraded remains the supported unsigned path.

## Migration Plan

Engine + service binaries pick up PipeListener. No protocol version bump. Rollback: revert.

## Open Questions

None.
