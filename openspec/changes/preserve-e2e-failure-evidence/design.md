## Context
A combined native run on `b0cf4bb` failed cold restore's UIA marker check. Its inner cleanup captured a live failure envelope, image and tree; the no-context outer wrapper subsequently overwrote the envelope with a generic no-window note. The screenshot visibly showed the marker while the UIA tree omitted the custom Column View. This change repairs evidence ownership and does not determine the UIA failure's cause.

## Goals / Non-Goals
Preserve current invocation evidence and accurately record subsequent wrapper failures. Reject stale evidence without deleting it. Do not alter scenario success criteria, fabricate screenshots, classify the UIA failure or change native app behavior.

## Decisions

### D1 — Fresh artifact roots and scenario directories
The CLI and direct runner accept new or existing empty artifact roots and reject non-empty or non-directory roots before any scenario runs. The wrapper reserves its scenario directory before invoking the body and rejects existing paths. Why: an existence check after a body would otherwise trust stale evidence from a previous run. A fresh per-scenario directory also keeps direct wrapper calls safe. Trade-off: rerunning with the same populated artifact path now fails explicitly; callers must choose a fresh root.

### D2 — Preserve inner observations and record outer context separately
If the body wrote `failure.json` during this invocation, retain it and its adjacent tree/image; write an outer boundary envelope using `failure-outer.json`, or a unique suffix if that name is already occupied. The same rule applies to returned failures and exceptions, with or without a mock/live context. If no inner envelope exists, write the normal outer `failure.json`. Why: overwriting a live observation with a generic no-window note makes the evidence internally contradictory. The outer note describes only its own lack of an additional capture.

### D3 — Require an explicit scenario result
All current scenario implementations return `New-E2EResult`; none intentionally use a raw/null value to indicate success. Validate that the body emits exactly one result with the expected id, required fields, a non-empty stage and a known PASS/FAIL/SKIPPED status. Invalid output becomes an `invalid-result` failure with evidence, preserving any inner envelope. Explicit PASS and SKIPPED stay unchanged. Why: the old fallback manufactured PASS for null or unsupported output, and an unknown status could evade all aggregate counters.

### D4 — Exercise the actual runner and wrapper without GUI
Headless tests execute the real wrapper and real direct-runner function, use owned marked fixture roots, and execute a copy of the actual CLI runner against a fake gate and no-launch stub to prove existing-empty-root handoff. Sentinel files are not screenshots and do not establish visual behavior. A failed scenario remains `FAIL`; a rejected stale path does not cause the body to run or present stale artifacts as current.

## Risks / Trade-offs
- Source fixtures and result files can still be modified by unrelated actors with access to the same writable directory after reservation; this is not a security boundary against a malicious local user.
- Existing automation that intentionally reuses a populated artifacts root must choose a fresh root; old files remain untouched.
- No actual GUI scenario was rerun on this branch. The prior UIA failure remains open and requires separate diagnosis.

## Migration Plan
No persistent data migration. Publish as a reviewable successor to PR31 after integrator review and combined qualification; no automatic merge or production release.
