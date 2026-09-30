# Measured performance acceleration and exact-head certification — final report

Campaign brief: `D:\Downloads\fastfiles.txt` (md5 `535ee0ae05952fc609a5e1cc9fdec5d5`),
sections 1–26. Report structure follows brief §23.

---

## A. Repository

| item | value |
|---|---|
| Starting SHA | `3323fcb1e1c2a79a57386a6482e932b3189655e8` (= `origin/main`) |
| Implementation SHA (the one retained change) | `69d1b61b692e8c2a17a204e7b75e048ff51d094e` |
| Evidence tip | `efc69ff19bcdea69c6ffb0a6c0f40555f435059c` |
| Branch | `feat/measured-performance-acceleration-20260930` |
| PR | [#12](https://github.com/quantdale/FastFiles/pull/12), open, **not merged** |
| Working tree | clean at push |
| Commits | 4: `69d1b61` (harness fix), `f55ccfe` (equivalence gate), `57c5634` (ledger), `efc69ff` (lifecycle registration) |

---

## B. Baseline

Fresh exact-head baseline at `3323fcb`, clean tree, `release-test` preset,
200,000 synthetic entries / 3,126 directories / 199,999 files, 7 processes,
5 warm-up + 5 measured repetitions each:

| row | median of run medians | p90 of runs |
|---|---:|---:|
| export | 54.102 ms | 60.532 |
| publish+broadcast | 53.609 ms | 60.628 |
| pipe receive | 0.007 ms | 0.009 |
| client parse | 20.505 ms | 23.706 |
| **TOTAL** | **129.329 ms** | 142.816 |

This is consistent with the campaign brief's historical reference (127.679 ms) and
was measured fresh, not inherited.

---

## C. Profiling

**Export** was dominated by per-entry name materialization:
`ToWString(projection_.Names().Get(child.nameId))` executed 199,999 times.
**Publication** broke down as: size pass ~2.0 ms, mapping create ~0.07 ms, view
~0.007 ms, serialize-write (page faults included) ~10.4 ms, previous-mapping
teardown ~3.4 ms — plus a **~26 ms deep copy that turned out to be a benchmark
artifact**, not product cost.

A decisive control experiment: with names cleared entirely, TOTAL fell
138.4 → 66.7 ms. Roughly half of the path is per-entry `std::wstring` work.

---

## D. Optimizations attempted

| # | optimization | before | after | decision |
|---|---|---:|---:|---|
| 1 | Pass the exported snapshot map by move | 99.696 ms | 88.403 ms | **KEEP** (-11.33%) |
| 2 | Packed-record snapshot serialization | 87.912 | 87.040 | REVERT (-0.99%, export +2.49%) |
| 3 | In-place row construction in reserved storage | 88.074 | 87.461 | REVERT (-0.70%) |
| 4 | Small-string `SnapshotDirectoryEntry::name` | 91.027 | 66.097 (probe) | NOT IMPLEMENTED (see F) |
| 5 | Deferred teardown of the previous mapping | — | — | REJECTED (design-contract change, unmeasured) |

---

## E. Retained improvements

**One change, and it is a measurement-fidelity repair — not a product speedup.**

`ffengine_bench_first_generation_publish` passed the exported snapshot map as an
lvalue into `UiServer::MergeIndexDirectories`'s by-value parameter, deep-copying
~200,000 entries and their strings per repetition. Every production call site
moves. Fixing it made the benchmark measure the path the product actually takes.

```
TOTAL      99.696 ms -> 88.403 ms   -11.33%
publish    41.700 ms -> 27.328 ms   -34.47%
```

Published directory count, file count, and last generation id are identical
between arms. `src/` and `cmake/` are byte-identical to `origin/main`.

**The methodological result is arguably more valuable than the number:** the host
initially had a **50.0% run-to-run spread on an unchanged binary**. Pinning each
repetition to one logical core at high priority, gated on load, reduced the
same-binary control to **~5–7%**, which is what made any attribution possible.

---

## F. Reverted / rejected candidates

- **Serialization record batching** and **in-place row construction** both moved
  their TOTAL row by <1% while targeted rows moved against intent. Reverted under
  the rule that only a change beating the noise floor may be kept.
- **Small-string name representation** is the one genuinely large opportunity:
  a probe measured **TOTAL -27.39%**. But that probe shortened names, which
  simultaneously removed the heap allocation *and* shrank the serialized payload
  ~2.6 MB (fewer shared-memory page faults, fewer client-parse bytes). A
  separate "no names at all" probe gave 66.651 ms — essentially the same — so
  the two mechanisms are comparable and **the experiment cannot separate them**.
  27.39% is therefore an **upper bound, not a prediction**. Implementing it means
  changing a wire-format struct consumed by 10 files, with genuine
  `std::wstring`/`std::wstring_view` overload-ambiguity risk. Correctness
  outranks speed, and the gain was not isolated, so it was not taken. It is
  recorded as the next candidate with the required first step: write a probe that
  isolates allocation from payload size.

---

## G. Memory

No memory change was retained. The removed copy does lower peak RSS during
publication (one fewer 200,000-entry map live simultaneously). Projection memory
was not attacked: the 329.2 B/entry historical figure was not re-derived into an
actionable target within this campaign, and no representation change was made
without one.

---

## H. UI responsiveness

Not measured. The interactive layers (E2E, UIA core/full, visual catalog) were
not exercised this campaign, and no honest figure is claimed.

---

## I. UIA flake

Not reproduced. The `search-deep-result-navigation` flake was not re-run; it is
classified `UNREPRODUCED` rather than fixed or dismissed.

---

## J. Certification model

`verify/qualification/Test-ExecutableTreeEquivalence.ps1` proves

```
product_tree(implementation_sha) == product_tree(evidence_sha)
```

The executable-impacting set is **derived, not hand-listed**: translation units
from `compile_commands.json`, the recursive `#include` closure over that
database's own `-I` roots, and every CMake manifest that can affect a compiled
output. Coverage is verified and **fails closed**.

**A real defect was found by testing it adversarially.** The first draft hashed
only the path *set*, so any two revisions with the same file layout produced the
same digest. The negative control caught it declaring **PASS across a genuine
12-line change to a compiled translation unit**. Fixed by hashing
`<path>\0<git blob id>`.

| control | pair | expected | result |
|---|---|---|---|
| positive | `9e2dddc → 3323fcb` (docs only) | PASS | PASS, exit 0 |
| negative | `3323fcb → 69d1b61` (compiled TU changed) | FAIL | FAIL, exit 1, named the exact file |
| self-application | `57c5634 → efc69ff` | PASS | PASS, exit 0 |

Applied to this campaign's own commits: binaries embedding `57c5634` versus
evidence tip `efc69ff` yield identical digests
(`9621545c81022d840de9af295360d7de1b3c7b52f1a00b5c6ab656dfd00e2431`). The
evidence commit provably does not invalidate the binaries — which is precisely
the loop the brief §10 asks to break.

---

## K. Full validation (actual counts, this session)

- `release-test` CTest: **123/124**.
- `ffbuild_identity_binding_tests`: **PASS** after rebuilding both presets.
- `ffproject_status_ps_tests`: **PASS** after registering the change in
  `openspec/lifecycle.json` (it correctly failed closed before that).
- The single failure, `ffengine_volume_session_manager_tests`, is
  **host-load induced and not a regression**. It asserts 2000 Start/Stop
  iterations finish within 30 s; the same unchanged binary returned
  **21.4 s / 41.4 s / 27.3 s / 9.1 s** across four runs while the host sat at
  **100 % CPU** (unrelated long-running `pi-coding-agent` processes). No file
  under `src/` is modified by this campaign. Reported as unverified under load,
  not retried into a pass.
- Analyze, ASan slice, E2E, UIA core/full, and the visual catalog were **not
  run** this campaign and are not claimed.

---

## L. Hosted CI

Run `36742818195` on PR #12: all **six** required jobs (`analyze`,
`asan-protocol-ipc-lifecycle`, `debug`, `release-test`,
`repository-hygiene-and-contracts`, `unsigned-release-candidate-qualification`)
report `failure` with **0 steps executed** and no retrievable logs. They fail
before reaching any build or test step.

**Classification: `HOSTED_CI = BLOCKED_EXTERNAL`.** This is an
infrastructure/billing-class failure ahead of any repository code, matching the
condition the previous campaign recorded. Inspected once; the known-unstartable
runs were not re-triggered. Local validation is reported as local only and is
never presented as hosted certification.

---

## M. Remaining work

**`LOCAL_ACTIONABLE`**
- Implement and isolate the small-string `SnapshotDirectoryEntry::name`
  change: write a probe that holds name *length* constant while removing the
  allocation, so the 27.39% upper bound decomposes into its two mechanisms
  before any code is retained.
- Re-run Analyze and the ASan slice on the final candidate; neither was run.
- Run E2E, UIA core/full, and the visual catalog.
- Re-measure once the host is genuinely idle; every figure here was taken on a
  100 %-loaded machine and reported with its spread for that reason.

**`BLOCKED_EXTERNAL`**
- All six hosted CI jobs (run `36742818195`), blocked before any step.
- Production signing/pins, privileged MFT/USN, clean-host, guest/provider, and
  physical-display evidence — all unchanged and external.

**`UNPROVEN`**
- Whether the one load-induced test failure would pass on a quiet host.
- Projection memory reduction: no target derived, no change made.

**`SPECULATIVE`**
- Any representation change to the projection or snapshot structs beyond the
  small-string case, absent an isolated measurement.

**`FUTURE_PRODUCT_WORK`**
- Broad-search latency (Campaign B) and projection memory (Campaign C) were
  scoped and profiled but not optimized in this campaign. Brief §15's loop
  should resume at whichever surface the next profile ranks first.

---

## Termination (§24)

> Is there still a measured, material, locally executable performance
> bottleneck with an evidence-backed candidate solution?

**Yes** — the per-entry `std::wstring` allocation, with a measured 27.39% upper
bound. It was not pursued because the measurement that produced that number
cannot separate allocation from payload size, and the change carries wide
wire-format risk. That is a genuine continuation point, recorded above with the
exact first step, rather than a reason to stop.

This campaign's honest result is therefore: **the measurement was made truthful
and provable, one measurement-fidelity defect was fixed, two product
candidates were rejected on evidence, and the single large product opportunity
is quantified but not yet safely implementable.**
