## 1. Named-pipe remote-client rejection

- [x] 1.1 Set `PIPE_REJECT_REMOTE_CLIENTS` on `PipeListener::CreateInstance` `dwPipeMode` together with the existing byte/wait flags (design D1). Do not add a silent fallback that retries without the flag.
- [x] 1.2 Add a Check() in `tests/ipc` that the mode word actually used to create the instance includes `PIPE_REJECT_REMOTE_CLIENTS` (design D3; `FF_PIPE_LISTENER_TESTING` getter is acceptable).

## 2. Engine periodic service-peer revalidation

- [x] 2.1 After a successful HeartbeatAck in `PrivilegedConnection::ActiveLoop`, call `VerifyServiceIdentity` on the live pipe; failure breaks the loop like a heartbeat timeout (design D2).
- [x] 2.2 Add or extend an engine test so a failing identity check on the heartbeat path transitions off Active (injectable verifier seam or existing handshake fixture). Do not require production pins.

## 3. Verification

- [x] 3.1 Run `ctest --test-dir build/debug -R "ffipc_listener|ffengine_client_handshake|ffengine_active_path" --output-on-failure`.
- [x] 3.2 Confirm `Commands.h` client→service allowlist is unchanged (no new privileged primitive).
