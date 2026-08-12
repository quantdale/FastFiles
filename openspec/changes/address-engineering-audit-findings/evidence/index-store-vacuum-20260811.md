# Index-store VACUUM validation — 2026-08-11

## Scope

This slice closes tasks 5.2–5.4 of `address-engineering-audit-findings`.
It adds idle-only, interval- and size-growth-gated SQLite VACUUM scheduling
with persisted settings defaults of 24 hours and 10 percent.

## Implementation

- `Store` records the database path, last VACUUM timestamp, and post-VACUUM
  database size; `VacuumIfDue` refuses non-idle calls, interval violations,
  and growth at or below the configured threshold.
- The existing engine idle callback is the only production caller that allows
  VACUUM. Regular reconciliation scheduler ticks continue to perform only
  cheap WAL maintenance.
- `Settings` serializes/deserializes `vacuumIntervalHours` and
  `vacuumSizeGrowthPercent`, defaulting to 24 and 10.

## Evidence

- `ffindexstore_store_tests`: PASS. Non-idle, no-growth, interval, successful
  idle/growth, and post-VACUUM baseline gates are covered.
- `ffprotocol_tests`: PASS. VACUUM settings JSON round-trip is covered.
- `ffengine_index_pipeline_tests`: PASS after the maintenance API/signature
  integration.
- Full Debug build: PASS.
- Full Analyze build (`/analyze`): PASS.
- Full Release build: PASS.
- Serial Debug CTest: PASS, 45/45, including `ffdoc_drift_tests`.
- Serial Release CTest: PASS, 45/45.
- `openspec validate --all --strict`: PASS, 45/45.
- `pwsh ./verify/verify.ps1 build`: PASS (`windows-build-validation`).
