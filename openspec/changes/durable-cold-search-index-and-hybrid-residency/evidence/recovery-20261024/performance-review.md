# Lane C — performance/build review

## Review

- **Reviewed SHA:** `8c4c8481f312aa4651a5c2bab3cb61e0b0e50af0`.
- **Implementation baseline:** `5d7e98ae412cd893e47e7ebc9a9bdea4c609d0ac`.
- **Scope:** benchmark construction, allocation instrumentation, fixtures, repeat methodology, memory interpretation and disk requirements.
- **Fixed:** none; read-only review.
- **Merge verdict:** **OK with notes for the evidence-only baseline-to-HEAD delta. No unqualified PR15 performance acceptance.** Final-source 2M repeats and an allocator-probe-free ingestion measurement remain outstanding.

`watchdog_diff` confirmed a clean checkout against reviewer-launch HEAD. Committed-range inspection used the supplied `build/campaign/recovery-20261024/evidence-range.txt` and relevant portions of `pr15-executable.diff`, alongside actual source. The former contains evidence/document changes, not production changes. The benchmark defects below exist in the cumulative PR15 source diff; they are **not new regressions after `5d7e98a`**.

No builds, benchmarks, repository writes or prohibited evidence-directory reads were performed. Historical test results were not independently certified.

## Findings

### P2-C1 — WAL watcher has an unsynchronized stop flag

**Location:** `tests/indexstore/bench_candidate_index_resources.cpp:83–105`.

`WalWatcher::Start` launches a thread reading ordinary `bool stop` at line 91. `Stop` writes it at line 99 before joining. Those concurrent accesses have no synchronization and constitute a C++ data race. Sleeping in the sampling loop does not establish synchronization.

**Minimal reproduction:** any successful resource benchmark arm starts the reader thread and later calls `Stop`; the conflicting accesses are directly evident from the source. No particular hang or corrupted measurement was reproduced here.

**Smallest fix:** make the flag `std::atomic<bool>` and use atomic load/store. `peak` can remain ordinary storage because it is consumed after joining.

### P2-C2 — Resource benchmark reports success after failed measurement/output

**Locations:**
- `tests/indexstore/bench_candidate_index_resources.cpp:273–308`
- `tests/indexstore/bench_candidate_index_resources.cpp:383–430`

`RunArm` returns a default result when database opening fails. A failed `ApplyBatch` merely breaks the loop; throughput is subsequently calculated using the **requested** scale, rather than successfully inserted rows. `wmain` does not propagate these failures.

Output opening can also fail at line 384, yet the program still prints `wrote ...` and returns zero at lines 429–430.

**Minimal reproduction, not executed:**
```powershell
ffindexstore_bench_candidate_resources.exe --scale 1 --output "<nonexistent-parent>\result.json"
```

With a genuinely nonexistent parent, database/output opening fails, but the terminal return remains zero. During a partial-ingestion failure, a JSON artifact can additionally contain misleading throughput.

**Smallest fix:** propagate an explicit success/completed-row result from each arm; reject incomplete arms, failed migration preparation and failed output writes with nonzero exit status.

## Correct and independently verified

### The 132.37-second 5M measurement is allocator-instrumented

This is proven by source and raw tracked output:

- `tests/engine/bench_large_index_scalability.cpp:34–38` unconditionally defines `FFBENCH_ALLOCATOR_PROBE_DEFINE`.
- `tests/engine/bench_allocator_probe.h:191–253` supplies replacement global allocation/deallocation operators.
- Allocation/deallocation updates atomic counters and uses a per-allocation header (`bench_allocator_probe.h:116–175`).
- `tests/engine/bench_ingestion_profile.cpp:4–6` includes the same benchmark translation unit, inheriting those replacements.
- `.../evidence/artifacts/final-acceptance/ingestion-5m-final.json:13` explicitly says `"allocator_probe_enabled":true`.
- That artifact’s line 20 explicitly warns that **every mode** of the executable is instrumented.
- Line 21 records `generation_ms:132367.8954`; the companion log confirms completion at `ingestion-5m-final.log:53–56`.

**Therefore:** the 132.37-second result is a completed measurement, but **not an instrumentation-free ingestion result**. Calling it “uninstrumented” would be incorrect.

The current owner report calls it “ordinary,” rather than literally “uninstrumented” (`final-acceptance-owner-report.md:100–105`). “Ordinary” can accurately identify the non-stage-profile target, but should not imply absence of allocator instrumentation. Importantly, `tasks.md:52` still explicitly leaves clean uninstrumented remeasurement pending.

The ordinary target omits `FF_INGESTION_PROFILE`; that removes **stage timers/logging**, not the allocator probe. The distinction follows directly from `tests/engine/CMakeLists.txt:254–295`.

### Stage attribution has supporting raw measurements

I inspected the tracked profiles, not merely the owner narrative:

- `profile-5m-before-fallback.log:425–432`: approximately 148–197 ms store work versus 12,799–13,190 ms enforcement per batch, with increasing eviction counts and estimated memory.
- `profile-5m-after-fallback.log:655–666`: approximately 202–233 ms store work and 0.001 ms enforcement near completion; total 136,261.964 ms.
- `profile-5m-entries-only.log:655–666`: total 116,449.380 ms.

These substantiate the measured enforcement cliff and its removal in the diagnostic experiment. They do not establish a controlled shipping-binary speedup distribution or isolate internal FTS merge costs.

## Exact target/configuration answer

### Current HEAD has no supported allocator-probe-free exact-fixture latency/ingestion target

Neither the release-test preset nor a runtime option disables the unconditional replacement operators.

| Target | Allocator replacements | Stage profile | Relevant limitation |
|---|---|---|---|
| `ffengine_bench_large_index_scalability` | Yes | No | All modes allocator-instrumented |
| `ffengine_bench_ingestion_profile` | Yes, via included source | Yes | Diagnostic only |
| `ffengine_bench_identity_ordering_oracle` | No | No | Existing-fixture correctness comparison, not ingestion |
| `ffindexstore_bench_candidate_resources` | No | No | Store-only, different fixture; findings above |
| `ffindexstore_bench_store_open` | No | No | Store-open/migration workload, different fixture |

Thus **there is no honest current command producing the requested exact-fixture, instrumentation-free ingestion result**. Omitting `--diagnostics` does not solve this.

The minimal corrective implementation is a benchmark-only compile-time opt-out around the replacement-operator definition, preferably exposed through a sibling uninstrumented target. Keep the existing memory-report target instrumented and accurately label both outputs. No production algorithm changes are necessary.

For current-source **allocator/stage-probe-free correctness evidence**, the existing command is:

```powershell
cmake --build --preset release-test --target ffengine_bench_identity_ordering_oracle --parallel 1
.\build\release-test\tests\engine\ffengine_bench_identity_ordering_oracle.exe `
  --db "<existing-2M-fixture>" --output "<evidence-volume>\oracle-2m.json"
```

This creates a scratch database backup in the temporary directory. It is **not** a substitute for the missing same-harness latency repetitions.

`release-test` is the appropriate unsigned optimized configuration: `RelWithDebInfo` plus `FASTFILES_RELEASE_TEST_BUILD=ON` (`CMakePresets.json:39–47`). Production Release signing requirements need not—and must not—be weakened.

## Fixture and repeat consistency

### Same fixture within the ingestion experiment

`BuildSyntheticDatabase` uses the real pipeline and 8,192-record batches (`bench_large_index_scalability.cpp:557–628`).

At 5M it constructs:

- one root;
- 78,125 directories;
- 4,921,874 files;
- deterministic ten-pattern filenames, including Unicode;
- varying sizes/timestamps and a branching directory hierarchy.

The diagnostic target directly includes this builder. Its entries-only arm changes candidate maintenance, not fixture generation (`bench_ingestion_profile.cpp:20–25`).

### Migration/oracle fixture is different

`tests/indexstore/bench_store_open.cpp:201–255` builds Store records directly: every row is file-shaped, parent ID is 5, size is 4,096, and writes use 5,000-row batches. It does not construct the pipeline benchmark’s directory tree.

Consequently, migration/oracle results cannot be pooled with pipeline-ingestion timings merely because both fixtures contain 5M rows.

### Final-source 2M repetitions remain missing

The inspected final acceptance artifacts embed `5d7e98a` and request only `[200000,1000000]`:

- `repeat-200k-1m-1.json:3,10–12`
- `repeat-200k-1m-3.json:3,10–12`

Run 2’s retained log reaches 1M cold-query measurements but does not show normal benchmark completion (`repeat-200k-1m-2.log:30–55`). Excluding it from completed JSON distributions is appropriate.

`tasks.md:35` correctly leaves final-source 2M repetitions open.

The existing full harness can request `--scales 2000000`, but:

- it remains allocator-instrumented;
- it generates a new fixture;
- it runs all twelve classes, contention work and separate snapshot probes;
- JSON is written only after that work finishes (`bench_large_index_scalability.cpp:2360–2449`);
- it has no supported selective-only or ingestion-only option.

### Query-temperature labels require qualification

The twelve classes are defined at `bench_large_index_scalability.cpp:2140–2155`. Large scales use five measured “warm” iterations.

The explicit “cold” block unloads projection residency but then executes an unrecorded warmup immediately before its recorded query (`:2265–2280`). Therefore **cold means cold projection residency, not cold filesystem/SQLite cache**.

The 1M final artifacts have an empty projection after rebuild (`repeat-200k-1m-3.json:12`); their “warm” measurements are not fully hot-projection results.

The requested 10,000/50,000 result limits are clamped to the product cap (`IndexPipeline.cpp:1407–1408`), consistent with the artifacts’ 4,096-result boundary.

## Memory: modeled versus actual

These quantities must remain separate:

| Quantity | Interpretation |
|---|---|
| 500 MiB configured budget | Projection residency policy |
| 400 MiB watermark | Estimator enforcement target |
| 234,213,198 bytes | Diagnostic final-ingestion projection estimate |
| 1,368,276,992 bytes | Measured **instrumented-process** peak private usage |
| 1,262,170,112 bytes | Measured instrumented-process peak working set |

`Projection::EstimatedMemoryBytes` sums selected container capacities (`src/indexstore/src/Projection.cpp:174–188`). It is not process-private usage and does not include every allocator/SQLite/temporary allocation.

The process peak fields come from Windows process counters (`bench_large_index_scalability.cpp:1547–1556`). Their observed values are genuine process measurements, but include probe overhead. They cannot be relabeled as shipping-process peaks or corrected by subtracting cumulative allocation counts.

The post-generation/reopen projection being empty does not imply ingestion stayed near zero memory. The raw artifact retains the earlier process high-water marks.

## Disk budget and execution safety

**Do not run the large campaign with the supplied approximately 1.1 GiB free space.**

Measured anchors:

- Completed 5M pipeline database: **1,037,783,040 bytes**, approximately **0.967 GiB**.
- Sampled 5M migration/preparation WAL: **648,352,072 bytes**, approximately **0.604 GiB** (`migration-5m-wal-monitor.json:22–28`).
- Together, those example sizes already exceed **1.57 GiB**, before build outputs, additional copies, logs or reserve. These are different workload observations, not a guaranteed joint peak.
- The oracle copies the complete database into temporary storage (`bench_identity_ordering_oracle.cpp:45–77`).
- Pipeline scratch databases also use the temporary directory (`bench_large_index_scalability.cpp:269–278`).

For a rerun, use another sufficiently provisioned scratch volume for **both TEMP/TMP and outputs**, run one workload at a time, and build only the required target. A **4 GiB free-space preflight floor** is a reasonable operational precaution, not a proven bound. Build expansion and worst-case WAL usage were not measured in this review.

## Coverage gaps / scoped conclusion

No P0 or P1 product-performance defect was proven in this lane.

Still unverified:

- allocator-probe-free exact-fixture 5M ingestion timing and peak memory;
- final-source same-harness 2M fresh-process repetitions;
- controlled ingestion overhead distributions;
- large-reader, large-removal-crash and safe SQLITE_FULL operational cells.

The available evidence supports completed instrumented ingestion and a stage-isolated cliff correction. It does **not** close those remaining performance/resource gates.

### Principal inspected files and symbols

- Root `AGENTS.md`; applicable change `tasks.md`, `design.md`; workstate and selected performance/resource evidence documents.
- Root/test/component CMake files, `CMakePresets.json`, and SQLite build definitions.
- `bench_large_index_scalability.cpp`: allocation inclusion, fixture helpers, `BuildSyntheticDatabase`, `RunSearch`, memory-report path and `wmain`.
- `bench_allocator_probe.h`: allocation headers, counters and global operators.
- `bench_ingestion_profile.cpp`: fixture inclusion and entry point.
- `bench_candidate_index_resources.cpp`: `WalWatcher`, `RunArm`, `RunLifecycleArm`, `wmain`.
- `bench_store_open.cpp`: `FileName`, `BuildFixture`.
- `bench_identity_ordering_oracle.cpp`: scratch backup, search setup and argument handling.
- `IndexPipeline.cpp`: ingestion enforcement path, result-limit clamp and relevant supplied diff hunks.
- `Projection.cpp`: estimator and enforcement changes.
- Raw tracked final ingestion, before/after profiles, entries-only profile, repeat artifacts/log and migration WAL monitor listed above.