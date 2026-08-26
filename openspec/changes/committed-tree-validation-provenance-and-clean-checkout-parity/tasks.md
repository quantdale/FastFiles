# Tasks

## 1. Baseline and flow audit

- [x] 1.1 Pin the planner baseline, previous exact hosted result, lifecycle frontier, and historical false-green incident.
- [x] 1.2 Inventory direct CTest, verifier, status, release-frontier, CI, OpenSpec, and handoff validation-to-evidence paths.
- [x] 1.3 Record the existing run/lifecycle/hosted identities and identify the missing local source-state binding.

## 2. Repository-state provenance

- [x] 2.1 Add one shared Git/source-state module with full HEAD and commit-tree identities.
- [x] 2.2 Classify staged, unstaged, deleted, and non-ignored untracked source inputs without reading ignored verifier/build state.
- [x] 2.3 Add deterministic source-state digests, start/finish mutation detection, and path-safe reason codes.
- [x] 2.4 Define development vs exact-tree qualification without destructive reset/clean/stash behavior.

## 3. Exact committed-tree qualification

- [x] 3.1 Materialize the exact candidate commit in a unique detached local Git worktree.
- [x] 3.2 Run repository-sensitive source/diagnostic/doc/OpenSpec/status/release-frontier gates in that committed tree.
- [x] 3.3 Verify start/finish source identity and emit run-result-compatible exact-tree provenance.
- [x] 3.4 Remove only the exact temporary worktree proven to be owned by the current run.

## 4. Stale-result and consumer hardening

- [x] 4.1 Re-bind explicitly selected local PASS evidence to the current full HEAD/tree before status qualification.
- [x] 4.2 Classify legacy or dirty PASS evidence as development-only rather than exact-tree qualified.
- [x] 4.3 Fail closed on SHA/tree mismatch or source mutation when release-frontier evidence depends on the selected run.
- [x] 4.4 Keep status/release generation offline and preserve lifecycle/external blocker semantics.

## 5. Adversarial regression matrix

- [x] 5.1 Add hermetic Git fixtures for clean, staged, unstaged, deletion, untracked, and ignored-output states.
- [x] 5.2 Prove stale result replay and commit advance are rejected.
- [x] 5.3 Prove source mutation during qualification invalidates the result.
- [x] 5.4 Cover detached HEAD, missing Git metadata, deterministic reason ordering, and path redaction.
- [x] 5.5 Reproduce the historical dirty-correction/committed-stale false-green class and prove it is non-qualifying.
- [x] 5.6 Register the provenance fixture suite in CTest.

## 6. Guidance and evidence

- [x] 6.1 Document exact-tree qualification, development-only semantics, Git identities, and non-destructive worktree ownership.
- [x] 6.2 Record validation-flow inventory, provenance authority map, false-green reproduction, and fixture matrix.
- [x] 6.3 Preserve hosted CI exact-SHA evidence and all production signer/privileged/provider/clean-host/interactive boundaries.

## 7. Qualification integration

- [x] 7.1 Ensure the hosted Debug/release-test suites execute the provenance fixtures through normal CTest registration.
- [x] 7.2 Provide an exact-tree qualifier whose output can be explicitly selected by project-status and release-frontier consumers.
- [x] 7.3 Keep final hosted-run and implementation-SHA facts in evidence/handoff records derived from actual GitHub results rather than hard-coded task prose.
