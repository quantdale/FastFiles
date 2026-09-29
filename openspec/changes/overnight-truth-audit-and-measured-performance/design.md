## Context

See `proposal.md` for why this change exists. Live inspection at propose time found `main` and `origin/main` at `ea3d64f7c144343ec6faf849143d3f545e6fab36`, a clean primary worktree, PR #10 merged at that SHA, and hosted run `36588334920` failed with six jobs and zero steps. The billing annotation was: recent account payments have failed or the spending limit needs to be increased. The last recorded executable candidate, `6c40ab35793f1f9c03837f4b4dfa84b5964a032b`, is an ancestor, not the tip. `legacy-protocol-correctness-and-raw-volume-cancellation-closure` remains lifecycle-active because task 5.4 is still the unproven raw-volume bound.

## Goals / Non-Goals

**Goals:**

- Record contradictions in this change's evidence instead of inheriting them.
- Recertify only a clean implementation SHA whose embedded identity matches.
- Select performance work from current measurements, then keep or revert it from the same method.

**Non-Goals:**

- A second planning system, a protocol bump, or a new privileged command.
- Turning informational benches into wall-clock CTest gates.
- Closing task 5.4 or task 2.3 by supersession.

## Decisions

### D1: One change holds the campaign

**Why:** The prompt forbids a pile of speculative changes, and the project rule scopes a proposal to one capability. Successor fixes stay in this change unless a proven product-requirement break needs an added delta under the same change.

**Alternatives:** One change per audit lane. Rejected: that creates the speculative set the prompt forbids before any lane has a proven defect.

**Trade-off:** This change's task list grows only when evidence adds a successor. It does not pre-list unmeasured optimizations as required edits.

### D2: The truth ledger is change evidence

**Why:** `openspec/changes/<name>/evidence/` is the existing planning evidence location. `docs/agent/WORKSTATE.md` remains a resume checkpoint and is corrected only after a claim is classified. History is not rewritten to match the new classification.

**Alternatives:** A new top-level audit document. Rejected: the prompt says not to create a competing documentation system.

**Trade-off:** Readers must open this change for the ledger. The workstate checkpoint points there instead of duplicating the table.

### D3: The legacy raw-volume change stays active

**Why:** Task 5.4 is unchecked because no finite completion bound was proven. Superseding that change to keep a single active campaign would hide the residual. Schema v2 allows more than one `active` entry. This change is registered beside it and does not set `supersededBy`.

**Alternatives:** Supersede `legacy-protocol-correctness-and-raw-volume-cancellation-closure` and restate task 5.4 here. Rejected: the scan-generation campaign already refused that bookkeeping move, and this campaign does not own the proof.

**Trade-off:** Frontier reports show two active changes. The raw-volume one is external residual; this one is the local campaign.

### D4: Recertify before optimizing

**Why:** The prompt's first campaign is current-head reproduction. `ea3d64f` is not `6c40ab3`. Counts from the older candidate are historical until this tip, or a later implementation tip, is rebuilt and bound.

**Alternatives:** Start with search or startup optimization. Rejected: a speedup against an uncertified tip can hide a regression the old matrix no longer covers.

**Trade-off:** The first apply slice may be long and may find no product defect. That negative result is valid evidence.

### D5: Measure with existing tools before adding a harness

**Why:** Informational benches already exist and say they are not CTest gates. `windows-performance-baselines` records advisory run-envelope durations; its own description says regressions gate only when gate policy says so. Those tools are inventoried first. A new harness is added only for a user-visible path they do not measure, using disposable fixtures, warm-up, at least five measured repetitions, and median plus a tail. Debug timings are not product numbers unless the metric exists only there.

**Alternatives:** Register the large-index bench as a CTest pass/fail duration gate. Rejected: the bench comment and the noise of a wall-clock gate make that a flake factory.

**Trade-off:** Some paths stay unmeasured if a safe fixture cannot be built. The ledger says `UNPROVEN` rather than inventing a number.

### D6: Implementation is gated, and non-improvements revert

**Why:** The prompt allows implementation only after proof and forbids a falsified speedup. The acceptance bar is a meaningful user- or operator-visible change: roughly a double-digit percentage on an important metric, a perceptible absolute latency drop, or a proven correctness fix. An 8% improvement on a real hot path may stay. A large microbenchmark win on an irrelevant path does not.

**Alternatives:** Implement a predetermined startup or SQLite patch now. Rejected: no current-head profile in this session supports that patch.

**Trade-off:** The campaign can end with a truth ledger, a green recertification, and an honest "no safe local speedup" record. That is success, not a missing optimization.

### D7: Branch, pull request, and no self-merge

**Why:** The overnight prompt requires a dedicated branch, coherent commits, a non-forced push, and a reviewable pull request. It forbids merging that branch into `main` unattended. The branch `feat/overnight-truth-performance-hardening-20260929` was created from `ea3d64f` before this change was added. PR #10 is already merged; this decision does not unmerge it and does not treat that merge as hosted certification.

**Alternatives:** Commit the campaign directly to `main`, matching the generic session handoff. Rejected: the user-provided overnight prompt explicitly overrides that for this work.

**Trade-off:** `main` will not contain the campaign until a later human merge. Local and hosted identity must keep naming the branch tip separately from `origin/main`.

### D8: Hosted CI is observed, not repaired from here

**Why:** Run `36588334920` already classifies the current tip. Each later push is inspected once. A repeated zero-step payment or spending-limit failure stays `BLOCKED_EXTERNAL`. No billing, protection, signing, or trust-store mutation is in scope. The foundation design D4 closed command surface is unchanged unless a future proven defect requires a design amendment in this file first.

**Alternatives:** Manually rerun the failed workflow. Rejected: the jobs never started, and the prompt says not to relaunch a known-unstartable workflow.

**Trade-off:** The pull request cannot become hosted-green in this session if billing remains blocked. The report must say that in those words.

### D9: Identity order is implementation, rebuild, certify, then stop

**Why:** Executables embed source identity. The preferred end state is settled implementation, settled evidence, one commit, a rebuild from that tracked source, binding certification, and no further tracked edit. If a documentation-only commit is unavoidable afterward, the report proves the executable tree is unchanged and keeps the embedded SHA distinct from the documentation tip.

**Alternatives:** Certify a dirty tree and clean it up in a later commit. Rejected: that is the stale-identity failure earlier campaigns already repaired.

**Trade-off:** Evidence written after the certified commit cannot be silently amended into that same SHA. A later docs commit must say so.

## Risks / Trade-offs

- [Recertification fails for an environment reason] → Classify it. Do not edit product source unless the failure reproduces as a product or test defect.
- [A new harness measures noise] → Require warm-up, at least five repetitions, and median plus a tail. Do not promote one fastest run.
- [Two active lifecycle entries confuse the frontier] → The ledger states which active change is local work and which is the unproven raw-volume residual.
- [A reviewer suggests an unmeasured optimization] → Record it as speculative. Do not implement it to satisfy tone.
- [Apply starts to treat this design as permission to weaken security] → D8 and the spec reject that. No open-path command is authorized.

## Migration Plan

No protocol, schema, or installer migration. Rollback of a bad optimization is a revert on the overnight branch before the pull request is merged. The already-merged PR #10 is not migrated or reverted by this change.

## Open Questions

None that change the spec, the approach, or the task breakdown. The inventory task decides whether a harness is needed. The recertification task decides whether the first successor is a failure fix or the performance baseline.
