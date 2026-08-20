# Proposal — harden-index-store-concurrent-access

## Why

The canonical `filesystem-index-store` specification already promises that WAL-backed ingestion permits concurrent readers to proceed without being blocked by an in-progress write transaction. `Store::Open` enables WAL, but the current `tests/indexstore/test_store.cpp` coverage uses one `Store` handle at a time and does not execute the promised same-database reader/writer overlap.

This is a production-readiness gap in evidence and potentially in behavior: a future SQLite/configuration/refactor regression could turn reconciliation/diagnostic reads into `SQLITE_BUSY`, stall ingestion, break snapshot consistency, or make maintenance interfere with readers without any focused test failing.

## What changes

- Add deterministic same-SQLite-file tests using two independent `Store` instances/connections.
- Hold a real read statement open while a second connection commits a real batched write.
- Prove the held reader sees a stable SQLite snapshot, the writer completes within a bounded interval, and a subsequent read observes the committed data.
- Prove passive WAL checkpoint/maintenance remains bounded while an independent reader is active and does not invalidate the reader.
- Exercise close/reopen and repeated overlap so connection teardown leaves no stale lock/busy state.
- If the executable tests expose a supported reader/writer contention defect, make the smallest store-layer fix necessary; do not add broad retries or hide persistent errors.
- Reconcile the stale deferred-note claim that same-SQLite-file concurrency coverage is still absent only after executable evidence exists.

## Non-goals

- No multi-writer product contract. FastFiles still has one authoritative engine/store writer for a profile.
- No cross-process engine ownership redesign, leader election, or database-sharing feature.
- No production signing, trust-store mutation, privileged service/MFT/USN work, clean-host work, or VM provisioning.
- No schema version change and no index data-model change.
- No weakening of integrity, WAL, transaction, checkpoint, or durability policy merely to make a stress test pass.

## Success criteria

The change is COMPLETE only when an executable two-connection fixture proves the canonical WAL reader/writer contract, snapshot visibility is correct before/after commit, passive checkpoint behavior is bounded, repeated overlap/teardown is clean, all existing index-store behavior remains green, repository validation is green, and the final state is committed/pushed cleanly with hosted CI queried for the final SHA.
