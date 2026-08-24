## MODIFIED Requirements

### Requirement: Durable Read Snapshots Are Coherent and Refreshable

A read session used by one query SHALL pin one SQLite WAL snapshot for that
query. All rows, point lookups, path reconstruction, matching inputs,
top-N/sorting inputs, and aggregates used by that query SHALL come from that
snapshot. A later query SHALL be able to open a fresh snapshot and observe a
committed writer change without recreating the engine database. When a hybrid
query falls back, the fallback SHALL use one such session as its complete
durable source rather than combining rows from multiple snapshots.

#### Scenario: Held snapshot sees pre-commit state

- **WHEN** a durable reader begins before a writer commits an insert/update/
  remove batch and remains active while that batch commits
- **THEN** the held reader SHALL see one coherent pre-commit state, with no torn
  mixture of old and new rows

#### Scenario: Durable fallback holds one snapshot while writing continues

- **WHEN** a query switches to durable fallback while the writer continues
  committing relevant and unrelated batches
- **THEN** every candidate and parent path used for the response SHALL come
  from the fallback's one snapshot, and writer commits SHALL continue

#### Scenario: Fresh reader sees post-commit state

- **WHEN** the writer commit completes and the prior reader is released
- **THEN** a fresh read statement/session SHALL observe the committed rows,
  updates, and removals

#### Scenario: Passive maintenance overlaps a reader

- **WHEN** passive WAL maintenance runs while a read snapshot is active
- **THEN** maintenance SHALL return in bounded time and SHALL not invalidate or
  corrupt the reader, even if SQLite leaves frames in the WAL

