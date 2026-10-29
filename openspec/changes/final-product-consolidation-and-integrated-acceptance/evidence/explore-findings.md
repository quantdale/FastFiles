# Exploration findings and immutable intake anchors

## Scope and authority

This is a planning checkpoint, not an executed integration ledger or product certification. Read-only exploration fetched origin, inventoried all open PRs/worktrees, inspected ancestry and source/task differences, checked reviews/hosted evidence and used a fresh-context scout for predecessor acceptance. Propose re-fetched the same main/PR heads before creating the isolated central worktree. No application, benchmark, privileged operation or product merge was executed. All apply tasks remain unchecked.

Dates in requested branch names are identifiers, not proof of execution dates. Re-fetch and rederive live Git/PR/check state during apply; the identities below are historical references, never reset instructions.

## Verified source snapshot

- Main: `a26fe609439a18c128b2a2e557bc7eb2534fa128`; tree `d2debd948f53698e1a970ead6c759cd1303c176f`.
- Existing local simulation: `test/original-vision-integration-20261025` at `e400d7065571271b5052fa6e58e9971814d331d6`, clean at inspection, 207 commits ahead of main. This contains useful composite source/evidence, not an unrestricted final qualification.
- Original root checkout: `b77d61e7ad21b549be1b2c29bda35a85ce4a97e9` on the older large-index branch, without an upstream and with three pre-existing modifications: `docs/agent/WORKSTATE.md`, the large-index `evidence/rebuild/rebuild-root-cause-and-reachability.md`, and `tests/indexstore/test_projection.cpp`. These files/worktree are not owned by this proposal.
- At snapshot: 23 open PRs (#14–#36); all except #14 draft. Exact current parent heads were ancestors of their child PR heads. No `APPROVED` GitHub review was found; PR #14 had three COMMENTED reviews and two resolved threads. No unresolved GitHub review thread was found in the queried set. Local independent review documents remain separate scoped evidence, not GitHub approvals or final composite approval.

```text
main
+-- #14 -> #15 -> #17
+-- #16
+-- #18
|   +-- #19
|   +-- #20
|   +-- #21
|   |   +-- #22 -> #24
|   |   +-- #23 -> #25
|   |                 +-- #27 -> #30 -> #32 -> #33 -> #36
|   |                 +-- #34
|   +-- #26 -> #29
+-- #28 -> #31 -> #35
```

Nine maximal PR heads account for open-head ancestry. This is not semantic acceptance or a safe literal merge order:

| PR | Pinned head | Contained chain / responsibility |
| --- | --- | --- |
| 16 | `e75a230db9ac89f6a638754cb6ad672735e4d644` | F9 rendered-theme correction |
| 17 | `797722734319ea18c65e4f04bbacdcf245b47316` | #14/#15 core plus acceptance/root/cleanup follow-ups |
| 19 | `d2187aa45d0023a3878b94c8891989194ed4f9ac` | #18 plus Column View identity/focus |
| 20 | `a07970ecb4a025a463d8809641fa5bf2c8718a0d` | #18 plus native status layout |
| 24 | `b683f33ba271c0de2b7eb4bc38faf5f9f391fa8c` | #18/#21/#22 plus shutdown lifetime |
| 29 | `733a45b14d4a94cca1837248fa8f1c418fa55793` | #18/#26 plus quit preservation |
| 34 | `15b6e6632e206feb0ebf3eb7a59a68c7be81332b` | #18/#21/#23/#25 plus compact palette |
| 35 | `3ec322b16f7c3d54d31bb443baf9fe3423d07be4` | #28/#31 plus E2E evidence/result correctness |
| 36 | `ca6326fba6611e2a3be242b9485a1d76d29cb2d0` | #18/#21/#23/#25/#27/#30/#32/#33 plus rename/create safety |

The complete machine-readable ledger, per-input unique changes/tests, local-only equivalence and integration dispositions are apply task 1.2, not claimed complete here. For PR #14 the API-reported base SHA differed from the current fetched main tip; record both fields instead of treating PR metadata as the current base branch.

## Evidence and task-level corrections

The following use `SHA:path` notation because the files are not all present on the planning baseline. Inspect them through Git or the corresponding existing worktree; do not fabricate local links or import an entire stale branch to read one document.

- Product vision and native Refero direction: `981a9ba678a191107c78eec22a60c9620eb5848e:docs/product/original-vision-alignment.md` and `docs/product/original-vision-source-ledger.md`. The initial partial/unpolished pillar scorecard is a dated baseline, not a final integrated verdict. Degraded scope is browsed/pinned coverage, not whole-computer discovery.
- Original-vision acceptance: `e400d7065571271b5052fa6e58e9971814d331d6:openspec/changes/original-vision-journey-recovery/tasks.md` leaves broader tasks 4.1–4.3 open. Its 4.12 checkbox is stale: `275172ac97b65366a4a339359fc2cb60dfd754ba:openspec/changes/original-vision-journey-recovery/tasks.md` checks 4.12 with `evidence/storage-focused-resize-20261027.md`. The narrow selector therefore requires evidence reconciliation and final composition qualification, not assumed reimplementation.
- Rename/exclusive creation: `ca6326fba6611e2a3be242b9485a1d76d29cb2d0:openspec/changes/preserve-rename-and-creation-destinations/evidence/qualification.md` records implementation `f601998`, validated baseline `33a2cbe`, selected Debug/release-test 132/132 each, affected Analyze and ASan 3/3. Exactly the restricted reaper test was excluded. Interactive app journeys were NOT executed for this repair. Alias/reparse/source-identity races and cross-volume rename were not claimed.
- Reaper frontier: `797722734319ea18c65e4f04bbacdcf245b47316:openspec/changes/durable-cold-search-index-and-hybrid-residency/evidence/final-closure/acceptance-followup-20261025.md` already repairs launch-spelling assumptions by capturing actual process image/start. The naturally exiting-child identity test passed 8/8 with a wrong-launch-path negative control. The live termination fixture remains registered and unexecuted; its prior failure was not relabeled PASS. Do not restart this investigation from the older hypothesis.
- Existing simulation WORKSTATE retains cold-restore ColumnView UIA omission and preview “No selection” failures despite later passing runs. Its search-reopen probe is a bounded negative result (two 38/38 journeys), not in-panel snapshot-refresh qualification. Preserve both failures and later observations; no root cause is established by a retry.
- Restore/conflict evidence covers real Copy/Move apply-to-all and a controlled holding-folder Restore seam. It explicitly does not certify real multi-item Recycle Bin Undo. Explorer drag-in and broader native focus/role/physical-profile coverage remain separately unqualified.
- The existing simulation does not contain PR #36's final creation-collision test/source changes. It also contains composite harness/ownership edits that differ from individual incoming heads. Preserve full close/focus/search/storage/evidence assertions when resolving them; byte inequality alone is not a finding or permission to replace a file wholesale.

## External evidence at exploration

Run `40745953696` for PR #36 head `ca6326fba6611e2a3be242b9485a1d76d29cb2d0` had six failed jobs and zero executed steps. Check-run `123019663732` annotation explicitly said recent account payments failed or spending limit must be increased. This supports `HOSTED_CI=BLOCKED_EXTERNAL` and `HOSTED_CERTIFICATION=NOT_OBTAINED` for that historical run, not for a future uninspected SHA.

Both branch-protection and applicable branch-rules API reads returned HTTP 403 with the private-repository plan restriction. Required policy is UNKNOWN, not absent. No billing, policy, permissions, signing or trust-store change was attempted. The future candidate cannot bypass required checks or direct-push main to obtain integration.

## Planning outcome and limits

One umbrella acceptance capability is proposed. Existing feature requirements and historical failures are retained. Product/source gap analysis, the complete live ledger, final native acceptance, performance applicability, independent integrated reviews and certification remain apply work. Propose-time strict/lifecycle/drift checks validate planning consistency only, never product readiness.
