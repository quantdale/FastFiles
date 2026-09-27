# Apply prompt — `legacy-protocol-correctness-and-raw-volume-cancellation-closure`

Use this prompt in a **new session** to implement the already-proposed OpenSpec change. Do **not** re-run OpenSpec explore or propose. Do **not** rewrite proposal/design/specs/tasks unless an artifact is invalid against the live tree.

---

You are the apply agent for FastFiles change **`legacy-protocol-correctness-and-raw-volume-cancellation-closure`**.

Follow **`.agent/skills/openspec-apply-change/SKILL.md`** (also `.claude/skills/openspec-apply-change/SKILL.md`). Invocation equivalent: **`/opsx:apply legacy-protocol-correctness-and-raw-volume-cancellation-closure`**.

## Change location

`openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure/`

Read, in order:

1. `AGENTS.md`
2. `docs/agent/WORKSTATE.md`
3. `openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure/proposal.md`
4. `design.md`
5. every `specs/**/spec.md`
6. `tasks.md` (source of truth for remaining work)
7. `evidence/explore-findings.md` (read-only explore baseline)
8. `openspec/lifecycle.json` entry for this change (`active`) and superseded `final-residual-hardening-and-hosted-ci-recovery`

Then:

```
openspec status --change "legacy-protocol-correctness-and-raw-volume-cancellation-closure" --json
openspec instructions apply --change "legacy-protocol-correctness-and-raw-volume-cancellation-closure" --json
```

Announce: `Using change: legacy-protocol-correctness-and-raw-volume-cancellation-closure`.

## Do not

- Re-explore the campaign or create a new OpenSpec change.
- Rewrite the plan unless validation shows an artifact is invalid.
- Skip tests, convert failures into skips, or treat local CI parity as hosted certification.
- Assume the legacy `ScanComplete`-on-failure defect without the task 2.x reproducer.
- Mark inherited residual-hardening **2.3** (or this change's **5.4**) complete because a cooperative simulated driver returned.
- Use `TerminateThread`, detach workers that still touch stack-owned state, or claim a cancellation **request** is completed I/O.
- Change GitHub billing, payment, or repository permissions.
- Provision production certificates/pins or run privileged MFT/USN certification unless the user explicitly authorizes that campaign.
- Merge substantial privileged-service changes directly to unprotected `main` while required hosted checks cannot run.

## Invariants (must remain true)

1. A scan that did not complete must never be represented as successfully completed merely because the peer cannot understand `ScanFailed`.
2. A cancellation request is not completed I/O.
3. Local CI parity is not hosted certification.

## How to work

- Create/use feature branch `feat/legacy-protocol-correctness-and-raw-volume-cancellation-closure` (design D6).
- Implement `tasks.md` in order. Investigation/reproducer tasks (2.x, 4.x) before corrections (3.x, 5.x).
- Check off `- [ ]` → `- [x]` in `tasks.md` immediately after each completed task, with a brief implementing-symbol note.
- Pause on blockers; do not guess security or lifecycle behavior.
- Run focused tests after each implementation slice, then the ladder in section 7. Do not skip tests.
- If hosted Actions is still billing-blocked, record `HOSTED_CI = BLOCKED_EXTERNAL` once and leave a reviewable PR; do not repeatedly dispatch known-unstartable workflows.
- Independent reviewers A–E are tasks 8.1–8.5 after implementation stabilizes.
- Handoff is task 9.3 (report sections A–J).

## Validation commands (do not skip)

```powershell
cmake --build --preset debug --target <focused-test>
ctest --test-dir build/debug -R <name> --output-on-failure
npx --yes --package=@fission-ai/openspec@1.8.0 -- openspec validate --all --strict --no-interactive
# after slices stabilize:
ctest --preset debug
```

Use `verify/Invoke-OpenSpecStrict.ps1` when matching hosted/local parity. Full ladder is tasks 7.1–7.4.

## Already done (do not redo)

Propose-session produced apply-ready artifacts (proposal, six specs, design, tasks), registered this change as the sole `active` campaign, and superseded `final-residual-hardening-and-hosted-ci-recovery` while leaving predecessor tasks 2.3 / 5.5 / 6.1 / 6.2 / 6.6 **unchecked** and restated in this `tasks.md`. Explore findings are in `evidence/explore-findings.md`.
