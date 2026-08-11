# Index-store ANALYZE validation — 2026-08-11

## Scope

This slice closes task 5.1 of `address-engineering-audit-findings`.
It refreshes SQLite planner statistics after 10,000 committed MFT/USN upsert
records without changing authentication, signing, or privileged service
behavior.

## Implementation

- `Store::Analyze()` executes SQLite `ANALYZE` while the engine's existing
  pipeline mutex serializes ingestion and maintenance.
- `IndexPipeline` counts committed MFT records and USN upserts across batches,
  triggers at 10,000 records, and retains the threshold for a later retry if
  ANALYZE fails.
- The counter resets when the pipeline opens or closes a store, so records
  from separate engine sessions are not conflated.

## Evidence

- `ffengine_index_pipeline_tests`: PASS. A 9,999-record ingestion leaves
  `sqlite_stat1` absent; the next 10,000-record ingestion creates planner
  statistics, and the existing pipeline ingestion/rebuild/search cases pass.
- Focused CTest: PASS, 1/1.
- Full Debug build: PASS.
- Full Analyze build (`/analyze`): PASS.
- Full Release build: PASS.
- Serial Debug CTest: PASS, 45/45, including `ffdoc_drift_tests`.
- Serial Release CTest: PASS, 45/45.
- `openspec validate --all --strict`: PASS, 45/45.
- `pwsh ./verify/verify.ps1 build`: PASS (`windows-build-validation`).
