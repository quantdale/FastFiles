# Owner handoff — final-product-consolidation-and-integrated-acceptance (task 12.6)

Published as the body of the single consolidation PR. Live publication facts
(merge SHA, PR numbers, hosted run) are derived from Git/GitHub at publication
and appended here/PR-side without embedding self-referential claims in tracked
state. Status vocabulary: PASS / QUALIFIED / UNQUALIFIED / NOT_OBTAINED /
NOT_READY.

## The 20 named success states (design D11)

| # | State | Status | Evidence / scope | Owner action |
| --- | --- | --- | --- | --- |
| 1 | `LATEST_REMOTE_RECONCILED` | PASS | 12.1 refetch: main `0dfd4d5` and all 23 input heads unchanged since intake; zero dirty worktrees | none |
| 2 | `ALL_OPEN_PRS_ACCOUNTED_FOR` | PASS | `evidence/integration-ledger.json` (every PR: unique content/tests + disposition) + `local-input-reconciliation.md` | none |
| 3 | `INTEGRATION_CONFLICTS_RESOLVED` | PASS | all merges resolved (doc/registration unions + 1 product union); lanes A/B + focused recheck `reconciliation-and-review.md` | none |
| 4 | `NO_ACCEPTED_FIX_LOST` | PASS | content-level reconciliation; lane B spot-verified claims; only the disproven flat-prior guard excluded (documented) | none |
| 5 | `FILE_OPERATION_DATA_SAFETY_VERIFIED` | PASS | journey F cells + lane A invariant audit (no-overwrite, per-request consent, generated-result protection, undo safety) | none |
| 6 | `SEARCH_CORRECTNESS_VERIFIED` | PASS | journey B cells + reopen/snapshot probes + canonical equality families in 144/144 | none |
| 7 | `COLUMN_VIEW_JOURNEYS_VERIFIED` | PASS | journey C/D cells + UIA keyboard/dual-pane 25/25 | none |
| 8 | `STORAGE_INTELLIGENCE_VERIFIED` | PASS (scoped) | journey E cells; known/pinned coverage; no whole-volume overclaim | none |
| 9 | `ORIGINAL_VISION_ALIGNED` | PASS | `five-pillar-alignment.md`; degraded browsed/pinned coverage never claimed as whole-computer discovery | none |
| 10 | `REAL_NATIVE_E2E_VERIFIED` | PASS | E2E 8/0/0 on the real executable, filesystem oracles, safe cleanup | none |
| 11 | `UIA_AND_ACCESSIBILITY_VERIFIED` | PASS (host scope) | UIA core 25/25, rapid-input, lifecycle; synthetic DPI; physical High Contrast/reduced-motion profiles NOT exercised | physical-profile run needs a physical host |
| 12 | `EXPLORER_INTEROPERABILITY_QUALIFIED` | UNQUALIFIED | journey I cells open; bounded non-delivery diagnosis chain preserved (`native-acceptance-report.md`); cause class open (app-side registration NOT excluded); never auto-labeled BLOCKED_ENVIRONMENT | second interactive host / product-side drop instrumentation to resolve the cause class |
| 13 | `VISUAL_COHERENCE_VERIFIED` | PASS (current profile) | visual-matrix 350 assertions/0 failed; light/dark; actual captures reviewed (search workspace, column reveal — no material defects) | untested physical profiles listed under #11 |
| 14 | `CRITICAL_PERFORMANCE_PRESERVED` | PASS | 200k rebuild cycles 783–902ms vs certified 823–872ms (noise band); payload identity identical; historical large-scale evidence applicable by source-equivalence | none |
| 15 | `INDEPENDENT_REVIEWS_COMPLETE` | PASS | two fresh-context lanes (OK with notes) + focused recheck OK; both P2s fixed (one red-first) | none |
| 16 | `OPENSPEC_RECONCILED` | PASS | evidence-tagged task closure; lifecycle PASS; strict OpenSpec 106/106; raw-unchecked tasks have reviewed external overrides | none |
| 17 | `LOCAL_CERTIFICATION_COMPLETE` | QUALIFIED | every mandatory local gate executed + passed (144/144 Debug + release-test, Analyze 0w, ASan 4/4, E2E/UIA/visual, policy gates) with the frozen-policy exclusions named: restricted live-reaper fixture (registered, unexecuted) and privileged-trust label (opt-in). If the owner rules the reaper a required cell, this state degrades to unqualified until a separately permitted environment runs it | optional: authorize a separately permitted reaper run |
| 18 | `HOSTED_CERTIFICATION_COMPLETE` | NOT_OBTAINED | `HOSTED_CI=BLOCKED_EXTERNAL` (pre-step payment/spending failure; six jobs, zero steps — one inspection of the pushed SHA performed at 12.3; result recorded PR-side) | restore GitHub Actions billing, then one re-inspection of the main tip |
| 19 | `MAIN_INTEGRATION_COMPLETE` | PASS at this PR's merge | the terminal act of this campaign; verified post-merge and recorded in the follow-on reconciliation commit (ordinary non-force PR merge; no bypass) | none |
| 20 | `PRODUCTION_RELEASE_READY` | NOT_READY | requires production signer/pins (Release stays fail-closed without them), clean-host install, elevated MFT/USN validation, physical-display matrix, and explicit owner release authorization — all external to this campaign; Explorer interop owned by #12, not deferred here | owner: provision signing + authorize release qualification |

## Daily-use assessment

The integrated candidate is a coherent daily-driver file manager for ordinary
unprivileged use: first launch to Home, degraded-mode browsing with truthful
coverage copy, Fast Search reveal with exact identity preservation, Column
View/tabs/dual-pane navigation, Storage intelligence over known coverage, and
full file operations with consent-scoped conflict handling — all exercised on
the real executable with filesystem oracles. Known limits for daily use:
Explorer drag-in/out interop is unqualified on this host (cause class open),
real Recycle Bin multi-item Undo is a distinct unqualified journey (holding-folder
Restore works), and 2M+ row cold search serves from the durable cold stream
under the default memory budget (documented regime, bounded).

## Publication and terminal disposition

- One consolidation PR from `integration/fastfiles-final-product-20261029`;
  ordinary non-force merge; no admin/bypass; no branch deletions.
- All 23 original PRs land with their commits reachable from main and are
  reconciled as MERGED (their content is in main via this merge; per-PR unique
  content attribution lives in the ledger). No PR is described as merged whose
  commits are not in main.
- Publication tasks (12.3–12.6) close in the post-merge follow-on
  reconciliation record; the lifecycle entry moves to `completed` there with
  journey-I external overrides (REQUIRED-BUT-UNAVAILABLE).
