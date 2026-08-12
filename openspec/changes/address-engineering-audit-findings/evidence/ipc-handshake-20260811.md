# Real named-pipe handshake evidence — 2026-08-11

## Scope

Completed OpenSpec task 8.3. The test exercises the protocol handshake request
and acknowledgement across a real Windows named-pipe pair, including the
service-side overlapped read/write path. It does not bypass production
Authenticode, ACL, or mutual-authentication policy.

## Validation

- `ffipc_framing_tests` creates a real `\\.\pipe\...` pair with an overlapped
  server handle and a synchronous client handle.
- A Handshake request crosses the pipe and is parsed asynchronously; the test
  checks its message type, exact payload size, and compatible protocol version.
- A HandshakeAck crosses back through the overlapped service write helper; the
  test checks its message type and exact payload size.
- `cmake --build --preset debug --target ffipc_framing_tests` — PASS.
- `ctest --test-dir build/debug -R ffipc_framing_tests --output-on-failure` —
  PASS (1/1).

The test remains a Tier-0 wire/transport check; live signed service handshake
evidence remains external because production pins and elevation are intentionally
unchanged.

## Full validation

- x64 Debug/Analyze/Release builds — PASS.
- x64 Debug CTest — PASS (45/45).
- x64 Release CTest — PASS (45/45).
- pwsh ./verify/verify.ps1 build — PASS (windows-build-validation).
- npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all --strict
  — PASS (45/45).
- The repository doc-drift/line-ending guard passed after normalizing the
  edited test file to CRLF.
