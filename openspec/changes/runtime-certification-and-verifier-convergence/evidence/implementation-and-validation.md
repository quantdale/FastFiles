# Implementation and validation handoff

The implementation is recorded in the change artifacts and committed on
`main`. It adds identity-bound run and invocation envelopes, schema validation,
central result normalization and exit projection, killable capability workers,
exact owned-process cleanup, stale-artifact rejection, UIA scenario selection
and ordering metadata, provider/report fidelity handling, status projection
guards, and deterministic verifier-core tests.

Current local evidence:

- Debug CTest: 83/83 PASS.
- Post-diagnosis release-test CTest: 83/83 PASS. An earlier handshake timeout
  is retained as host resource-pressure evidence, not silently discarded.
- Analyze build: warning-clean; ASAN protocol/IPC: 3/3 PASS.
- Focused verifier/status/provider/UIA-selection tests: 4/4 PASS.
- Strict OpenSpec: 69/69; doc-drift and `git diff --check`: PASS.
- `verify.ps1 doctor` and `verify.ps1 build`: PASS.
- Unsigned x64 release-candidate qualification: PASS with
  `productionReady=false` and build identity bound to the committed source.

The complete live UIA wrapper/repeatability matrix remains host-bound: the
current desktop's PowerShell/commit pressure prevented a bounded terminal
matrix run, so no live UIA product pass is claimed. Foreground, Explorer,
elevation, signing, privileged-service, clean-host, guest, and physical-display
requirements remain explicit external boundaries.
