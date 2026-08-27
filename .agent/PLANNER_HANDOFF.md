# Universal Planner → Executor Handoff

This additive protocol preserves stricter repository rules. The planner writes one execution-ready major campaign to `.agent/EXECUTION_PROMPT.md` after auditing actual code/config/tests/docs, recent commits/diffs, useful issues/PRs, and native agent/state files. The prompt records Status, Planned-From, Planned-At, Target-Branch, mission, findings/rationale, preserved behavior, scope/out-of-scope, ordered workstreams, constraints, tests/integration validation, acceptance criteria, completion gate, Git requirements, and final reporting. Planning ends after committing/pushing the planning-only change.

## FastFiles 12-hour continuation override

When `.agent/NEXT_CAMPAIGN_12H.md` exists with `Status: ACTIVE`, read it **before** `.agent/EXECUTION_PROMPT.md`. It is the repository-level orchestration contract for the autonomous run. `.agent/EXECUTION_PROMPT.md` remains the detailed instructions for the currently active bounded OpenSpec campaign, but a COMPLETE/no-change result for that individual campaign does not end the 12-hour run. Control returns to `.agent/NEXT_CAMPAIGN_12H.md`, which requires a fresh live-tree audit, selection of the next evidence-backed frontier, creation of a new bounded OpenSpec, execution/validation, and repetition while the recorded 12-hour wall-clock budget remains.

The 12-hour override never authorizes speculative churn or weakening a gate. If an optimization premise is disproven, record/revert it truthfully and move to the next frontier. External-only signer/privileged/provider/clean-host/physical-display evidence remains external; do not fabricate it or mutate trust policy to keep the run active.

For `/goal continue` or equivalent: read repository instructions, this file, `.agent/NEXT_CAMPAIGN_12H.md` when ACTIVE, `.agent/EXECUTION_PROMPT.md` if present, the OpenSpec lifecycle and active change, and native campaign/state files; reconcile with current Git and implementation; resume from the first genuinely incomplete requirement, preserving existing behavior and avoiding unrelated rewrites; validate, repair introduced Critical/High regressions, update durable state, and commit/push per repository policy. If the 12-hour orchestrator is absent/inactive and no active planner prompt exists, fall back to native continuation semantics; if neither exists, report that a planner pass is required.


## Completion-oriented audit override — 2026-08-27

Current planner evidence is `docs/agent/DEEP_AUDIT_2026-08-27_FASTFILES.md`. The active orchestrator is `.agent/NEXT_CAMPAIGN_12H.md` planned from `57060c74e1d2416c8166201bbfd1834729ffd2da`. On resume, re-derive live HEAD and exact-SHA CI first. The current audited SHA has failed CI run `33033342573`; do not treat the prior indexed-search checkpoint as qualified until the stale lifecycle-status assertion, timing attribution, acceptance gate, full CTest/exact-tree, and hosted-CI requirements are resolved.
