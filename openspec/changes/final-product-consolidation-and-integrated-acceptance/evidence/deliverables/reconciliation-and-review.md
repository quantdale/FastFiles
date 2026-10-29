# Reconciliation and independent review record (tasks 6.1–6.3, 10.3–10.4, 11.1–11.2)

## 6.1 Attribution audit

- All 23 PR heads pinned in `../integration-ledger.json` verified ancestors of
  the candidate tip (23/23, `git merge-base --is-ancestor`, including the
  PR #14 chain commit `72b4f86` — the lane-B requested git-side spot-check).
- Source/test unions audited: registrations unioned by identity in
  `tests/ui/CMakeLists.txt`, `tests/engine/CMakeLists.txt`; no duplicate test
  targets; no conflict markers anywhere in the tree (repo-wide grep).
- No privilege/trust/protocol drift: lane B re-verified the closed command
  enum, SCM rights, Authenticode fail-closed Release behavior, frame/parser
  hardening and the MFT no-$DATA invariant against the actual tree.
- Local-only inputs: `../local-input-reconciliation.md` (2 accepted artifacts,
  everything else superseded or subsumed with evidence).

## Independent review (tasks 6.2/10.3) — two fresh-context read-only lanes

Run `cfb94ebf-aafd-478e-a9b2-9f4627d539fc` over the integrated candidate tree
(branch tip at review launch `06abcfe0` build identity; tree contents identical
to the reviewed candidate). Both lanes returned **OK with notes**; no P0/P1.

**Lane A — product/data correctness** (child `ff3b4e00`):
- Merge resolutions verified correct: `WindowShell.cpp` WM_DESTROY union keeps
  both behaviors (idempotent `StopRanking` + phase marker, safe ordering);
  `test_projection.cpp` union complete (both sides' tests defined and invoked
  exactly once); imported residency test API-consistent and registered.
- File-operation chains hold every data-safety invariant: no overwrite without
  consent (`RenameWithoutReplacement`/`CREATE_NEW`/`CreateDirectoryW`
  boundaries), per-request consent scope (no cross-request inheritance,
  fail-closed to Cancel), generated-result protection (fail-closed
  `EvaluateReplacementSafety`, disjoint replacement execution), restore/undo
  preflight and undo-ineligibility of replacement batches.
- Index/store: the P1-2 fit guard is the FIXED measured-growth version
  (mid-stream interval checks + bounded abort + cold retention); the disproven
  flat 512 bytes/row prior exists nowhere in `src/`; P1-1 epoch fix intact.
- **P2 (fixed below):** Link executor's raw-name fallback for truncated plans.
- Note: `TransferPathsOverlap` helper duplication (tests vs
  `EvaluateReplacementSafety`) — accepted drift point, no defect.

**Lane B — evidence/security/regression integrity** (child `0389a0b5`):
- All five security invariants intact (closed protocol enum 23 values,
  client SCM grant QUERY-only, Authenticode placeholder→fail-closed with the
  diagnostic escape hatch compile-time-gated to Debug, frame length validated
  before allocation in u64 arithmetic, MFT parser reads only 0x10/0x30).
- OpenSpec/task honesty: no bulk-checked tasks; evidence files do not
  overclaim; the two lifecycle state corrections are accurate (both tasks.md
  100% checked).
- Evidence claims spot-verified TRUE: flat prior absent, measured-growth guard
  present (`Projection.cpp:1164-1245`), fitting regression present, imported
  test registered, probes not in default CTest.
- **P2 (fixed below):** acceptance-matrix header asserted the freeze event
  while task 1.5 was still unchecked.
- Note: containment percentages/SHAs require git-side verification — done by
  the supervisor (23/23 ancestor check; containment method documented in
  `../local-input-reconciliation.md`).

## Findings remediation (task 6.3/10.4)

| Finding | Fix | Regression | Re-review |
| --- | --- | --- | --- |
| A-P2: Link plan-tail fallback bypasses reservation | Link loop now requires a planned name per item; unplanned tails fail `ERROR_FILENAME_EXCED_RANGE` (commit "fail truncated Link plan tails explicitly") | `CheckLinkPlanTailFailsWithoutReservation` — RED (4 FAILs on prior code: tail wrote unreserved raw name, reported success) → GREEN (5/5); full Debug CTest 144/144 | focused fresh-context recheck (recorded below) |
| B-P2: acceptance-matrix freeze wording vs unchecked 1.5 | task 1.5 checked in `tasks.md` with the freeze evidence (the matrix WAS frozen at intake before product edits — `dd9c1d7` precedes the first merge) | n/a (docs) | covered by lane B's "smallest fix" option |

Recheck verdict (focused re-review `b37b5da9` on the A-P2 fix): **OK** — fix
correct and complete (no remaining raw-name fallback; cancellation mid-loop
preserved; per-item Progress events and completed/total accounting truthful for
failed tails), regression discriminating and non-vacuous, no other request
kind's behavior changed.

## 11.1 Predecessor reconciliation

Every predecessor change's closure is carried by its own branch evidence (the
stage merges preserved their tasks.md/evidence trees verbatim); the only
cross-branch closure differences were the two lifecycle states corrected above
and the original-vision `4.12` staleness noted in explore-findings (PR26's
checked evidence carried). Original-vision 4.1–4.3 integrated acceptance is
superseded by this change's journeys A–I (`../evidence/acceptance-matrix.md`,
`native-acceptance-report.md`); the obsolete "successor publication to a new
branch" wording is replaced by the central destination
(`integration/fastfiles-final-product-20261029`) in this record.

## 11.2 Task closure discipline

- This change's `tasks.md`: closed only with per-task evidence tags; journey-I
  executions (7.11/7.12) remain raw-unchecked with external
  disposition/REQUIRED-BUT-UNAVAILABLE (needs a second interactive host or
  product-side drop instrumentation); publication tasks (12.3–12.6) close at
  and after the actual publication per their evidence.
- Lifecycle/strict/drift validated after every stage (strict OpenSpec 106/106,
  lifecycle model PASS, doc drift PASS at the certification tip).
- Historical failures preserved: see `certification-report.md` §Known
  intermittents (first failures retained beside later passes).
