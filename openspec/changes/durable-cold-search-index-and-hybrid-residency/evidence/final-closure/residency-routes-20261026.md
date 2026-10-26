# Independent canonical residency routes — 2026-10-26

## Scope and implementation

Task4.1 remains OPEN. No production engine/store/search/protocol or fixture-generator source changes. This optional benchmark mode compares a tested pipeline with a separate all-cold full-canonical pipeline on the same freshly owned SQLite backup; the source input opens read-only. Every reference bypasses all candidate filtering and must enumerate the full corpus. Both projections retain the500MiB default; no budget override is called.

`--residency-routes --extended` runs the unchanged23-cell matrix in resident, one-volume-cold mixed, cold and reloaded-resident states. It verifies actual residency, budget/estimate, hot/cold visits, request echo, ordered all-field identities/metadata, status, caps/truncation and immediate cancellation. A captured WorkStarted eviction must cause a generation restart and match the independent canonical result. Active durable readers must return to zero. New mode rejects --readonly-fixture, so mutation/rebuild qualification never uses an externally supplied read-only URI. Cleanup requires the factory marker, generated filename prefix and canonical parent equal to the system temp root.

The existing v2 default matrix and query definitions remain unchanged. Existing deterministic fixture tool supplies the corpus; no new generator or physical NTFS volume is needed. Planned commands (fresh marker-owned paths only):

```powershell
ffindexstore_bench_store_open.exe --build-fixture 200000 --db <fresh-fixture> --seed 0 --volumes 2 --maintenance-off
ffengine_bench_identity_ordering_oracle.exe --db <fixture> --output <report> --extended --residency-routes
```

## Validation before corpus execution

Debug oracle/fixture targets build warning-clean; registered oracle self-test1/1 PASS. A controlled bench-only omission of the independent-reference guard makes the self-test exit1 without opening any database. The valid source is restored byte-for-byte; the first rebuild incorrectly reused the red object because Copy-Item preserved the older source timestamp. That attempt failed and is retained. Explicit source touch/fresh compilation repairs the proven stale-object cause, and final self-test1/1 passes (0.23s). Additional negatives reject incomplete/hot/filtered references, status/cap/allocation mismatches and truncated cancelled payloads. Read-only/new-mode rejection exits2 before creating input or output. Strict OpenSpec and diff-check pass.

Actual corpus/routes, input before/after hashes and committed-source executable identity are PENDING. No hot/cold/mixed/generation result or task closure is claimed. No2M/5M benchmark was repeated; earlier-source2M scope and broader independent review remain separate. No privileged/trust-store/reaper activity.
