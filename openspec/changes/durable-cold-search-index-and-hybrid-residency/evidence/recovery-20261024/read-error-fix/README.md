# Final ordinary read-error correction

Implementation: `4c1c4501e5b1f0579a2d34f4f7afea5f83708979`.
These are small ordinary SQLite error-return fixtures, not storage exhaustion
or process-termination experiments.

## Red-first findings

1. A failed prepare/step of `PRAGMA user_version` defaulted to version0.
   A one-shot SQLite authorizer denial proves Open could accept a simulated
   newer version4 database and rewrite its marker to3. Open now rejects an
   unknown version instead of guessing fresh state.
2. Reconciliation `ListEntryIds` ignored terminal read errors. A one-shot
   SQLite VM interruption proves the pass could clear its obligation while
   stale rows remained. Complete enumeration is now explicitly reported;
   Finish retains the obligation on error, and the existing manager retry
   path does not publish successful completion.

`read-error-red2.log`: six expected failing assertions, including actual
error injection and marker/state checks. The earlier progress-only PRAGMA
probe did not fire (very short VM); it is not evidence for that finding.
`read-error-green.log`: eight focused Debug suites PASS after both fixes.
The preceding full ladder at3a41481 is historical; current-source revalidation
must be recorded separately, not silently carried across these production edits.

## Read-only benchmark access

To reuse synthetic fixtures without allocating another large database, the
oracle now offers an explicit `--readonly-fixture` mode. It enables URI parsing
only inside this benchmark process, constructs a local Windows `mode=ro` URI,
and installs an extension that rejects any non-read-only connection. SQLite
locking/change detection remain enabled: no `immutable` or `nolock` option.
Default disposable-backup mode is unchanged.

Official API basis:
- https://www.sqlite.org/uri.html
- https://www.sqlite.org/c3ref/open.html

Positive20k smoke:13/13 capped cold/canonical cells; fixture SHA256 unchanged.
Negative20k version2 smoke: exit3 when migration would require writing; fixture
SHA256 unchanged. These are preliminary harness checks, not final-source
performance claims. This read-only mode does not enable ingestion, mutations,
crash experiments, or writing any source/user database.
