## Context

See `proposal.md` for why this campaign exists and `evidence/explore-findings.md` for the source trace. The engine already drops paused scan batches and refuses a `ScanComplete` that arrives while that pause is still visible or while a failed-batch flag is still set. Resume of an incomplete scan clears that flag and starts another scan on the same connection-scoped `VolumeId`. Completion and failure payloads carry only that id. The privileged reader and the UI pause path are different threads.

An interrupted reconciliation stores a five-second floor by durable volume id, then clears the ephemeral retry queue. Reconnect of an already-indexed volume does not move that floor onto the new volume id. The scheduler wakes early only for the ephemeral queue. Its idle poll is ten minutes, and the ordinary reconciliation interval is six hours.

`StopVolumeScan`, connection teardown, and connection-scoped handles already exist. `openspec/changes/establish-architecture-foundation/design.md` D4 is the closed command surface: no client-supplied path, handle, or device string.

Hosted run `36302493823` on the current `main` HEAD failed all six jobs before any step because of billing. Branch protection and ruleset reads returned HTTP 403. Those facts are external constraints, not a design for a new CI system.

## Goals / Non-Goals

**Goals:**

- Make scan authority follow an attempt the engine can still order, including across pause, resume, replacement, and disconnect.
- Make an interrupted already-indexed reconciliation retry on the replacement connection at the five-second floor.
- Keep tab overflow, targeted-build identity, hosted merge, and the raw-volume bound inside the limits in the proposal.

**Non-Goals:**

- Choosing a wire protocol bump or a local generation counter as a proven fix.
- Redesigning raw-volume cancellation.
- Editing billing, protection, or the predecessor task checkboxes from this design.

## Decisions

### D1. Reproduce Finding A and Finding B before any fix

**Why:** The paused-reconciliation test never enters the resume path that clears the coverage hole, because that fixture is already scan-complete. The reissue-floor test covers an initial scan, not an already-indexed reconciliation. A green run of those tests does not show the reported orders.

**Alternatives:** Patch from the review text alone. Rejected. The source shows a dangerous callback order and a skipped floor, and it does not by itself prove the pipe timing.

**Trade-off:** If a deterministic test cannot produce the order, the campaign stops on a source-backed impossibility proof for that finding. No production edit lands on the strength of the proof's absence.

### D2. A VolumeId-only terminal does not become the replacement's authority

**Why:** `ScanComplete` and `ScanFailed` carry the connection-scoped volume id and nothing else. Resume currently clears the coverage-hole flag on that same session before the older terminal is applied. Incrementing a local counter at reissue does not label bytes already in the pipe, so the counter cannot tell the old terminal from the new one.

**Choice:** The engine retires the in-flight attempt before it grants a new attempt a clean authority slate. A completion or failure that arrives while retirement is unresolved, or that cannot be ordered after the replacement attempt, does not mark the volume complete, does not finalize a reconciliation pass, and does not erase the coverage hole. Disconnect continues to drop the old volume id with the session, so a terminal for that id stays non-authoritative.

**Alternatives:**

- Local counter only. Rejected. The wire id is unchanged.
- Resize `ScanComplete` to carry a generation. Rejected. Exact-length peers can misread it, and this design does not adopt a protocol bump.
- A new open-path or open-handle command. Rejected. Foundation D4.
- Always tear down the whole privileged connection on pause/resume. Not the default. It makes the old id die, and it also drops every other volume on that connection.

**Trade-off:** A legitimate completion is delayed until the engine knows it belongs to the current attempt. That is accepted. Ordinary pause/resume and a clean successful scan still complete once that ordering holds.

### D3. In-process retirement uses the closed command set first

**Why:** The service already supersedes a scan by stopping the previous worker before starting another, and a stop observed before the terminal write emits no terminal. The engine does not see that join. The first implementation has to close the engine-side hole without a new message: do not clear the coverage hole when starting a replacement, and do not accept a terminal as the replacement's until the previous attempt is retired.

**Alternatives:** Lead with an additive terminal message. Not selected. It is a fallback, not the design. If the reproducer from D1 shows that a volume-id-only terminal can still authorize the replacement after in-process retirement, stop and record that evidence before adding anything. The only legal extension is a new negotiated message. It must not be a longer `ScanComplete` or `ScanFailed`, and it must not carry a path or a handle.

**Trade-off:** The first patch might be insufficient if the pipe can still deliver an old success after the replacement is live. The combined regression in D5 is what keeps that from being called done. The fallback message is not pre-authorized.

### D4. Interrupted reconciliation is reattached by durable identity

**Why:** The five-second floor survives by durable volume id, and the reconciliation bit stored with it is never read. The already-indexed reconnect path does not consult the floor. Nothing wakes the scheduler for it, so the retry waits for the ten-minute poll and, for a quiet volume, the six-hour interval.

**Choice:** When the replacement enumeration binds the same durable volume, move that pending intent onto exactly one retry slot for the new ephemeral id. Keep the original due time. Wake the retry scheduler. Do not send `StartVolumeScan` from the enumeration path while the floor is unexpired. Do not address the previous connection's volume id. If the due time has already passed when the volume is recognized, issue one throttled retry through that slot rather than waiting for the ordinary interval, and do not immediately reconnect or rescan in a loop. Repeated failure replaces the same slot at the five-second cadence. The queue stays bounded by one slot per volume.

**Alternatives:** Shorten the ten-minute poll for every volume. Rejected. It wakes unrelated maintenance. Keep using the old ephemeral id after reconnect. Rejected. The service will reject it or, worse, confuse it with the new session. Full rescan on every reconnect. Rejected. The existing resume-or-reconcile rule stands.

**Trade-off:** A volume that returns after the floor has expired is retried once promptly instead of after six hours. A volume that returns inside the floor waits out the remainder. Pause, disable, disappearance, failed submit, reauthentication failure, and shutdown do not create a second pass or a stale send.

### D5. Findings A and B are one integration

**Why:** Both findings write the same session flags, the same retry map, and the same reconciliation pass. Separate patches can each look locally correct and still let a pause clear a hole that a disconnect then fails to retry.

**Choice:** One implementation owner for the shared session code. One combined regression: pause, resume, interrupted reconciliation, disconnect, reconnect, a stale terminal, and eventual recovery. Neither finding is resolved until that case passes.

**Trade-off:** The two reproducers stay independent so a failure is attributable. The fix does not land as two unsynchronized edits.

### D6. Many-tabs proof is three measurements

**Why:** The product creates one tab control per logical tab and hides the ones that do not fit. The visual action counts buttons whose ids fall in the tab-control range. Those numbers answer different questions. The inherited failure reported 4 against an expectation of 8. This session did not re-run it.

**Choice:** The scenario keeps the logical expectation of 8. It also records the visible count and whether the overflow affordance can activate a hidden tab. If the logical count is 8 and hidden tabs activate the right workspace, fix the harness. If the logical count is not 8, fix tab creation. Do not delete the overflow behavior, and do not treat a hidden control as a closed tab.

**Trade-off:** The scenario may fail for longer while the two causes are separated. That is preferred to a threshold edit.

### D7. Targeted-build identity is reproduced before the build files change

**Why:** Versioned executables already depend on the always-run identity target, and the resource file depends on the generated defines. That may already refresh a targeted build. It may not cover every binary that embeds a revision.

**Choice:** Configure at revision A, build the certification binaries, move the source identity to revision B, run the targeted commands local and hosted CI use, and read the embedded identity. The check must fail if it is looking at an artifact another target already refreshed. Change CMake only for a binary that stays stale. If none do, record the no-code closure. A `FASTFILES_BUILD_ID` value that is not 40 hexadecimal characters still fails the build. A well-formed override is embedded as given.

**Trade-off:** A passing full-tree rebuild is not evidence for this decision.

### D8. Hosted certification stays an external hold

**Why:** The live six-job run on this HEAD failed with empty steps and the billing annotation. Protection and ruleset APIs returned HTTP 403, so this design cannot see a required-check rule and must not invent one.

**Choice:** Recheck once at apply start and once after the candidate push. If jobs still cannot start, do not dispatch more of them, do not change billing or permissions, and do not merge to `main`. Local parity stays labeled local. If the jobs start, all six must pass on the executable candidate. A later documentation commit is not that candidate unless the executable tree is shown to be the same one that was qualified.

**Trade-off:** The branch can be reviewable and still not mergeable. That is the hold.

### D9. The raw-volume completion bound is unchanged

**Why:** Requesting `CancelSynchronousIo` does not bound how long a synchronous raw-volume `DeviceIoControl` takes to return. Predecessor task 2.3 and legacy-protocol task 5.4 exist to keep that gap open. Simulated stalls do not close it.

**Choice:** No new worker architecture in this campaign. The handoff states the bound as unproven. Those predecessor boxes stay unchecked.

**Trade-off:** A stuck raw-volume call can still stall join. A separate authorized campaign would need an elevated disposable environment before overlapped I/O, bounded admission, or a helper process is in play.

### D10. This change does not retire the predecessor

**Why:** The predecessor is still the active lifecycle entry because its completion-bound task is open on purpose.

**Choice:** Apply may register this change in `openspec/lifecycle.json` only in a way that leaves the predecessor's task 5.4 and residual-hardening task 2.3 unchecked and does not report the bound as proven. Do not mark the predecessor completed or superseded just to satisfy a single-active-campaign check.

**Trade-off:** The status model may report two open campaigns or reject the new directory until that registration is done carefully. That failure is not a reason to check the bound.

## Risks / Trade-offs

- [In-process retirement still accepts an old success after the replacement is live] → D1's reproducer plus D5's combined case stay red; D3's additive message is not started until that evidence exists.
- [Retry wake reconnects in a loop] → One slot per volume, the original five-second due time, and no scan send from enumeration while the floor is unexpired.
- [A fast successful replacement is delayed or dropped] → Success tests cover pause/resume and a clean scan whose terminal is ordered after the current attempt.
- [Incomplete reconciliation deletes rows] → Pass finalization stays tied to an authoritative terminal of that pass. Disconnect and a non-authoritative terminal abort the pass.
- [The harness is "fixed" by expecting 4 tabs] → D6 forbids that edit. Logical count and overflow activation are separate assertions.
- [A CMake change rebuilds the world and hides a stale targeted edge] → D7's sequence builds the specific targets after the identity moves.
- [Someone merges because protection cannot be read] → D8. HTTP 403 is not permission.
- [A docs commit is called the certified build] → The qualified executable SHA stays distinct unless tree equivalence is recorded.
- [The completion bound is checked off by the stall fixture] → D9. Those tasks stay open.

## Migration Plan

No index schema migration and no installer migration. The engine change is revertible by restoring the previous session-manager behavior. Rollback does not include rewriting volumes a stale terminal may already have marked complete; that repair is a new reconciliation once authority is fixed, not a one-time data migration in this design.

Deploy is the normal feature branch, review, and the hosted hold in D8. There is no flag to turn false completion back on.

Registering the change in the lifecycle manifest is an apply step under D10. This proposal does not edit that manifest.

## Open Questions

None that change the requirements, the decisions above, or the task breakdown. The reproducer's pass or fail chooses between "in-process retirement is enough" and "stop and bring evidence before any additive message". It does not reopen D2 or D3's rejection of a local counter, a resized completion, or an open-path command.
