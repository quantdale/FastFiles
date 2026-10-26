# Independent canonical residency routes — 2026-10-26

## Scope and implementation

Task4.1 remains OPEN. No production engine/store/search/protocol source changes. The existing fixture producer now has an opt-in --engine-roots mode; default Store fixture generation stays unchanged. This optional benchmark mode compares a tested pipeline with a separate all-cold full-canonical pipeline on the same freshly owned SQLite backup; the source input opens read-only. Every reference bypasses all candidate filtering and must enumerate the full corpus. Both projections retain the500MiB default; no budget override is called.

`--residency-routes --extended` runs the unchanged23-cell matrix in resident, one-volume-cold mixed, cold and reloaded-resident states. It verifies actual residency, budget/estimate, hot/cold visits, request echo, ordered all-field identities/metadata, status, caps/truncation and immediate cancellation. A captured WorkStarted eviction must cause a generation restart and match the independent canonical result. Active durable readers must return to zero. New mode rejects --readonly-fixture, so mutation/rebuild qualification never uses an externally supplied read-only URI. Cleanup requires the factory marker, generated filename prefix and canonical parent equal to the system temp root.

The existing v2 default matrix and query definitions remain unchanged. Existing deterministic fixture tool supplies the corpus; no new generator or physical NTFS volume is needed. Planned commands (fresh marker-owned paths only):

```powershell
ffindexstore_bench_store_open.exe --build-fixture 200000 --db <fresh-fixture> --seed 0 --volumes 2 --maintenance-off --engine-roots
ffengine_bench_identity_ordering_oracle.exe --db <fixture> --output <report> --extended --residency-routes
```

## Validation before corpus execution

Debug oracle/fixture targets build warning-clean; registered oracle self-test1/1 PASS. A controlled bench-only omission of the independent-reference guard makes the self-test exit1 without opening any database. The valid source is restored byte-for-byte; the first rebuild incorrectly reused the red object because Copy-Item preserved the older source timestamp. That attempt failed and is retained. Explicit source touch/fresh compilation repairs the proven stale-object cause, and final self-test1/1 passes (0.23s). Additional negatives reject incomplete/hot/filtered references, status/cap/allocation mismatches and truncated cancelled payloads. Read-only/new-mode rejection exits2 before creating input or output. Strict OpenSpec and diff-check pass.

Actual corpus/routes, input before/after hashes and committed-source executable identity are PENDING. No hot/cold/mixed/generation result or task closure is claimed. No2M/5M benchmark was repeated; earlier-source2M scope and broader independent review remain separate. No privileged/trust-store/reaper activity.

## First execution and fixture correction

The clean b424c2e two-volume run FAILED: resident stage2/23, only no-match/cancellation passed; later stages did not run. Input SHA256 remained02119aeb252154a9de1530f6b244775d40898659d8c4f5006626ef64defdcd46. Raw failed JSON/logs/provenance remain under ignored build/residency-canonical-20261026. Read-only SQL confirms all200000 rows parent5, but only volume1 contains identity5. Matching engine results require complete parent chains; no-match enumerates200000 canonical rows without materializing candidates. Production rejects incomplete candidates correctly.

The opt-in engine fixture replaces the first row in each volume with a self-parent directory root5 and shifts other IDs to avoid collisions, preserving exact total rows and the existing filename corpus. It requires at least one full5000-row batch per volume before touching the output. Status codes are now emitted in each oracle cell. Default Store-only fixture behavior is preserved. Both targets rebuild warning-clean and self-test1/1 passes. The initial comma-separated two-target dev.ps1 invocation was rejected by Ninja; separate target invocations succeeded. Corrected corpus execution is pending; task4.1 remains OPEN.

## Production root-child path defect

Clean5e91403 with complete roots still fails resident2/23: matching counts/status and full canonical scans now valid, but exact candidates differ. Input hashE75A903D284C5790C99087E1CA6B1BD6C8B490F7122033B8E5CC4E412822518A unchanged. Independent read-only root_folder_review confirms hot direct-root child folderC:\ versus durableC:, with actual Open/Reveal joining the drive-relative path. Nested folders already match; root candidates intentionally keep empty folder and drive-root display name.

Minimal fix appends the root separator only when the successfully traversed durable parent chain has no non-root segments. Regression includes root child file/directory, nested file and root candidate; nonvacuous route/count/status, exact ordered all-field signatures, cap and absolute joined-path assertions. Valid-request RED fails14 assertions, GREEN1/1 passes5.33s. Initial fixture attempts omitted maxResults and failed InvalidRequest premises; those logs remain and are not counted as product regression proof. A readiness assertion was also added. Prior5M/2M evidence remains bound to its original implementation; this fix requires its own bounded canonical qualification.

## Bounded preflight verified

Clean implementation324122ab587759c2e7412a56ab487db192f179eb:20k/two-volume extended matrix93/93 PASS, all four stages plus in-flight eviction. Actual default maximum524288000B; estimates resident/reloaded6527808B, mixed3200302B, cold16448B. Hot/cold visits and actual residency checked, canonical20000 rows per completed cell, one generation restart and all durable readers released. Input SHA256463a7e5623da11f36f3b776bc8af022ba5d7870ef7528b7cb6c2c040ec7fd650 unchanged; source clean before/after. Compact raw reports/provenance, including both failed200k runs, preserved under artifacts/residency-20261026. This is a20k preflight, not200k acceptance. Independent source review and requested regression strengthening recorded in reviews/root-child-folder-20261026.md. Current strengthened focused suite1/1 passes5.67s.
