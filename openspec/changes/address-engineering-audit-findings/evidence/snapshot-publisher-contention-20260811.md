# SnapshotPublisher contention evidence — 2026-08-11

## Scope

Completed OpenSpec task 8.4. The CTest fixture starts one publisher and
releases eight worker threads at the same time. Each worker publishes 64
single-directory snapshots, for 512 bounded concurrent calls through the
publisher's publication mutex.

## Validation

- `ffengine_snapshot_publisher_tests` verifies every returned generation is
  non-zero, unique, and contiguous from 1 through 512.
- The test verifies `CurrentGeneration()` reaches 512 after all workers join.
- The test opens the shared section read-only and parses the final active slot,
  checking the format version, active-slot bounds, and one-directory payload.
- `cmake --build --preset debug --target ffengine_snapshot_publisher_tests` —
  PASS.
- `ctest --test-dir build/debug -R ffengine_snapshot_publisher_tests
  --output-on-failure` — PASS (1/1).
- The same focused test repeated five times with
  `ctest --repeat until-fail:5` — PASS (5/5).

The test covers bounded writer contention and final publication integrity; it
does not change the shared-memory contract or security policy.

## Full validation

- x64 Debug/Analyze/Release builds — PASS.
- x64 Debug CTest — PASS (45/45).
- x64 Release CTest — PASS (45/45).
- `pwsh ./verify/verify.ps1 build` — PASS (windows-build-validation).
- `npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all
  --strict` — PASS (45/45).
- The repository doc-drift/line-ending guard passed after normalizing the
  edited test, task, and evidence files to CRLF.
