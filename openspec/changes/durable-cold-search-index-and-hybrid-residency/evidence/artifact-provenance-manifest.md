# Measurement artifact provenance manifest — durable-cold-search campaign

Reviewer-F P1 remediation: the bench harness's v2 JSON does **not** embed
source identity (lane-F gap G1: `command_line`/`source_sha`/`source_state`/
`preset` are accepted as flags but not written into the artifact). The
previous "identity embedded" wording in this change's records was therefore
**false** and is corrected here and in the affected records. Identity is
instead recorded in THIS run manifest, next to the copied artifacts, and the
artifacts themselves are byte-identical to the run outputs.

Harness invocations (all from the successor worktree, release-test preset,
`ffengine_bench_large_index_scalability` unless noted):

| Artifact | Command flags | Source SHA | Tree state |
|---|---|---|---|
| `artifacts/baseline-200k-1m-2m.json` | `--scales 200000,1000000,2000000 --diagnostics --output <path> --source-sha 2f365a5…` *(see note)* | `d3884eecd03805a91fe5d12d7a90ceb1b5c082f2` (PR #14 head; run from the successor worktree at that base) | clean |
| `artifacts/candidaterung-200k-v2.json` | `--scales 200000 --source-sha <HEAD> --source-state dirty --preset release-test` | `7c8faea5b6085e31dafc0568725b6adc48a45ff9` | dirty (pre-3aa64bf) |
| `artifacts/candidaterung-200k-1m-2m.json` | `--scales 200000,1000000,2000000 --diagnostics --source-sha <HEAD> --source-state clean --preset release-test` | `3aa64bf14f962f523388d5b9d9623502018422a3` | clean |
| `artifacts/fts5-probe-{200k,1m,2m}.json` | `ffengine_bench_fts5_candidate_probe --scale <n> --source-sha d3884eec… --source-state prototype-worktree` | `d3884eec`-era prototype worktree | prototype |
| `artifacts/secondary-index-200k-1m-2m.json` | `ffengine_bench_secondary_filename_index --scales 200000,1000000,2000000` (no identity flags — harness predates them) | `d3884eec`-era | clean |

Note on the baseline: it reproduces the PREDECESSOR regime and was run before
the successor's product changes; the successor worktree at that moment
contained the measurement/decision commits only (no product diff vs
`d3884eec`). The comparison is same-harness/same-fixture-generator.

Known methodology limits (lane-F P1, preserved honestly): at >=2M the
harness's "warm" temperature labels are cold-stream under the default 500 MiB
budget; cold cells are n=1 (exploratory); the corrected repetition protocol
(lane-F) is not yet implemented. Wall-clock claims from these artifacts are
therefore EXPLORATORY except where the mechanism (rows examined 19,688 -> 1)
is the evidence.
