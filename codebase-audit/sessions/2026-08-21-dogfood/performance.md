# Dogfood measurement evidence

Measurement date: 2026-08-21. The host was Windows 11 build 26200, x64,
12 logical processors, 25,028,239,360 bytes of RAM, MSVC 19.44, and
PowerShell 7.6.5. The committed measurement/tooling wave is
`ca2a3ff81ca89fa47cf5295930617b385acf7dea`.

Raw JSON outputs were written under the user temp directory and are not
checked into the repository. Reproduction commands are in
`verify/dogfood/README.md`.

## Physical fixture scale

| Profile | Generated content | Independent comparison | Cleanup |
| --- | ---: | --- | --- |
| Small | 143 files / 26 directories; special cases hash-verified | PASS | PASS; marker-guarded reset |
| Medium | 10,023 files / 35 directories; 10,057 manifest entries | PASS; paths/types/sizes/attributes | PASS; marker-guarded reset |
| Large | 100,023 files / 125 directories; 100,147 manifest entries | PASS; paths/types/sizes/attributes | PASS; marker-guarded reset |

Hashing is opt-in for Medium/Large because the PowerShell per-file hash walk
is not a bounded default. File-operation acceptance uses independent Win32
filesystem assertions and the Small fixture's content hashes.

## Indexed-engine benchmark

The release-test benchmark used the real SQLite/index projection, indexed
search, snapshot publisher, and snapshot parser. The latest repeatable 100K
run reported:

| Dataset | Generation | Projection rebuild | Snapshot export | Snapshot UI map/parse |
| ---: | ---: | ---: | ---: | ---: |
| 10,000 | 333 ms | 221 ms | 6.5 ms | 12.3 ms |
| 100,000 | 1.78 s | 966 ms | 67.9 ms | 68.9 ms |
| 500,000 | 74.1 s* | 4.31 s | 343 ms | 307 ms |

`*` The 500K generation value is a materially slower host run; earlier
same-host runs were 13–15 seconds. A separate 100K repeat completed in 26.9
seconds overall and measured search p90 values of 0.47 ms (selective), 49.26
ms (moderate), 34.60 ms (broad), 29.98 ms (prefix), 30.50 ms (extension),
and 34.33 ms (worst substring). The slower run is retained as variance and
was not optimized without a profile or a UI trace.

The largest actual published snapshot probe was 1,980,421 rows. The benchmark
also records a theoretical serialized-capacity estimate of 2,200,468 rows,
but the 2,200,468 and 2,200,469 probes were correctly rejected as oversized.
The benchmark output label was corrected in `ca2a3ff` so the estimate is no
longer reported as a successful publication.

## Launch and resource soak

The diagnostic executable's committed soak (`Invoke-DogfoodSoak.ps1`) ran for
30 seconds with seven samples. Both UI and engine reached a live state and
were then stopped by exact PIDs owned by the script; cleanup left no process.

| Process | Working set | Private bytes | Handles | Threads | Idle CPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| FastFiles | 66.6–66.7 MB | 41.9–42.0 MB | 1,199–1,203 | 28–30 | ~0% |
| FastFilesEngine | 17.0 MB | 2.9–3.0 MB | 191 | 10 | ~0% |

The diagnostic launch path is the portable user-facing path for this campaign.
The unsigned release-test UI was not counted as a launch pass: its engine
refused startup because production Authenticode pins are intentionally absent.
That is the expected fail-closed signing behavior, not a bypass or a product
regression.

Folder-open, preview, and storage latency were not inferred from this
engine-only measurement. Their correctness evidence remains the semantic UIA
and headless component tests; a future UIA timing run can add like-for-like
latency numbers without changing this baseline.
