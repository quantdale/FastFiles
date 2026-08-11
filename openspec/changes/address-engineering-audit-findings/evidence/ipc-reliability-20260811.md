# IPC reliability evidence — 2026-08-11

## Scope

Completed OpenSpec tasks 2.1–2.3 in the `ipc-reliability` capability. The
change removes service-side `PeekNamedPipe`/`Sleep(2)` polling without changing
the closed command surface, pipe ACLs, mutual authentication, or signing
policy.

## Implementation

- The service Ctrl-pipe listener can create instances with
  `FILE_FLAG_OVERLAPPED`; its connect operation is also completed through an
  event and `GetOverlappedResult`.
- ServiceConnection reads the handshake and active command frames through
  bounded overlapped `ReadFile` operations. Header and payload reads share one
  absolute idle deadline, and a timeout cancels the pending operation before
  the existing reject-by-close teardown.
- Privileged service writes use the matching overlapped framing helper. The
  write-deadline watchdog now cancels a pending pipe operation with
  `CancelIoEx`, while retaining the synchronous-thread cancellation fallback
  for diagnostic/test handles.
- `ReadFrameWithTimeout` provides the bounded non-overlapped fallback used by
  tests, so test fixtures do not need to weaken the production listener mode.

## Focused validation

- `cmake --build --preset debug --target ffipc_framing_tests FastFilesIndexSvc`
  — PASS.
- `ctest --test-dir build/debug -R ffipc_framing_tests --output-on-failure` —
  PASS (1/1).
- Direct framing coverage passed: maximum-size synchronous frame, malformed
  header rejection, overlapped completion, overlapped timeout, overlapped
  service write, and synchronous fallback completion/timeout.

The full Debug/Analyze/Release, CTest, verifier, and strict OpenSpec gates are
recorded below after the final validation pass. Changed source and evidence
files are normalized to the repository's CRLF convention before commit.

## Full validation

- x64 Debug build — PASS (all configured targets).
- x64 Debug CTest — PASS (45/45).
- x64 Analyze build — PASS.
- x64 Release build — PASS (all configured targets).
- x64 Release CTest — PASS (45/45).
- `pwsh ./verify/verify.ps1 build` — PASS (`windows-build-validation`).
- `npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all --strict`
  — PASS (45/45).
