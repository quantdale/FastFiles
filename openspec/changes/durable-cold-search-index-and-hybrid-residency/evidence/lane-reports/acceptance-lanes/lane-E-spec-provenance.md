## Review

**Scope/limitation note:** this lane has no shell tool, so `git diff --stat 628de6d..80a6f11` / `80a6f11..b68767a` could **not** be executed. Per policy I report that limitation rather than claiming the committed ranges were reviewed. Everything below is verified from worktree source, tests, and campaign artifacts; exact git commands the supervisor must run are listed under Lane E3.

### Lane E1 — Migration/downgrade spec mismatch

**(a) Where the spec claims reversibility**
- `openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/index-engine/spec.md:66` — "### Requirement: Schema migration and rollback shall be measured and **reversible**"; `spec.md:68-69` — "Any schema addition SHALL be versioned and additive, with a measured migration cost at 2M-5M rows, **a documented rollback that loses no indexed data**, and a rebuild-on-open fallback when migration is interrupted."
- `proposal.md:69-73` — "4. **Schema migration and rollback.** … **rollback to v2 with no data loss** …"; `proposal.md:108` — "rollback are mandatory acceptance items, not stretch goals."
- `design.md:52` — "3. **Migration:** v2 -> v3 cost at 2M-5M; **rollback path**; crash-consistency".

Note the spec's only Scenario (Interrupted migration, `spec.md:72-77`) covers crash-interruption — **no scenario covers downgrade**, so "reversible" is asserted in prose but untested/undefined.

**(b) What the code does on downgrade**
- `src/indexstore/src/Store.cpp:36` — `constexpr int kCurrentSchemaVersion = 3;`
- `Store.cpp:893-898`: `} else if (existingVersion != kCurrentSchemaVersion) { // A newer-than-understood schema is refused rather than guessed at. // A v2-era application opening a v3 database lands here and fails // closed (no downgrade, no data loss). Close(); return false; }`
- Header comment `Store.cpp:33-35`: "A v2 application opening a v3 database is refused by the existing newer-version guard below (fail closed; **no downgrade is attempted**)."
- Data preservation is real: v2→v3 is "pure additive derived state. The entries table is never rewritten" (`Store.cpp:879-881`); refusal is `Close(); return false;` with no delete path.

**(c) Can a safe downgrade exist?** **Yes — as an offline operator rollback, and the recipe already exists in-tree (3 copies):** `tests/indexstore/bench_store_open.cpp:279-283`, `tests/indexstore/test_store.cpp:1111-1116`, `tests/indexstore/bench_candidate_index_resources.cpp:337-341` all execute `DROP TABLE IF EXISTS entry_name_exceptions/entry_name_index_meta/entry_name_index_map/entry_name_terms; PRAGMA user_version=2;` — lossless because the four v3 objects (`Store.cpp:102-114`) are derived-only. `test_store.cpp:1123` proves a v2-shaped DB opens and migrates. What does **not** exist: any automatic in-binary downgrade, any documented operator procedure, or any test of the refusal guard (see E2).

**Recommendation: C** (amend spec to accept `FAIL_CLOSED_DOWNGRADE` + operator recovery instructions), with B's operational content folded into the amendment. A is false as shipped; B alone understates that the refusal is a deliberate security posture (`lane-E.md:4168`: "Do not weaken the newer-version refusal"). Proposed delta — replace `spec.md:66-77` requirement block with:

```diff
-### Requirement: Schema migration and rollback shall be measured and reversible
-
-Any schema addition SHALL be versioned and additive, with a measured migration
-cost at 2M-5M rows, a documented rollback that loses no indexed data, and a
-rebuild-on-open fallback when migration is interrupted.
+### Requirement: Schema migration shall be measured, with a fail-closed,
+data-preserving downgrade policy
+
+Any schema addition SHALL be versioned and additive, with a measured migration
+cost at 2M-5M rows, a rebuild-on-open fallback when migration is interrupted,
+and a documented downgrade policy: FAIL_CLOSED_DOWNGRADE. A binary that does
+not understand the on-disk schema version SHALL refuse to open the database
+(no integrity failure recorded, no deletion performed), and all indexed
+entries SHALL remain intact. Reversal to the previous schema version SHALL be
+an operator-invoked, documented procedure that drops only derived candidate
+structures and resets `user_version`, never rewriting the entries table.
+
+#### Scenario: Downgrade refusal (FAIL_CLOSED_DOWNGRADE)
+
+- **WHEN** a build whose understood schema version is older opens a database
+  written by a newer schema version
+- **THEN** the open fails closed with no integrity failure and zero deletion
+- **AND** all entries are readable once the newer binary is restored
+
+#### Scenario: Operator rollback to the previous schema version
+
+- **WHEN** an operator runs the documented rollback procedure (drop
+  `entry_name_terms`, `entry_name_index_map`, `entry_name_exceptions`,
+  `entry_name_index_meta`; set `user_version` to the previous version) before
+  reinstalling the previous release
+- **THEN** the previous release opens the database normally with every entry
+  intact, and the candidate index is rebuilt when the newer release returns
```

Also amend `proposal.md:72-73` "rollback to v2 with no data loss" → "a documented, data-preserving downgrade policy (fail-closed refusal + operator rollback)". 

### Lane E2 — Task 3.3c audit

**Already satisfied:** the measurement half. `build/campaign/open-matrix/5m-migration-v2.json` records 2 fresh-process v2→v3 runs on the 5M entries-only fixture (`open-5m.db`, 976,363,520 bytes): `candidate_rebuild` 120,755.98 ms / 98,078.11 ms, `total` 137,350.81 ms / 108,484.64 ms, fine-grained map/terms/exception decomposition — matching the reported figures.

**Still missing to legitimately check 3.3c ("…+ downgrade-policy evidence recorded"):**
1. **There is no downgrade evidence at all.** `grep downgrade` across `tests/` finds only unrelated USN-wire downgrade tests (`tests/protocol/test_protocol.cpp`). No test exercises `Store.cpp:893-898` (the C2e case requested at `lane-E.md:4183` is still absent — flagged untested at `evidence/lane-reports/review-B-tail.md:8`). The certification claim "**Fail-closed downgrade (v2 app refuses v3; entries preserved): PASS**" (`evidence/certification-record.md:51`) has **no backing test or artifact** — the only bases are the code guard and prose (`5m-staged-acceptance.md:61`, `architecture-decision-record.md:99`).
2. **The 5M artifact is untracked and unreferenced.** `build/` is gitignored (`.gitignore:1`); no tracked file anywhere cites `5m-migration-v2.json` (repo-wide grep: zero matches). Tracked evidence actively contradicts it: `5m-staged-acceptance.md:62-64` and its NOT_EXECUTED list say "5M open-matrix cells … NOT_EXECUTED"; `resource-measurements.md:61` says "2M/5M migration timing: **DISCLOSED GAP**".
3. Its identity is `source_sha "628de6d+dirty-harness", source_state "dirty"` — a binary matching no commit (see E3).

**Exact close-out requirements:** (1) add + run a downgrade-refusal test (e.g. `TestNewerSchemaVersionFailsClosedWithoutDataLoss` in `tests/indexstore/test_store.cpp`: completed v3 DB → bump `user_version` above 3 (same guard a v2-era binary hits) → `Open()==false`, `integrityFailed==false`, `CountEntries` unchanged; ideally additionally run a `d3884eec`-era (v2) build against a completed v3 DB per `lane-E.md:4183` C2e); (2) record the policy as tracked evidence (e.g. `evidence/downgrade-policy.md`: FAIL_CLOSED_DOWNGRADE decision, cite `Store.cpp:893-898`, the operator rollback SQL from `bench_store_open.cpp:279-283`, test output); (3) preserve `5m-migration-v2.json` per the manifest's raw-artifact recovery procedure (byte copy + SHA-256 into `evidence/artifacts/`) and update `5m-staged-acceptance.md` Stage E + NOT_EXECUTED list and `resource-measurements.md:61`; (4) land the E1 spec amendment; (5) re-run the 5M migration cell from a clean-tip-built harness (or explicitly label the current numbers dirty-harness/mechanism-only).

### Lane E3 — Benchmark executable identity

**Git limitation:** `git diff --stat 628de6d..80a6f11` and `80a6f11..b68767a` were not run (no shell here). Supervisor must run exactly those two commands plus `git diff 628de6d..80a6f11 -- src/ tests/` to settle the questions below.

**Established without git:**
- `evidence/certification-record.md:4-8` claims candidate = `628de6d` (product tree) / tip `80a6f11` ("evidence + bench-harness commits on top; `git diff 628de6d..80a6f11 -- src/` is empty") — i.e. `src/` identical is a **documented claim, unverified here**; `docs/agent/WORKSTATE.md:5` repeats it. The "bench-harness commits" admission plus the 5M artifact's `"628de6d+dirty-harness"` label and the task statement together establish that **`tests/indexstore/bench_store_open.cpp` (target `ffindexstore_bench_store_open`, `tests/indexstore/CMakeLists.txt:60`) differs between 628de6d and 80a6f11** — the 5M run binary was built from uncommitted harness edits later committed at 80a6f11. Whether any other test/bench input differs requires the git diff.
- `b68767a` (final tip) postdates every documented chain (`WORKSTATE.md:5` ends at `80a6f11`); its delta is unknown — the "executable-tree equivalence gate from 628de6d to the final tip" (`certification-record.md:6-8`) must be confirmed to cover `b68767a`, not just `80a6f11`.

**Claims-vs-truth table**

| Artifact (claimed in) | Claimed identity | Truth | Minimal re-run |
|---|---|---|---|
| `build/campaign/open-matrix/5m-migration-v2.json` (cert-record:49, WORKSTATE) | `628de6d+dirty-harness`, dirty, build_identity 628de6d | Executable matches **no committed tree** (uncommitted `bench_store_open.cpp` delta, later `80a6f11`) | Rebuild `ffindexstore_bench_store_open` at clean tip; `--build-fixture 5000000` + `--measure --case migration-v2 --processes 2` (~1 h incl. fixture) |
| `build/campaign/open-matrix/{200k,1m,2m}-*.json` (20 files; cert-record:49) | `1c427d7`, dirty, build_identity 1c427d7 | Built from uncommitted tree (OpenPhaseTimings slice later landed `ed7190e`+; lane A-2 PRAGMA-timing fix postdates it) | Re-run `migration-v2` + `migration-v2-interrupted` cells at 2M from clean tip (~15 min); other cells only if startup claims are re-issued |
| `bench-run4/5-2m.json`, `bench-5m.json`, `lifecycle-*.json` (perf-repeatability:11, 5m-staged:3-4, lifecycle-matrix:10) | `628de6d`, clean (embedded identity agrees) | Matches **iff** engine-bench sources unchanged 628de6d→tip — unverifiable without git | Only if git diff shows tests/engine bench changes: re-run affected harness at tip |
| Whole-candidate binding (cert-record:4-8) | "src/ identical 628de6d..80a6f11"; equivalence gate "to the final tip" | Unverified; tip-of-record ambiguity (`80a6f11` vs `b68767a`) | Run the two diffs; re-evaluate gate at `b68767a` |
| `artifacts/baseline-200k-1m-2m.json` (manifest) | flag `2f365a5` vs executed `d3884eec` | Discrepancy already disclosed (manifest review-corrections #1) | None (keep disclosure) |
| `candidaterung-200k-v2.json`, `candidaterung-capped-200k.json`, `bench-sanity-200k.json` | dirty-tree, asserted-only | No committed identity; already labeled exploratory | None unless promoted to final claims |

**Findings:** P1 — spec says "meversible rollback"; implementation is deliberate fail-closed refusal with no documented recovery (E1). P1 — `certification-record.md:51` PASS claim for fail-closed downgrade is unbacked by any test/artifact, and 3.3c's "downgrade-policy evidence" does not exist (E2). P1 — 5M migration evidence is untracked, contradicted by tracked docs, and produced by a no-commit harness binary (E2/E3). P2 — open-matrix 200k–2M cells cite a dirty pre-remediation tree; `b68767a` delta/equivalence unconfirmed.

**Merge verdict: BLOCK** — until the spec is amended (E1 delta), the downgrade claim is backed by a test or reclassified, and the 5M artifact is tracked/contradiction-resolved (identity re-run or honestly labeled).