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
| `artifacts/baseline-200k-1m-2m.json` | `--scales 200000,1000000,2000000 --diagnostics --output <path> --source-sha 2f365a5…` *(flag was stale — see discrepancy note)* | `d3884eecd03805a91fe5d12d7a90ceb1b5c082f2` (PR #14 head; run from the successor worktree at that base) | clean |
| `artifacts/candidaterung-200k-v2.json` | `--scales 200000 --source-sha <HEAD> --source-state dirty --preset release-test` | `7c8faea5b6085e31dafc0568725b6adc48a45ff9` | dirty (pre-3aa64bf) |
| `artifacts/candidaterung-200k-1m-2m.json` | `--scales 200000,1000000,2000000 --diagnostics --source-sha <HEAD> --source-state clean --preset release-test` | `3aa64bf14f962f523388d5b9d9623502018422a3` | clean |
| `artifacts/candidaterung-capped-200k.json` | `--scales 200000 --source-sha <HEAD> --source-state dirty --preset release-test` (post-cap `2968077`-era tree) | `2968077`-era | dirty |
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

## Raw-artifact recovery (PR #15 finalization campaign, section 13) — 2026-10-22

`artifacts/candidaterung-200k-1m-2m.json` was referenced above but was **not
present on the pushed GitHub branch** (external reviewers could not retrieve
the raw observations behind the reported candidate measurements). Recovery
procedure per campaign section 13: searched the local worktree and benchmark
output directories; the exact original run output was found at
`<main-repo>/build/worktrees/candidaterung-200k-1m-2m.json` (with its
companion `.log`). No re-derivation or fabrication: the preserved artifact is
a byte copy of that run output.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `artifacts/candidaterung-200k-1m-2m.json` | 654,258 | `9c33af1c6922aa42abf8f34477f6968bc7fcd60cdc3802b9e3e8aa491478feeb` |
| `artifacts/candidaterung-capped-200k.json` | 313,077 | `06a64f3ebd94d55967aa983b90369c13db6d0415d216292f290d5b4885076329` |

Verification: the recorded SHA-256 equals the git blob content hash
(`git show <rev>:<path> | sha256sum`), so the durable bytes are retrievable
and checkable even if a future checkout's line-ending materialization
(`core.autocrlf`) differs from the run output on disk.

Companion raw log preserved alongside the recovered run artifact:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `artifacts/candidaterung-200k-1m-2m.log` | 11,522 | `9438001e196ebafe3b660bed57c8a94c1f28136676b99a53f5e8879766fa8096` |

## Review-wave corrections (lane D/F, PR #15 finalization review wave) — 2026-10-23

1. **Baseline flag/SHA discrepancy (lane D P2).** The baseline run's
   `--source-sha` flag value (`2f365a5…`) disagrees with the tree the run
   actually executed on (`d3884eec`, PR #14 head). The FLAG was stale; the
   executed tree is established by the run's position in the campaign
   (measurement phase run on the landed PR #14 tree before any successor
   product change). Both values are recorded rather than silently
   harmonized; the flag-supplied identity was never machine-verified in the
   pre-repair harness.
2. **Caller-asserted identity is not verified identity (lane D P2).** All
   pre-repair artifacts and the post-repair `bench_large_index_scalability`
   identity block carry `source_state_asserted_by: caller`. Post-repair runs
   additionally embed `build_identity` (ffsetup, refreshed at build time),
   which binds the BINARY to the tree it was built from. The
   `ffindexstore_bench_store_open` harness is the only one that also hashes
   its own executable at runtime. This manifest never upgrades asserted to
   verified.
3. **Run-series identity corrections (lane D P2).** `build/campaign/
   bench-run1.json` embeds `source_sha ed7190e…, source_state clean` —
   correct as stated, but its product tree contains the PRE-planner-fix
   candidate admission (the CROSS JOIN plan defect); its selective-query
   numbers (~122 ms at 2M) are CONTAMINATED for comparison purposes and must
   never be pooled with post-fix runs. `bench-run2-2m.json` /
   `bench-run3-2m.json` were invoked with `--source-state dirty` although the
   tree was clean at `97b9ceb` — the flag was wrongly conservative; the
   embedded `build_identity` (`97b9ceb…, tree=clean` at build time) is the
   authoritative binding. No historical artifact is rewritten; corrections
   live here.
4. **Temperature labels are harness labels (lane D P2).** `cold`/`warm` in
   `bench_large_index_scalability` denote the harness's per-class sequencing
   (first run after fixture/rebuild vs repeated runs), NOT a controlled OS
   page-cache state; no cache flush is performed. At >=2M the `warm` runs are
   cold-stream under the default 500 MiB budget (lane-F caveat, unchanged).
   Quoted "cold" numbers therefore mean "first iteration of a fresh
   measurement block".
5. **Aggregate p50 mixing (lane A-3).** `ffindexstore_bench_store_open`
   artifacts' `aggregate_ms` fold-signature p50 mixes cold-process (paid)
   and warm-same-process (0 ms) samples; the per-process cost is the
   cold-sample distribution (`repetition == 0` rows), quoted separately in
   the startup evidence.

Both artifacts were produced by the **pre-repair** harness and therefore carry
NO embedded identity (the manifest row above is their only identity record;
the JSON itself contains no `source_sha`). The campaign's section-14 harness
repair (`bench_large_index_scalability` v2 identity block; `ffindexstore_bench_store_open`
and `bench_candidate_index_resources` v2 identity blocks) applies to
**subsequent** artifacts only; the historical files are preserved untouched
and never silently overwritten. The final certification reruns at the clean
implementation SHA produce new, identity-embedded artifacts kept separately.
