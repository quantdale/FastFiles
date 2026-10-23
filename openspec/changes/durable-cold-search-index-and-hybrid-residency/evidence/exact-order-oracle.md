# Exact identity and ordering acceptance — oracle v2

## V1 invalidated (not evidence of equivalence)

The oracle introduced in `0019b37` cleared the readiness marker before
reopening. `Store::Open` repairs that marker, so its arms could execute the
same candidate route. It also allowed two Unavailable/empty responses to
pass and did not force cold residency. The earlier 200k v1 PASS is withdrawn.
The purported earlier 5M background launch produced no log/result; it is not
an executed acceptance cell.

## V2 reference independence and safety

`ffengine_bench_identity_ordering_oracle` creates its own uniquely named
scratch directory and uses SQLite's read-only-source backup API to capture
committed rows (including WAL content). It never tampers the input database.
One production pipeline is opened on that scratch copy, volumes configured,
and all projection residency explicitly unloaded. Both arms share this
unchanged database. A test hook bypasses **all** name prefilters in the
canonical arm: `StoreReadSession::ForEachEntryUntil`, not FTS or LIKE/GLOB.
Normal application callers leave the hook disabled.

For every completed query, acceptance requires:

- Completed status on both arms (not merely equal statuses).
- Canonical cold rows examined == fixture entry count; no hot rows.
- No candidate-planner callbacks from the canonical arm.
- Actual candidate admission for selective/exact/no-match cells.
- Nonempty reference results for every matching class, empty for no-match.
- Exact element-by-element comparison of returned volume/id/parent,
  name/folder, sizes/knownness, timestamps and flags, not hashes or counts.
- Equal truncation flags, counts and request-cap compliance.
- Immediate cancellation produces Cancelled status with empty results.

Comparator negative controls reject equal-length reordered sequences,
changed volume/parent identities and dropped rows. These fast controls are
registered with CTest separately from the large-scale measurement.

## First checked runs (exploratory build, pending clean-tip rerun)

- 200k: 23/23 cells PASS (12 established classes, six sort-direction cells,
  cap 1/2/17/4096, immediate cancellation).
- 5M: 13/13 PASS (all 12 classes + immediate cancellation), canonical scans
  exactly **5,000,000 rows per completed query**. Selective/exact/no-match
  candidate admission is 1 each, examining 1/1/0 rows respectively.
- All returned identities and positions compare exactly. The high-limit
  request asks for 50,000 but returns the existing protocol-limited 4,096
  with truncation; neither arm claims 50,000 results were delivered.

Raw staging artifacts: `build/campaign/oracle/oracle-v2-{200k,5m}.{json,log}`.
The measured build embeds `0019b37` with uncommitted v2 source; it is NOT a
clean-SHA final certification. Preserve that provenance and supersede with
a clean-tip run rather than relabeling it.

## Coverage boundaries

This 5M input is the existing **open/migration fixture**, not the discarded
historical pipeline-ingest database. It contains 5,000,000 rows and the same
name-pattern family but a different parent/layout distribution; no direct
latency comparison to the historical 5M pipeline benchmark is claimed.
Memory residency is forced cold, not fictitious fully-hot 5M residency.

Volume-root/multi-volume, exceptional-fold/surrogate, repeated-trigram,
rename/delete/partial-upsert and cap-boundary mechanisms remain small-fixture
unit-test coverage. Their dedicated 5M-scale cells and uncapped full-result
sequences are NOT_EXECUTED. v2 proves capped search publication equality,
not exhaustive uncapped result sets. Sort sweeps execute at 200k here; the
base 5M run uses established query sort fields only.

Final independent adversarial review remains INCOMPLETE: the three-lane
review wave failed with provider 402 quota/balance errors before verdicts.
No review success or owner acceptance is inferred from that failure.
