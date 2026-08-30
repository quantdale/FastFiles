# Universal Planner → Executor Handoff

## Current repository freeze

The current FastFiles repository is frozen after local truth convergence:
there is no active planner/execution campaign and no active-local task. The
default next action is **none — await a new reproducible P0/P1, a reported
regression, or authorized external production-certification inputs**. The
dated completion-integrity material below is historical evidence and must not
be resumed merely because its old prompt remains in the repository.

This additive protocol preserves stricter repository rules. The planner writes one execution-ready major campaign to `.agent/EXECUTION_PROMPT.md` after auditing actual code/config/tests/docs, recent commits/diffs, useful issues/PRs, and native agent/state files. The prompt records Status, Planned-From, Planned-At, Target-Branch, mission, findings/rationale, preserved behavior, scope/out-of-scope, ordered workstreams, constraints, tests/integration validation, acceptance criteria, completion gate, Git requirements, and final reporting. Planning ends after committing/pushing the planning-only change.

## FastFiles 12-hour continuation override

When `.agent/NEXT_CAMPAIGN_12H.md` exists with `Status: ACTIVE`, read it **before** `.agent/EXECUTION_PROMPT.md`. It is the repository-level orchestration contract for the autonomous run. `.agent/EXECUTION_PROMPT.md` remains the detailed instructions for the currently active bounded OpenSpec campaign, but a COMPLETE/no-change result for that individual campaign does not end the 12-hour run. Control returns to `.agent/NEXT_CAMPAIGN_12H.md`, which requires a fresh live-tree audit, selection of the next evidence-backed frontier, creation of a new bounded OpenSpec, execution/validation, and repetition while the recorded 12-hour wall-clock budget remains.

The 12-hour override never authorizes speculative churn or weakening a gate. If an optimization premise is disproven, record/revert it truthfully and move to the next frontier. External-only signer/privileged/provider/clean-host/physical-display evidence remains external; do not fabricate it or mutate trust policy to keep the run active.

For `/goal continue` or equivalent: read repository instructions, this file, `.agent/NEXT_CAMPAIGN_12H.md` when ACTIVE, `.agent/EXECUTION_PROMPT.md` if present, the OpenSpec lifecycle and active change, and native campaign/state files; reconcile with current Git and implementation; resume from the first genuinely incomplete requirement, preserving existing behavior and avoiding unrelated rewrites; validate, repair introduced Critical/High regressions, update durable state, and commit/push per repository policy. If the 12-hour orchestrator is absent/inactive and no active planner prompt exists, keep the repository frozen and take no implementation action unless a new reproducible P0/P1, regression, or authorized external certification input is present.

## Historical completion-integrity audit override — 2026-08-28

The historical planner evidence was `docs/agent/DEEP_AUDIT_2026-08-28_FASTFILES.md`.

Read `.agent/NEXT_CAMPAIGN_12H.md` first. The 2026-08-27 baseline failure is obsolete: audited SHA `933528ed29c2deac583587bedf3259b8ee617f5d` has hosted CI SUCCESS in run `33095061127`.

Green CI did not close the historical correctness frontier at that time. Exact source review found that `rule-pruning-durability-and-reconciliation-failure-closure` had:

- RebuildAll false-success callback/epoch behavior after secondary pruning failure;
- an invalid/relaxed reconciliation retention regression;
- deleted/prematurely completed task requirements;
- a fault injector without explicit named-boundary identity;
- unsupported shutdown/restart evidence.

The historical rule-pruning `tasks.md` was reopened to reflect those gaps and was subsequently completed.

After its exact implementation SHA was fully qualified and hosted-green, the queued OpenSpec was executed:

`openspec/changes/completion-integrity-hardening-and-release-certification/`

That campaign covers truth-surface hardening, release evidence retention, sanitizer policy reconciliation, fresh repository-wide correctness/security/concurrency/performance/UI audit, and final certification.

Do not reopen the completed indexed-search optimization without a new current-tree reproducer. Do not delete open requirements to make active-local count reach zero. Do not manufacture external signer/clean-host/elevated/physical-display evidence.
