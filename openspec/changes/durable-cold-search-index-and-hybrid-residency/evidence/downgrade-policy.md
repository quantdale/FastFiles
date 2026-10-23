# Downgrade policy — FAIL_CLOSED_DOWNGRADE (task 3.3c evidence)

Change: `durable-cold-search-index-and-hybrid-residency`. Decision record for
the schema-v3 downgrade policy, closing the spec/implementation mismatch found
by acceptance lane E ("the spec claims migration/rollback is *reversible*; the
implementation is fail-closed older-binary refusal").

## Decision: FAIL_CLOSED_DOWNGRADE (spec option C, with option B's operational content)

There is **no automatic in-binary downgrade** and none will be added: a binary
that does not understand the on-disk schema version **refuses to open the
database**, and the data is fully preserved. Reversal is an **operator-invoked
procedure** that drops only derived candidate structures. The spec
(`specs/index-engine/spec.md`, "Schema migration shall be measured, with a
fail-closed, data-preserving downgrade policy") now states exactly this.

Rationale: the v3 additions are **pure derived state** — the entries table is
never rewritten by migration (`src/indexstore/src/Store.cpp`, v2->v3 comment:
"pure additive derived state"), so refusing a newer schema is the
data-protection posture (never guess at unknown schema), while an operator can
always step back because only derived structures are dropped.

## Behavior 1 — downgrade refusal (the guard)

`src/indexstore/src/Store.cpp` (version dispatch in `Store::Open`): an
`existingVersion` that is neither 1, 2, nor the current version hits
`Close(); return false;` — no integrity failure recorded, no deletion. This is
the exact branch a v2-era application reaches when it meets a v3 database
(`existingVersion != kCurrentSchemaVersion`).

Evidence: `ffindexstore_store_tests` ->
`TestUnknownSchemaVersionFailsClosedWithoutDataLoss` (added this campaign):
25 seeded rows, `user_version` advanced past the understood version -> `Open`
fails closed with `integrityFailed == false`, raw sqlite verifies all 25 rows
still present, and restoring the understood version reopens with all rows.

## Behavior 2 — operator rollback to the previous schema version (documented procedure)

On a **disposable/closed** database (the application must not be running;
prefer a backup copy), execute exactly:

```sql
DROP TABLE IF EXISTS entry_name_exceptions;
DROP TABLE IF EXISTS entry_name_index_meta;
DROP TABLE IF EXISTS entry_name_index_map;
DROP TABLE IF EXISTS entry_name_terms;
PRAGMA user_version=2;
```

This drops only the v3 derived candidate structures and resets the version
marker. It never rewrites `entries`/`volumes`. The previous release then opens
the database normally; when the newer release returns, its v2->v3 migration
rebuilds the candidate index (measured cost: `5m-migration-v2` cells).

Evidence: `ffindexstore_store_tests` ->
`TestOperatorRollbackToV2IsLosslessAndRebuilds` — rollback SQL applies, all
12 entries survive byte-for-byte at the row level, and re-opening with the
current binary re-migrates and the rebuilt candidate rung streams every
canonical match. The same SQL is exercised by the in-tree v2 simulations
(`tests/indexstore/bench_store_open.cpp`, `test_store.cpp`,
`bench_candidate_index_resources.cpp`).

Note the equivalence: the rollback procedure and the tests' "simulate a
genuine v2 database" step are the same statement sequence — the operator path
is what the tests run in reverse.

## 5M measurement cells (task 3.3c)

The v2->v3 migration at 5M rows was measured twice on the 5M entries-only
fixture (`build/campaign/open-5m.db`, 5,000,000 rows, 976,363,520 bytes):
`candidate_rebuild` 120,755.98 ms / 98,078.11 ms (sub-phases map/terms/
exception), totals 137,350.81 ms / 108,484.64 ms, peak working set ~13 MB.
Raw artifact: `evidence/artifacts/5m-migration-v2.json` (preserved this
campaign; originally `build/campaign/open-matrix/5m-migration-v2.json`).

**Identity qualification (honest):** the preserved run was produced by a
`ffindexstore_bench_store_open` binary whose source identity was
`628de6d+dirty-harness` (the phase-instrumentation harness edits were
uncommitted at run time and later landed at `80a6f11`). The production
migration code under test (`Store.cpp`) was the certified `628de6d` tree. A
clean-tip re-measurement at the final implementation SHA is part of the final
acceptance ladder; until it lands, the numbers above are mechanism evidence
with a disclosed harness-identity qualification, not final-sha evidence.

## Explicitly not claimed

- No claim that an older binary can read a v3 database directly (it cannot;
  it fails closed by design).
- No in-binary `v3 -> v2` downgrade tool. The operator procedure is SQL on a
  closed database.
- Downgrade with *preservation of candidate structures* is not offered — the
  derived index is rebuilt on return to v3 (cost measured per scale).
