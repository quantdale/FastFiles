# Implementation and validation — harden-ipc-trust-boundary-gaps

Applied 2026-09-14 on `feat/refero-frontend-overhaul` (change proposed at `d214f1e`).

## What changed

- `PipeListener::CreateInstance` creates every instance with
  `PIPE_REJECT_REMOTE_CLIENTS` in the `dwPipeMode` word (byte-type/byte-readmode
  wait mode unchanged); both the privileged service control pipe and the
  per-session UI pipe inherit it, with no silent retry without the flag
  (design D1). The exact mode word is recorded in a member so the lifecycle
  test can assert the real value.
- The engine re-runs `VerifyServiceIdentity` on the live pipe after every
  successful `HeartbeatAck` in `PrivilegedConnection::ActiveLoop`; a failure is
  treated exactly like a heartbeat timeout — the connection leaves Active, the
  loop tears down, and the engine remains in degraded mode until a later
  successful handshake (design D2). No production pins are provisioned by this
  change; placeholder pins continue to fail closed.

## Files

- `src/ipc/src/PipeListener.cpp`, `src/ipc/include/ffipc/PipeListener.h`
- `src/engine/src/PrivilegedConnection.{h,cpp}`
- `tests/ipc/test_pipe_listener_lifecycle.cpp`
- `tests/engine/test_volume_session_manager.cpp`, `tests/engine/CMakeLists.txt`

## Tests added

- `ffipc_listener_lifecycle_tests`: every started listener instance's recorded
  creation mode includes `PIPE_REJECT_REMOTE_CLIENTS`.
- `ffengine_volume_session_manager_tests`: a fake service completes the
  handshake and answers the first heartbeat; an injectable verifier
  (`FF_PRIVILEGED_CONNECTION_TESTING`, no production pins) returns true for the
  connect-time check and false for the first Active revalidation, and the test
  asserts Active was reached, verification ran again on the heartbeat cadence,
  and the connection left Active. Skips cleanly when the `FastFiles.Svc.Ctrl`
  name is already held.

## Validation

- `ctest --test-dir build/debug -R ffipc_listener --output-on-failure`: PASS.
- `ctest --test-dir build/debug -R ffengine_volume_session --output-on-failure`:
  PASS (including the new heartbeat-revalidation case).
- `git diff` confirms `src/protocol/include/ffprotocol/Commands.h` is
  unchanged: no new privileged primitive was added.
- Full Debug CTest on the exact normalized tree (2026-09-14): **104/104 PASS**
  (267 s), including `ffdoc_drift_tests` and `ffproject_status_ps_tests`.
- `openspec validate --changes --strict`: 56/56 passed.
