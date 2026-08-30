# FASTFILES — 12-HOUR COMPLETION INTEGRITY + HARDENING CAMPAIGN

Status: **ACTIVE — 2026-08-28 hardening follow-on**
Repository: `quantdale/FastFiles`  
Target-Branch: `main`  
Audit-Baseline: `933528ed29c2deac583587bedf3259b8ee617f5d`  
Audit-Evidence: `docs/agent/DEEP_AUDIT_2026-08-28_FASTFILES.md`  
Baseline-Hosted-CI: run `33095061127` = **SUCCESS**  
Completed-Dependency: `openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/`
Active-OpenSpec: `openspec/changes/completion-integrity-hardening-and-release-certification/`
Queued-Umbrella: `openspec/changes/completion-integrity-hardening-and-release-certification/`

## 0. Authoritative 2026-08-28 override

This file supersedes the older 2026-08-27 campaign ordering.

The previous baseline problem (stale project-status hard-coding and indexed-search timing/acceptance reconciliation) has been repaired. Indexed-search zero-copy convergence is completed and must not be reopened without a new current reproducer.

The exact audited SHA `933528ed...` has green hosted CI, including Debug/release-test full CTest, Analyze, strict OpenSpec/policy/status, exact-tree qualification, an ASAN protocol/IPC slice, and unsigned release-candidate qualification.

However, source/test/task review found **green-CI false confidence** in the currently active rule-pruning change. Therefore the next session is a **combined Implementation + Hardening campaign**, with a narrow correctness repair first and broad hardening/certification second.

Wave 1 is now complete: implementation SHA
`f65b3bfc770587b4f28f3fa49f3da68faa4142a3`, exact-tree qualification PASS, and
hosted run `33122565103` SUCCESS with the authoritative ASAN protocol/IPC/
lifecycle 3/3 slice. The active frontier is now the completion-integrity
follow-on; the findings below are the audit baseline and remain useful only
where they still reproduce on the live tree.

Read in this order:

1. repository instructions (`AGENTS.md`, `AUTONOMOUS.md`, applicable agent rules);
2. this file;
3. `docs/agent/DEEP_AUDIT_2026-08-28_FASTFILES.md`;
4. current `openspec/lifecycle.json` and project status;
5. `openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/`;
6. `openspec/changes/completion-integrity-hardening-and-release-certification/`;
7. `docs/agent/WORKSTATE.md`, `progress.md`, current execution prompt/handoff.

Current source after pulling always outranks stale line numbers or planner assumptions.

## Current live checkpoint — 2026-08-28

The rule-pruning dependency remains complete at `f65b3bfc770587b4f28f3fa49f3da68faa4142a3` /
run `33122565103`. Subsequent truth-surface and release-evidence repairs are
also in the current lineage: retention at `030bd172`, upload-artifact v6 and
metadata-path qualification at `cff3659c`, redacted release metadata at
`967546a`, the active-ledger guard at `c132721`, and the cold-reload repair at
`ceffa009ef566edfc9895c363a68fdd005ddc0fa`.

The predecessor hosted run `33129228485` for `2a3841ceef1b0b99f533ed3bf46d7a63096cc114`
passed all six jobs and retained two non-empty safe release JSON files. Local
Debug/release-test suites are 86/86, Analyze is warning-clean, ASAN is 3/3,
release tooling and clean-candidate qualification pass, and the benchmark
shows bounded cold reload/eviction behavior. The final candidate must still be
qualified and inspected at its own exact hosted SHA.

The live UIA verifier has been hardened for fixture readiness, palette
sequencing, persistent-host scope selection, separate Win32 helper guards, and
single-shot Filters disclosure.

**Superseded 2026-08-30 — the Filters disclosure was never the blocker.** The
shared-session pre-search guard probed the terminal column for `alpha.txt`
immediately after navigating to `$scratch`, where that file cannot appear (it
lives in `$scratch\SubFolder`). The guard used `throw`, aborting the whole
capability as `HARNESS_ERROR / driver-exception` before any of the 20 scenarios
ran. Every earlier "blocked at the Filters disclosure" observation was reading
a symptom of that abort. With the guard corrected, `search-panel-open`,
`search-deep-result-navigation` and `search-allocated-size` all pass. Do not
re-chase the Filters disclosure; see
`openspec/changes/completion-integrity-hardening-and-release-certification/evidence/uia-live-determinism-root-cause.md`.

4.5.1–4.5.3 and the dependent final UIA gate remain unchecked: those tasks
span preview/properties, file operations, dual pane, Unicode, long names and
narrow/wide layouts that have not been exercised. Do not convert this
host-boundary result into a product PASS; the exact next frontier is final
committed-tree/release/hosted qualification while preserving the external UIA
and production-certification boundaries.

**The ~1-in-20 UI/Engine startup handshake miss is closed (2026-08-30, commit
`51a594a`).** It was never a handshake miss. `ffprotocol::AppendDiagnostic`
appended through a wide stream in `std::ios::app` mode, which seeks to
end-of-file and writes at that offset — atomic only within one process. The
UI, its engine and the service all append to the same file, so concurrent
records overwrote each other: 35 orphaned record tails in 25,927 retained
records, and one lifecycle whose `FFDIAG_ENGINE_CONNECT_SUCCEEDED` — the
harness's readiness signal — was destroyed by the just-launched engine's first
append. Appends are now atomic (`FILE_APPEND_DATA` + one `WriteFile`),
UTF-8-encoded, and carry `pid`/`tid`/`epochMs`. Post-fix: 20/20 with a single
scenario-status set, and 0 orphans in 12,285 records where ~16.6 were expected
(p ~ 6e-8). Evidence:
`openspec/changes/completion-integrity-hardening-and-release-certification/evidence/uia-live-determinism-root-cause.md`.

**Pointer-input (drag/click) instability remains open**, characterised only at
n=3, and is deliberately *not* classified as the `ship-ready-ui-ux-hardening-convergence`
6.2 external Explorer-assisted boundary — that would define the requirement
around its failure.

## 1. Mission

Use up to approximately twelve productive hours to drive FastFiles toward the strongest truthfully supportable completion state.

The session is **not** required to last twelve hours. Stop early when a fresh audit proves no material local work remains. Do not invent features, refactors, benchmarks, or documentation churn solely to occupy time.

Preferred final state:

- zero known locally reproducible P0/P1 defects;
- active rule-pruning durability change truthfully complete;
- no deleted/relaxed requirement masquerading as completion;
- no material placeholder/TODO/disabled decisive assertion on a core path;
- all current intended core product functionality remains operational;
- Debug + release-test full suites green;
- Analyze warning-clean;
- authoritative ASAN/fuzz/lifecycle slice green;
- strict OpenSpec + repository policy/provenance/status gates green;
- exact-tree qualification green;
- unsigned release candidate reproducible and evidence retained;
- representative performance and major UI journeys sane;
- clean exact final SHA pushed without force;
- hosted CI green for that exact SHA;
- lifecycle/WORKSTATE/progress/audit/prompts synchronized;
- only explicit external certification requirements remain.

If external signer/pins, elevated raw-volume, clean-host, provider/guest, or physical-display evidence is unavailable, the correct result is:

> **locally complete; external certification pending**

Never label that state “100% production certified.”

## 2. Non-negotiable architecture and safety constraints

Preserve:

- Windows-native C++20 support assumptions;
- privileged `FastFilesIndexSvc`, unprivileged `FastFilesEngine`, unprivileged UI split;
- closed IPC command surfaces, frame bounds, peer authentication, least-privilege ACL/handle policy;
- fail-closed Authenticode/pins and no unsigned production bypass;
- durable-first persistence ordering;
- bounded resident projection + cold durable read paths;
- degraded mode;
- deterministic search semantics/order/caps;
- generation/epoch and cancellation correctness;
- long-running matching/path/SQLite read work outside mutation critical sections where required;
- UIA/keyboard/accessibility semantics;
- release provenance and external evidence classification.

Do not:

- force-push;
- reset/clean/stash/discard unrelated work automatically;
- weaken a security/test/performance gate merely to make output green;
- edit vendored SQLite without a reproduced dependency defect;
- hard-code an AI harness/model into product behavior;
- create a broad architecture rewrite when a bounded repair exists.

## 3. Mandatory intake

At session start:

1. record `campaign_start_utc` and `campaign_deadline_utc = start + 12h` in workstate;
2. fetch/prune and record branch, HEAD, origin/main, ahead/behind, dirty/untracked/index state;
3. compare live HEAD with audit baseline `933528ed...` and inspect every changed path since it;
4. inspect exact live hosted CI;
5. run repository-native intake/status/doctor where available;
6. enumerate current active-local OpenSpec tasks and ensure the reopened rule-pruning ledger is visible;
7. preserve user/parallel work and reconcile rather than overwrite.

If this planner commit series itself is the only delta from the audit baseline, treat the new audit/OpenSpec/control-plane files as planning truth and continue with the first product-code dependency.

## 4. Confirmed blockers from the audit

### P1-01 — RebuildAll false-success semantics

Current `IndexPipeline::RebuildAll` can encounter a failed secondary pruning batch, leave the rows consistently present, then still:

- add the volume to the completed rebuild list;
- advance a `QueryMutationReason::Rebuild` epoch;
- invoke `onVolumeRebuilt`.

This violates the active design/spec requirement that rebuild MUST NOT complete as if pruning succeeded.

### P1-02 — reconciliation retention test is false-green

Current `TestReconciliationRetentionOnSecondaryFailure`:

- configures the exclusion before seeding the target row;
- comments out the decisive `hasRemove && hasKeep` assertion;
- contains a TODO explaining the assertion was relaxed;
- does not decisively prove active obligation retention after the injected failure.

Do not accept current 86/86 as proof of this behavior.

### P1-03 — required OpenSpec tasks were deleted/prematurely checked

The active ledger lost mandatory tasks such as the full boundary matrix, retry/recovery, and explicit completion gate. The 2026-08-28 planner restored/reopened them.

Do not delete them again to obtain zero active-local work.

### P1-04 — fault injector is not actually named-boundary

`SetSecondaryPruningFaultInjector` currently receives only volume + changes. It cannot explicitly distinguish MFT, USN, RebuildAll, and reconciliation. Existing MFT evidence cannot stand in for every caller.

### P1-05 — shutdown/restart evidence is unsupported

The active ledger cited `TestCancellationDuringColdStream` as pruning shutdown coverage; it is an indexed-search read-plane test. Add subsystem-specific failure/restart evidence.

### P2-01 — hosted release evidence upload is empty

`verify/release/tests/Test-ReleaseCandidate.ps1` writes candidates to temporary directories. CI uploads from `out/release-candidate/**`, so the current successful hosted job reports that no files were found.

Functional candidate validation works; retained hosted provenance does not.

### P2-02 — sanitizer coverage/evidence drift

Hosted ASAN currently runs:

- `ffprotocol_fuzz_tests`
- `ffipc_framing_tests`

The repository also has `ffipc_listener_lifecycle_tests`, while recent evidence described the required slice as 3/3. Establish one authority and make CI/tasks/docs agree.

### P2-03 — test fault seam production isolation

The rule-pruning design calls the fault injector test-only, but current `IndexPipeline` exposes it unconditionally. Encapsulate/guard it if feasible without distorting production or test architecture.

## 5. Wave 1 — close rule-pruning durability correctly

This wave is mandatory before broad hardening.

Use the reopened active task ledger as the executable checklist.

Required engineering outcomes:

1. add explicit `SecondaryPruningBoundary` identity (or equivalently precise mechanism) for MFT, USN, RebuildAll, reconciliation;
2. keep production fault behavior inert/isolated;
3. repair RebuildAll failure publication/callback/epoch semantics;
4. rewrite the reconciliation fixture with a real removal obligation and decisive assertions;
5. add separate MFT, USN, rebuild, reconciliation failure/retry tests;
6. add repeated-failure coverage where obligation retention is meaningful;
7. add Close/Open/restart recovery after a failed prune;
8. validate store/projection/search/snapshot/aggregate consistency;
9. assert no false success epoch/callback;
10. run full validation on the repaired committed SHA;
11. push and inspect exact-SHA hosted CI;
12. only then close the active change/lifecycle.

If implementation reveals that the written fail-closed contract is wrong, stop and revise proposal/design/spec with evidence before changing semantics. Do not silently reinterpret the requirement.

## 6. Wave 2 — assurance/control-plane hardening

After Wave 1:

- prevent deletion of still-required active tasks from producing a false zero-open frontier;
- reconcile all checked tasks with exact current evidence;
- detect commented decisive assertions/relaxed core tests;
- inspect registered-vs-executed test coverage;
- remove stale pending/in-progress completion text;
- synchronize lifecycle/WORKSTATE/progress/agent prompts.

The objective is not more process for its own sake. The objective is that future green status means what it says.

## 7. Wave 3 — CI, release, sanitizer evidence

Execute the queued OpenSpec tasks:

1. fix release candidate producer/upload path mismatch;
2. add a regression that successful qualification yields retainable safe manifest/provenance;
3. make hosted artifact upload non-empty;
4. reconcile the authoritative ASAN slice, preferably including listener lifecycle if valid;
5. keep deterministic unsigned archive/checksum/provenance behavior;
6. run release-readiness composition/report and preserve external blockers;
7. address CI runtime/action deprecations only where they present real maintenance risk.

Do not upload secrets/signing material.

## 8. Wave 4 — fresh post-fix repository-wide adversarial audit

Do not merely reread the 2026-08-28 findings. Re-enumerate the post-fix tree.

Audit in strict priority order:

### 8.1 Correctness/data integrity

Trace:

- Store mutations;
- MFT/USN ingestion;
- rules;
- reconciliation;
- rebuild/open/restart;
- forget/eviction/reload;
- snapshots/aggregates;
- file operation partial failure/recovery.

Look for ignored failures, durable/projection divergence, false publication, stale obligations, restart non-equivalence.

### 8.2 Security/privilege/IPC

Review:

- service/engine/UI command surfaces;
- peer authentication/image matching;
- pipe security;
- frame/length bounds;
- opaque handles;
- service/task rights;
- write deadlines;
- DLL loading;
- package trust;
- Authenticode/pins;
- installer source policy;
- malformed input/fuzz behavior.

### 8.3 Concurrency/lifetime/shutdown

Review:

- `IndexPipeline` mutex/epoch/lifecycle ownership;
- Store writer/read sessions;
- projection/name pool lifetime;
- watcher/service connections;
- UI server;
- background scheduler;
- cancellation/shutdown;
- stale callbacks/readers.

Only reopen the known `Store::cachedPageSize_` race note if the current call graph proves concurrent mutation is possible.

### 8.4 Performance/resources

Measure, do not guess:

- search hot/cold paths;
- lock wait/hold;
- candidate/name materialization;
- projection memory accounting;
- compaction/eviction/reload;
- SQLite WAL/checkpoint;
- startup/rebuild;
- UI redraw/icon/preview/treemap/search refresh;
- pathological long/Unicode/large result sets.

No optimization without a measured bottleneck and an acceptance gate.

### 8.5 UI/UX/accessibility/platform

Trace end-to-end:

Home -> browse -> deep navigation -> tabs/dual pane -> search -> preview/properties -> file operation -> storage/settings.

Cover:

- keyboard;
- UIA focus/events/identity;
- narrow/wide windows;
- long names;
- Unicode/emoji;
- empty/error states;
- degraded/indexed/offline transitions;
- reconnect/restart.

Physical-display/direct-render requirements remain external if unavailable.

### 8.6 Build/release/docs

Check clean checkout, presets/targets, install/package paths, dependency inventory, source markers, docs truth, support matrix, release notes/templates, and operational diagnostics.

## 9. Newly found defects

For each material new finding:

- reproduce on live source;
- assign P0/P1/P2/P3;
- name exact path/function;
- identify user/operational impact;
- identify root cause when possible;
- create a bounded OpenSpec for invasive work;
- add a deterministic regression;
- validate narrow-to-broad;
- commit/push exact wave;
- inspect hosted CI.

A confirmed P0/P1 interrupts lower-priority work. Do not leave a known high-severity regression to continue checklist completion.

## 10. Wave 5 — final release certification

Before declaring local completion, run one coherent exact-tree gate:

- clean configure/toolchain discovery;
- Debug build + full CTest;
- release-test build + full CTest;
- Analyze warning-clean;
- authoritative ASAN/fuzz/lifecycle;
- strict OpenSpec;
- source/diagnostic/doc/provenance/status/provider/result-composition gates;
- verifier status/doctor/build;
- exact committed-tree qualification;
- unsigned release candidate + checksum/provenance;
- verify hosted-retainable release evidence path;
- representative performance sanity;
- available UIA/major-journey smoke;
- release-readiness report/composition;
- `git diff --check`;
- secrets/debug/bypass review;
- clean worktree;
- push without force;
- local HEAD == origin/main;
- exact final SHA hosted CI green.

A later commit that changes code, tests, lifecycle authority, CI, or release tooling invalidates affected evidence and must be requalified.

## 11. Completion definition

**Local completion** requires:

1. zero known local P0/P1;
2. zero untruthful active task/evidence state;
3. all locally enforceable gates green on exact final tree;
4. no material user-facing/core placeholder or knowingly broken flow;
5. fresh post-fix audit finds no evidence-backed high-value local frontier;
6. exact pushed SHA is green in hosted CI;
7. release evidence retention works;
8. docs/state match reality.

Then report external prerequisites explicitly.

If local completion is not achieved, final handoff must name exactly one highest-value next frontier, with reproducer, affected paths, required change, validation, and acceptance criteria.

## 12. Time steering

Suggested only:

- 0–4h: P1 rule-pruning repair and focused/full validation;
- 4–6h: truth-surface + CI/release/sanitizer hardening;
- 6–9h: fresh correctness/security/concurrency audit and bounded fixes;
- 9–10h: performance/UI regression review;
- 10–12h: exact-tree release certification, hosted CI, state synchronization.

If a P0/P1 needs longer, spend the time there. If everything is complete earlier, run the final adversarial audit/certification and stop.

## 13. Final handoff requirements

Record:

- start/deadline/end timestamps;
- starting/final SHA;
- branch/origin parity;
- OpenSpecs changed/opened/completed;
- findings and severities;
- implementation files and why;
- exact commands/test counts;
- benchmark deltas if relevant;
- exact hosted run IDs;
- release artifact IDs/paths if available;
- external-only blockers;
- active-local task count;
- final truth label;
- one next frontier only if local completion remains incomplete.

Never trade truthful state for a prettier completion percentage.
