# Runtime certification baseline and root-cause trace

## Reconciled starting state

- Branch: `main`
- Starting HEAD and `origin/main`: `90d012ebfbcd7530f5b397528dc768de34a7396f`
- Worktree: clean before this change
- Hosted CI for that exact SHA: GitHub Actions run `32812481618`, completed
  `success`
- The repository was not switched to an implementation branch. Draft PR #6 is
  still open, but its current diff is superseded by mainline history; public
  repository access did not provide authenticated administrative write access.

## Reproduced observations

### Historical contradiction retained as evidence

The existing ship-ready evidence records a complete exported/live UIA result of
23 PASS, 6 truthful SKIP, and 0 FAIL, while three supported invocations of
`verify.ps1 run -Capability windows-ui-automation-validation` (including
`UiaScenario=all`) reported `status=FAIL` with an opaque `scenario-failed` and
no child scenario in the console. That record is preserved in
`openspec/changes/ship-ready-ui-ux-hardening-convergence/evidence/audit.md` and
is not rewritten here.

The current source trace proves that these observations cannot yet be treated
as one authoritative result:

1. `verify/verify.ps1` resolves the capability, runs the local provider, calls
   `Invoke-Capability`, prints only the capability envelope status/reason, then
   derives the process exit code from the surviving envelope list.
2. `verify/core/CapabilityRunner.psm1` invokes the capability directly in the
   parent PowerShell pipeline. It does not catch a capability exception, reject
   zero or multiple pipeline outputs, enforce a timeout, or attach cleanup
   state to the envelope.
3. `Invoke-UiAutomationValidationCapability` initializes
   `overallReason = 'scenario-failed'`, appends mutable child records, and
   clears that reason only after counting the final child list. A zero-failure
   child list therefore returns a null reason from this function; the parent
   cannot legitimately turn that same returned object back into
   `scenario-failed`.
4. The UIA function can append cleanup failures in `finally`, and the parent
   provider `finally` can throw before reports and the final result are
   persisted. Those paths are capable of producing a different result or no
   result, but the pre-change harness does not preserve which one occurred.

The historical export and wrapper output are therefore classified as an
**unresolved verifier-composition discrepancy**, not as proof of a product
failure or as proof of a product pass. The missing identity, child-state
validation, pipeline-cardinality check, and failure precedence are the local
hardening targets.

### Post-hardening terminal evidence

After the worker/result-envelope slice was implemented, a clean core selection
returned a terminal result rather than `scenario-failed`:

```text
status=FAIL classification=ASSERTION_FAILURE reason=column-items-unexpected
summary=2 scenario(s) failed: column-populated-and-selected-item-in-tree,
mouse-tab-operation; first=column-items-unexpected: column item scenario failed:
Exception calling FindAll ... Unrecognized error.
```

This is a truthful child failure, not a wrapper contradiction. The column
scenario was then isolated and passed after replacing its recursive `FindAll`
over the dynamic column fragment with live-parent/direct-child discovery and a
bounded readiness wait. The corresponding isolated mouse run first exposed a
second harness boundary: the worker exited abnormally with PowerShell native
resource errors (`Out of memory`, then `Releasing the double mapped memory
failed`) before it could emit a candidate result. A minimal nested PowerShell
launch subsequently exceeded 40 seconds on the same host, while the host was
under sustained commit/pagefile pressure from unrelated long-lived QEMU and
IDE/agent processes. That evidence is classified as **environmental resource
unavailability / harness abnormal-exit evidence**, not as a product assertion
and not as a truthful mouse PASS.

The UI/engine/worker exact-process checks after those runs showed no remaining
repository-build UI or engine process. The harness now records the abnormal
worker as `HARNESS_ERROR` with bounded stdout/stderr diagnostics, kills only
the exact worker and repository-build identities it owns, and refuses to adopt
pre-existing repository-build UI/engine processes for capabilities declaring a
clean baseline. The registry propagation for that baseline declaration is
covered by a non-live contract check.

The later clean core probe reached an attributable terminal result instead of
the historical wrapper string:

```text
status=FAIL classification=ASSERTION_FAILURE reason=column-items-unexpected
summary=2 scenario(s) failed: column-populated-and-selected-item-in-tree,
mouse-tab-operation
```

The column assertion was isolated and passed after the driver stopped using a
recursive dynamic-fragment `FindAll` and instead reacquired the live pane and
enumerated bounded direct list children. The remaining focused mouse/worker
probes then hit native PowerShell resource failures (`Out of memory`,
double-mapped-memory release, and pagefile/assembly-load errors); a minimal
nested `pwsh` launch exceeded 40 seconds under the same host pressure. No
FastFiles UI/engine/worker residue remained after exact cleanup. This is
recorded as an environmental worker-start/resource boundary, not converted to
a product PASS or a product assertion.

The verifier-core result suite now proves the opaque zero-child case as a
canonical `HARNESS_ERROR` with reason
`capability-failed-without-attributed-cause`; the raw declared `scenario-failed`
value is retained only as `declaredReason`/bounded diagnostic text. Its
worker-boundary fixtures are executed when the host can launch a bounded nested
PowerShell worker. On the current pressured host they terminate as an explicit
test prerequisite skip after the pass fixture produces no worker diagnostics;
the pure composition, identity, and cross-run checks remain green. The UIA
selection/order suite and project-status composition suite pass, and strict
OpenSpec validation remains green.

### Current full-wrapper reproduction

Command:

```text
pwsh -NoProfile -File ./verify/verify.ps1 run -Capability windows-ui-automation-validation
```

The invocation resolved the diagnostic UIA capability and launched the
repository-build UI and engine, but it exceeded a bounded observation window
without printing a terminal capability result or returning an exit code. Before
termination, exact process identity was checked by executable path, PID, and
creation time:

| Owner | PID | Executable | Creation time (local WMI representation) |
|---|---:|---|---|
| wrapper PowerShell | 43068 | `pwsh.exe` running the supported command | `20260825141242.817978+480` |
| UI | 31084 | `build/debug-diagnostic/src/ui/FastFiles.exe` | `20260825141302.971462+480` |
| engine | 56456 | `build/debug-diagnostic/src/engine/FastFilesEngine.exe` | `20260825141304.622683+480` |

The separately installed `FastFilesIndexSvc.exe` was observed and was not
targeted. The wrapper, UI, and engine were then terminated by those exact PIDs;
the post-check showed only the separately installed service remained. This is
evidence of an **unbounded verifier execution/lifecycle boundary**. It is not
evidence of a product assertion failure because no authoritative child result
was returned.

### Focused comparison

Command:

```text
pwsh -NoProfile -File ./verify/verify.ps1 run -Capability windows-ui-automation-validation -CapabilityOption UiaScenario=selection-model-interactions -Format human
```

After exact cleanup of the interrupted full invocation, the focused wrapper
returned exit code 0 and `status=PASS`. This demonstrates that the wrapper can
complete a bounded selection slice on the same host; it does not certify the
complete mixed shared/isolated capability. The existing UIA evidence continues
to classify foreground ownership and Explorer drag-source limitations as
truthful environment skips.

## Classification at the baseline

| Observation | Classification | Evidence boundary |
|---|---|---|
| Historical 23/6/0 export versus wrapper `scenario-failed` | verifier composition / evidence identity unresolved | The two paths did not share a validated invocation envelope or child-state ledger |
| Current full wrapper never reached a terminal result | harness defect: missing bounded execution | Exact wrapper/UI/engine identities remained alive until manually terminated |
| Focused selection wrapper PASS | local capability slice PASS with existing environment skips | Exit 0 and status PASS; complete capability not inferred |
| Focused column-item wrapper after direct-child hardening | local harness slice PASS | The live dynamic fragment no longer uses a recursive `FindAll` for this assertion |
| Focused mouse worker native OOM/double-mapped-memory exit | environment resource pressure surfaced as harness abnormal exit | No child result was emitted; no mouse PASS is claimed |
| Foreign foreground, Explorer synthetic drag, privileged service/signing/clean-host/guest/display prerequisites | external or environment boundary | Existing stable reason codes and repository policy; no security weakening permitted |

## Required implementation consequence

The campaign must make the parent own one invocation identity and one
authoritative result channel. A capability that throws, stalls, emits zero or
multiple result objects, returns a cross-run artifact, or fails cleanup must
produce an explicit attributed terminal result. Reports and exit codes must be
derived from that validated envelope, not from console text, a mutable counter,
or the latest artifact directory.

## Continuation validation ladder

The continuation session reconfigured and built through the discovered Visual
Studio 2022 BuildTools x64 developer environment. The current local gates are:

| Gate | Result | Evidence |
|---|---|---|
| Debug configure/build | PASS | `cmake --preset debug` and `cmake --build --preset debug` completed; the verifier target was registered |
| Debug CTest | PASS | 83/83 tests passed in 304.63s |
| RelWithDebInfo release-test configure/build | PASS | `cmake --preset release-test` and `cmake --build --preset release-test` completed |
| First release-test CTest | NOT GREEN | 82/83 passed; `ffengine_client_handshake_tests` hit the 120s timeout while the other tests passed |
| Release timeout diagnosis | ENVIRONMENT BOUNDARY | The handshake binary passed alone in 0.40s and in the immediate UI-server/snapshot/handshake sequence in 10.67s; no owned process or pipe residue remained. Host diagnostics showed roughly 5.4 GiB free physical memory with QEMU, Java, and IDE/agent processes consuming substantial working and paged memory |
| Post-diagnosis release-test CTest | PASS | 83/83 tests passed in 269.30s, including the handshake and verifier tests |
| Analyze configure/build | PASS | `FASTFILES_STATIC_ANALYSIS=ON` build completed without warnings or errors |
| ASAN protocol/IPC | PASS | 3/3 passed in 115.95s: protocol fuzz, IPC framing, and IPC listener lifecycle |
| Strict OpenSpec | PASS | 69/69 items passed |
| Verifier doctor | PASS | VC toolset, Windows SDK, CMake, Ninja, and CTest available; optional tools reported as truthful skips |
| Verifier build capability | PASS | `status=PASS`, `classification=PASS`, 9 passed / 0 failed / 0 skipped across Debug, release-test, and Analyze |

The first release timeout is retained as evidence and is not converted into a
pass by the later run: the isolated and sequence experiments established that
the test is not deterministically broken, and the complete post-diagnosis run
is the current release gate. The nested worker fixtures and the supported full
live UIA wrapper were not rerun during this continuation because the current
host remains under the documented resource boundary; no live UIA or production
signing/privileged/clean-host claim is inferred from the local gates above.
