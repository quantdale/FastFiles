# Canonical scale follow-up: 200k and 1M

> Historical checkpoint: later actual default-budget 2M/5M mixed-route executions and local task 4.1 closure are recorded in `mixed-scale-closure-20261026.md`; selected current-source suites are in `final-selected-20261026.md`. Earlier pending statements below describe this checkpoint, not the later disposition.

Both extended matrices pass **23/23** with all route premises valid. This closes the missing final-search-source 1M cold canonical cell and supersedes the earlier-source 200k cell. It does **not** close task4.1 or qualify hot/mixed routes by implication.

## Source and execution identity

- Executable source: clean disposable integration `6e060fbb34569ac9d21b964cff84d36743f350f3`, Debug diagnostic configuration.
- `git diff db7887c1..6e060fbb -- src/engine src/indexstore src/search src/protocol tests/engine/bench_identity_ordering_oracle.cpp tests/indexstore/bench_store_open.cpp` is empty. These product and oracle input subtrees exactly match the retained PR15 implementation. Unrelated native UI integration is present and is not claimed equivalent to PR15.
- Oracle SHA256: `9c128a98527e090f163e169e83353e977e14f14045bf239d241bda15793d5bcb`.
- No privileged service, raw volume, user database, signer, trust store or live termination experiment is involved. The input databases are newly generated, marked disposable fixtures beneath ignored build directories. The oracle uses its owned SQLite backup copy.

Commands used for each scale:

```powershell
ffindexstore_bench_store_open.exe --build-fixture <200000|1000000> --db <fresh-owned-fixture> --seed 0 --volumes 1 --maintenance-off
ffengine_bench_identity_ordering_oracle.exe --db <fixture> --output <report> --extended
```

The report records the actual row count. Every completed canonical arm examines all200,000 or1,000,000 rows with no hot rows and bypasses candidate filtering. Selective/exact/no-match cells prove actual indexed admission; matching classes are nonempty. Ordered identities, parents, metadata, status, truncation and caps compare exactly. Immediate cancellation is Cancelled with empty results. The fixture hashes before/after are equal.

| Scale | Exact cells / premises | Fixture SHA256 |
|---|---|---|
|200k|23/23;23/23|`6ca44e5d49ffb49e3e195e1a46ffec2f64ea7ec15a5cf7d1e733bf84fe64328a`|
|1M|23/23;23/23|`c96f5fe82f767d12215875e048648f798331abc78011c1fe03807c30c7c18724`|

Raw unique JSON and provenance receipts are in `artifacts/canonical-20261025/`. Timings are retained as raw diagnostics only: concurrent ordinary certification and a Debug build make these correctness runs unsuitable for performance claims.

## Retained frontier

No completed expensive2M/5M experiment was repeated. Existing final-source5M23/23 and2M Store18/18 remain retained at their original scopes. The earlier2M canonical report remains attributed to269d1c3. Its only subsequent production-source delta is `Projection::EvictSubtree`, while Store, engine query and oracle source are unchanged; that is a narrow query-source observation, not blanket setup/runtime equivalence.

Combined current-source engine/projection/residency and oracle self-tests pass4/4 at6e060fbb. They exercise their named hot/cold/eviction seams, but task4.1 still requires explicit independent-canonical resident/mixed/transition qualification and a final2M scope determination. Broader independent review remains incomplete; new delegation returned workspace-credit exhaustion and was not retried. Restricted reaper and external hosted/release boundaries remain unchanged.
