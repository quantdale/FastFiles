# Secondary filename-index evaluation

This is the decision artifact for the measured secondary filename-index
architectures. The benchmark is informational and uses the same canonical
`ffsearch::Query::MatchesView` matcher for verification after candidate lookup;
it does not change the production projection or query path.

Command used for the final matrix:

```powershell
build\release\tests\engine\ffengine_bench_secondary_filename_index.exe --scales 100000,500000,1000000,2000000 --output docs\benchmarks\secondary-filename-index.json
```

## SQLite capability

The vendored SQLite is 3.53.4, but the target is compiled without
`SQLITE_ENABLE_FTS5`. `CREATE VIRTUAL TABLE ... USING fts5` fails with
`no such module: fts5`, and the FTS5 trigram tokenizer is unavailable as well.
Ordinary tokenized FTS would not preserve arbitrary infix semantics anyway:
the benchmark's tokenized-infix probe is a required recall check, not an
assumption that token search is a substring index.

## Architectures

### Linear canonical scan

This is the existing correctness baseline. It has no secondary build or index
memory cost, but selective and no-match queries remain O(N).

### UTF-16 trigram postings

Each filename contributes unique three-UTF-16-code-unit grams to sorted posting
lists. Query grams are intersected, then every candidate is verified by the
canonical matcher. ASCII literal terms of at least three units are eligible;
short terms, Unicode terms, globs, multi-term queries, folder predicates, and
metadata predicates remain on the linear canonical path. Non-ASCII names are
kept in a conservative verification bucket for ASCII queries, preventing
false negatives from the accelerator.

At 2M entries:

| Query | Canonical matches | Candidates | Reduction | Median |
| --- | ---: | ---: | ---: | ---: |
| selective | 1 | 4 | 99.9998% | 0.01 ms |
| no match | 0 | 3 | 99.9999% | 0.00 ms |
| moderate `report_` | 196,875 | 196,877 | 90.16% | 19.51 ms |
| broad `common_` | 588,654 | 588,657 | 70.57% | 40.24 ms |
| late substring `7391` | 1 | 597 | 99.97% | 0.04 ms |

The exact machine-readable values, including all query classes, are in
[`secondary-filename-index.json`](secondary-filename-index.json). The table
above highlights the scaling behavior without replacing the full artifact.

Build and memory scale approximately linearly: 54.0 ms / 8.8 MiB at 100K,
285.1 ms / 45.3 MiB at 500K, 574.2 ms / 91.2 MiB at 1M, and 1,198.9 ms /
168.6 MiB at 2M. The memory figure is an explicit container-allocation
estimate; the benchmark also records process private-byte deltas. The
persistent-size figure is a compact posting-stream proxy, not a committed
database schema.

All 12 eligible query classes had zero false negatives and exact canonical
verification at every measured scale. The 10 ineligible classes are explicitly
marked as fallback-only and were never treated as accelerated semantics.

Mutation probes at 2M remained semantically exact after each batch. For 100,
1,000, and 10,000 operations respectively, trigram insert/delete/rename times
were approximately 33/4/45 ms, 35/27/453 ms, and 45/268/4,574 ms. The rename
cost is the important tradeoff: sorted posting-list erase/insert is measurable
for large batches, but remains bounded and correctness-preserving.

### Sorted normalized names / prefix ranges

The sorted-name structure costs 246.0 MiB at 2M and does not accelerate the
current literal query because Fast Files literal matching is arbitrary infix
matching. The benchmark also ran the tempting prefix-only range as a separate
probe: at 2M, `photo_` produced 196,874 prefix candidates but missed 1,969
canonical infix matches created by names containing the term later in the
filename. It is therefore not a safe production accelerator without changing
search semantics.

## Decision

`utf16_trigram_postings` is the single measured winner. It is the only tested
design that materially removes the O(N) selective/no-match cost while
preserving the canonical match set, and it has a straightforward fallback for
unsupported query classes. FTS5 is unavailable in the shipped SQLite build;
prefix ranges are semantically unsafe for the current infix language.

Production integration is the next bounded slice. It must retain the same
generation ownership as the projection, update on insert/delete/rename, and
fall back to the canonical scan if construction or maintenance fails. No
production code or SQLite schema has been changed by this evaluation.
