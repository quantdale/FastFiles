## Context

FastFiles already contains most intended product functionality and a substantial assurance stack. The remaining risk is concentrated at boundaries where code, tests, lifecycle state, and release evidence disagree.

The exact audited SHA `933528ed29c2deac583587bedf3259b8ee617f5d` has green hosted CI run `33095061127`, including Debug/release-test 86/86, Analyze, exact-tree qualification, strict OpenSpec, policy/status gates, an ASAN 2-test slice, and unsigned candidate qualification. Green output is necessary but not sufficient because source review found false-green assertions and task-ledger drift inside the active rule-pruning change.

This change is intentionally dependency-aware: it coordinates post-fix hardening after the active correctness campaign closes, rather than creating two simultaneous product implementation frontiers.

## Goals / Non-Goals

### Goals

- Make project-completion claims depend on exact current-tree evidence.
- Finish all confirmed P1/P2 local blockers from the 2026-08-28 audit.
- Preserve a bounded, repair-first workflow suitable for a long autonomous session.
- Re-audit the live tree after each material repair rather than trusting stale findings.
- Keep validation representative: build success alone is not completion.
- Preserve release evidence in CI.
- Separate locally enforceable readiness from external certification.
- Stop when further work is speculative or genuinely low value.

### Non-Goals

- Broad redesign of working modules.
- Feature additions solely to make the campaign larger.
- Weakening tests/security/performance criteria to reach green.
- Turning external prerequisites into local PASS states.
- Keeping multiple invasive product changes active without dependency ordering.

## Decisions

### D1 — Correctness campaign closes before umbrella hardening

The active `rule-pruning-durability-and-reconciliation-failure-closure` change is the first dependency.

Before this umbrella change is considered execution-active:

- `RebuildAll` MUST not publish or callback a failed secondary pruning pass as a successful rebuild;
- reconciliation failure retention MUST be proven by a valid fixture with the key assertions enabled;
- named failure boundaries MUST be independently injectable/testable;
- MFT, USN, rebuild, reconciliation, repeated-failure, retry, shutdown/restart paths MUST have evidence appropriate to their semantics;
- deleted/unsupported task requirements MUST be restored and truthfully closed.

This avoids hiding active correctness repair inside a broad certification document.

### D2 — Exact-SHA evidence outranks historical checkboxes

A validation item is complete only when evidence names the exact committed implementation/handoff SHA or is demonstrably unchanged by later commits under the repository's provenance rules.

A checked task MUST NOT contain wording such as pending, in progress, relaxed, expected, assumed, or analogous evidence of an uncompleted gate.

If a still-required task is removed from a ledger, the removal MUST be accompanied by an explicit design/spec revision explaining why the requirement no longer applies. Deletion is not completion.

### D3 — Defect-derived regression tests are mandatory

Every material defect fixed during this campaign gets a deterministic regression test at the closest useful layer.

Tests MUST prove the broken invariant, not only exercise nearby code. A fixture that pre-removes the target state, comments out the decisive assertion, or reuses an unrelated test as proxy evidence does not satisfy the requirement.

### D4 — Release evidence must be durable and exact-SHA bound

The existing unsigned release candidate test currently writes to temporary directories. CI later attempts to upload `out/release-candidate/**/candidate-manifest.json` and `provenance.json`, producing no artifact.

The campaign MUST align producer and consumer paths. Safe candidate metadata/provenance needed for auditability is copied/emitted to a deterministic workflow-owned path and uploaded for the exact run.

Do not upload secrets, signing material, private keys, or unsafe host data.

### D5 — Sanitizer policy must have one authority

Current hosted CI runs two sanitizer tests while the repository contains an IPC listener lifecycle target and recent campaign evidence describes a three-test slice.

The executor MUST identify the authoritative requirement. If `ffipc_listener_lifecycle_tests` is sanitizer-compatible, include it. If not, document the technical reason and add/retain equivalent lifecycle memory-safety coverage rather than preserving contradictory 2/2 and 3/3 claims.

### D6 — Fresh audit order is risk-driven

After current known blockers close, audit in this order:

1. data integrity / mutation failure propagation;
2. security / privilege / IPC / trust;
3. concurrency / ownership / shutdown / cancellation;
4. memory/resource bounds and performance;
5. UI state machines, accessibility, responsiveness, major workflows;
6. build/CI/release evidence and false-green verification;
7. maintainability and documentation.

A confirmed Critical/High defect preempts lower-priority work.

### D7 — New product work is bounded

If the fresh audit finds missing intended core functionality, create a new bounded OpenSpec with exact reproducer, scope, acceptance criteria, and dependency placement. Do not turn this umbrella change into an implementation dumping ground.

If the feature already works and is sufficiently hardened, leave it alone.

### D8 — Local completion is distinct from production certification

Local completion can be declared only with all repository-enforceable gates green and no remaining evidence-backed local P0/P1 frontier.

External requirements remain explicit, for example:

- authorized production signer;
- approved production pin values;
- elevated signed raw MFT/USN runtime;
- clean-host install/upgrade/uninstall;
- provider/guest evidence where required;
- physical-display/foreground-owned UIA/rendered evidence where required.

If any remain unavailable, final state is **locally complete; external certification pending**.

## Execution Model

### Phase A — close current correctness blocker

Do not start broad hardening until the rule-pruning change is truthful.

### Phase B — assurance/control-plane convergence

Repair task/evidence truth, current-SHA references, release artifact retention, and sanitizer policy drift.

### Phase C — fresh repository-wide hardening audit

Re-enumerate the live tree, inspect changed paths since this audit, then systematically review current production/runtime/test/release paths by risk order. Historical findings are only hypotheses until reproduced.

### Phase D — bounded remediation cycles

For each confirmed material finding:

1. reproduce;
2. classify severity/user impact;
3. write/update bounded spec;
4. implement minimally;
5. add regression;
6. run focused validation;
7. run broad validation when stable;
8. commit/push coherent wave;
9. inspect exact pushed-SHA CI;
10. update truth surfaces.

### Phase E — release certification

Run the full final matrix on one committed tree, then push and inspect the exact hosted SHA. If the final documentation/evidence commit changes executable truth surfaces, re-run the relevant exact-tree/hosted gates.

## Constraints

- preserve C++20/Windows support matrix;
- preserve privileged/unprivileged process boundaries;
- preserve closed IPC command surfaces and least-privilege ACL/handle behavior;
- preserve fail-closed Authenticode/pin policy;
- preserve degraded mode;
- preserve deterministic search/order/cancellation/generation behavior;
- preserve SQLite durability and commit-before-projection ordering;
- preserve projection memory ceilings and cold read paths;
- preserve UI accessibility and keyboard semantics;
- no network dependency in ordinary product/search behavior;
- no force pushes or destructive repository cleanup;
- no disabled tests, commented decisive assertions, or temporary bypasses at completion.

## Validation Strategy

Validation is narrow-to-broad:

1. defect-specific unit/integration repros;
2. affected subsystem suites;
3. Debug full CTest;
4. release-test full CTest;
5. Analyze warning-clean;
6. authoritative ASAN slice;
7. strict OpenSpec;
8. source/diagnostic/doc/provenance/status/provider gates;
9. verifier doctor/build and exact-tree qualification;
10. unsigned release-candidate qualification with retained evidence;
11. representative performance sanity;
12. user-journey/UIA checks available in the environment;
13. hosted CI on the exact pushed SHA.

Tests passing does not close a finding if the test does not assert the intended failure path.

## Acceptance / Exit

This change can close only when:

- the dependency rule-pruning change is complete on exact-SHA evidence;
- all P1/P2 local findings in the audit are fixed, downgraded with evidence, or explicitly superseded by a stronger current design;
- no new P0/P1 local findings remain after a fresh audit;
- exact final tree passes the full local-enforceable validation matrix;
- hosted CI is green for that exact final SHA;
- release candidate metadata/provenance is retained as intended;
- lifecycle/tasks/WORKSTATE/progress/current prompts agree;
- any remaining release blockers are genuinely external and documented.

If these conditions are satisfied before the nominal twelve-hour budget, stop. If they are not satisfied at the budget boundary, leave a safe pushed checkpoint and one executable next frontier rather than claiming completion.
