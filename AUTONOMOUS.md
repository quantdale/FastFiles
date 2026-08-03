# FastFiles Autonomous Engineering (AUTONOMOUS)

This document is the **one entry point, resume protocol, and future-agent intake contract**
for FastFiles' autonomous engineering loop. It is the machine- and agent-readable companion
to `verify/autonomous/contract.json` (the machine contract) and the `AGENTS.md` section of
the same name.

## Purpose

FastFiles keeps a strict, evidence-gated verification harness (`verify/`) and a set of
OpenSpec *changes* with `tasks.md` files that are the source of truth for what is built and
what is deferred. The autonomous loop (`verify/intake.ps1`) drives a full lifecycle non-
interactively, persists its state so it can be resumed across sessions, classifies failures,
bounds retries, and never closes a task on narrative proof alone.

## The one entry point

```
pwsh ./verify/intake.ps1 autonomous [-RunId <id>] [-Fresh] [-Provider <id>] \
    [-Configuration debug,release] [-MaxIterations <n>] [-MaxRetries <n>] \
    [-TimeoutSeconds <n>] [-ImplementScript <path>] [-AllowPush] [-SkipCommit]
```

Prefer the `intake.ps1` entry point over composing `verify.ps1` verbs by hand. The loop
understands the phase ordering, resume, and classification; raw `verify.ps1` calls do not.

Companion verbs:

```
pwsh ./verify/intake.ps1 status [-RunId <id>]      # machine-readable status (JSON)
pwsh ./verify/intake.ps1 archive-gate [-RunId <id>] # re-resolve the terminal archive gate
```

## Exit codes (deterministic)

| Code | Meaning |
| --- | --- |
| 0 | PASS |
| 1 | FAIL |
| 2 | SKIPPED |
| 3 | HARNESS ERROR |
| 10 | NOT-YET-IMPLEMENTED |

## Run state file

```
verify/runs/autonomous/<run-id>/state.json
```

A run is a full 15-phase lifecycle. The state file records the current phase, completed and
incomplete step ids, per-task evidence references, failure classification, bounded retry
counts, external blockers, and the authoritative open-task count. On a crash or interruption
the loop is resumed at the first incomplete phase on the next `autonomous` invocation.

`<run-id>` is `yyyyMMdd-HHmmss`. The schema is `verify/autonomous/schemas/run-state.schema.json`
and is validated on every save.

## The 15 run phases

`discover -> plan -> provision -> implement -> build -> test -> diagnose -> repair ->
re-test -> validate -> collect-evidence -> update-tasks -> commit -> sync -> archive`

| Phase | Meaning |
| --- | --- |
| discover | fingerprint environment, toolchain, install media, open-task inventory |
| plan | build the worklist: capabilities, provider, configurations, implement hook |
| provision | provider lifecycle `provision` + `activate`; record external blockers |
| implement | apply product edits via `-ImplementScript`; else verify tree consistency |
| build | `cmake --preset <cfg>` + `cmake --build` for each configuration |
| test | `ctest --test-dir build/<cfg>` full suite |
| diagnose | classify the latest failure (Class A / Class B / external) |
| repair | apply Class A fixes automatically; surface Class B for review |
| re-test | re-run the failed build/test step after a repair |
| validate | run the harness `verify.ps1 run` + `gate` and resolve the four-state archive gate |
| collect-evidence | copy logs/reports into `artifacts/` and write `evidence.json` |
| update-tasks | close tasks only for PASS evidence produced by this run (7.6) |
| commit | stage and commit the orchestrator's own tracked-source delta |
| sync | push only when an authenticated remote is available *and* `-AllowPush` |
| archive | resolve the terminal four-state archive gate and write `ARCHIVED.json` |

## Failure classification and bounded retries

- **Class A** — harness / config / environment. Auto-fixable by the orchestrator (e.g. clean a
  stale build dir, rebuild the toolchain env). Applied automatically.
- **Class B** — product source. Surfaced with a recorded root cause, message, and diagnostics
  reference (`repair-surfaced-classb.json`) for *human review*; never auto-accepted.
- **external** — physical / legal / commercial / secret impossibility. Recorded with machine
  evidence (`externalBlockers`), every unaffected task is completed, the full pluggable path is
  implemented, and affected tasks stay `REQUIRED-BUT-UNAVAILABLE`. Never fabricated.

Retries are bounded (≤ 3). A **recurring normalized failure signature** stops the loop and
escalates with captured diagnostics rather than retrying the same action unchanged.

## The 13-step future-agent intake sequence

A new agent session that needs to make a change should follow these steps. Steps 1–10 are the
agent's work; the orchestrator automates the mechanical verification, evidence, and commit
phases on top.

1. **Inspect persisted run state** — `status` verb; decide resume vs. new run.
2. **Convert the request into an OpenSpec change** — `proposal.md` / `design.md` / specs /
   `tasks.md` under `openspec/changes/<name>/`.
3. **Implement the product edit** — per the change's `tasks.md`; keep `src/` scoped, warning-clean.
4. **Provision the environment provider** — `local` by default; a disposable VM when the
   provider and media are available.
5. **Generate/update tests** — the change's tests, registered with CTest in the component's
   `CMakeLists.txt`.
6. **Run focused verification** — `ctest --preset debug -R <component>`.
7. **Run full verification** — full `ctest --preset debug` + the archive gate.
8. **Diagnose failures and classify** — Class A / Class B / external.
9. **Repair** — Class A auto-applied; Class B surfaced with root cause + diff for review.
10. **Update evidence and task checkboxes** — only with recorded verification (never narrative).
11. **Commit** with evidence references.
12. **Push** when an authenticated remote is available and the operator opted in.
13. **Sync/archive and leave resumable state** if interrupted.

## Dependency graph

```
discover → plan → provision → implement → build → test →(fail)→ diagnose → repair → re-test → validate
                                                          └→(pass)→ validate
validate → collect-evidence → update-tasks → commit → sync → archive
```

## Flaky-test policy

On any intermittent failure the loop preserves the initial output, reproduces under stress,
captures timing / resource / thread / process / environment data, and classifies the outcome
(`verify/core/FlakyTestPolicy.psm1`). A "passed on retry" is **never** the terminal state
without a root-cause fix or a documented non-determinism bound.

## Scope and guardrails

- This loop is for **verification, evidence, and documentation** automation. It never silently
  accepts a Class B fix; product code changes always go through human review.
- Evidence under `verify/runs/`, `verify/baselines/`, and `verify/.signing/` is gitignored by
  design; the `commit` phase only stages tracked source the loop itself wrote.
- `sync` (push) is opt-in via `-AllowPush`; without it the phase records
  `push-not-opted-in` and completes honestly.