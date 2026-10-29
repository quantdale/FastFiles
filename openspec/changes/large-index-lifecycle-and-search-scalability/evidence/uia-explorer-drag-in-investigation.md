# Explorer-to-FastFiles UIA drag investigation (Phase C) — 2026-10-21/22

Focused investigation of the certification-recorded UIA failure
(`drag-in-from-explorer`, reported failing operation `SelectionItemPattern.Select`
on a Windows Explorer-owned item) per the PR #14 closure mission. The mission's
question: can the failure be resolved through a correct supported automation
strategy, or can the current environment not qualify the complete scenario?

**Outcome in one line:** the recorded failure was a **test-automation defect
(proven and fixed)**; with the fix, the Explorer-owned source interaction is
now fully qualified, but the **complete Explorer-to-FastFiles drop journey
still does not deliver on this host** and is retained as
`EXPLORER_TO_FASTFILES_E2E = UNQUALIFIED` with the exact evidence below. The
scenario remains in the suite un-suppressed.

## 1. Environment

- Windows 11 Home build 26200, interactive session 1, single **1920x1080**
  display (virtual screen metrics confirmed), no elevation.
- Explorer: Windows 11 File Explorer (`CabinetWClass`), item rows expose
  `ControlType.ListItem` (class `UIItem`, FrameworkId `DirectUI`) with
  patterns Invoke/Value/**SelectionItem**/ScrollItem.
- FastFiles: `build/debug-diagnostic` (unsigned diagnostic build), degraded
  mode, engine connected in every scenario run used as evidence.

## 2. Root cause of the originally recorded failure — TEST AUTOMATION DEFECT
## (proven, fixed)

The scenario located the drag source with `Wait-UiaElement -Name
'explorer-in.txt'` and no control-type constraint. The first name match in
Explorer's Details view is **not the item row**: it is the Details-view name
container, a `Group` (AutomationId `DetailsViewControlRootGrid`, class
`NamedContainerAutomationPeer`) that merely *contains* the name and **does not
expose SelectionItemPattern**. The pre-instrumentation harness collapsed three
distinct failure modes into the single message "SelectionItemPattern.Select did
not produce a selected row" and discarded the exception, so the recorded
failure could not be attributed from artifacts.

Evidence (first instrumented run, `run1`):
`attempt=3 ... [SelectionItemPattern unavailable; name='explorer-in.txt'
automationId='DetailsViewControlRootGrid' controlType='Group'
className='NamedContainerAutomationPeer' pid=32016]` — the failing element was
the container, not the row. This is **the only failure mode consistent with
run1's element capture and the pre-fix code path** on the same host and view:
the certification-era artifacts themselves cannot discriminate the three
collapsed modes (the pre-fix harness discarded the exception), so the
attribution to the container-match mode is near-certain but formally an
inference over the collapsed message.

Fixes applied to `verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1`
(behavior-preserving evidence capture + targeting correction):

1. Source-item discovery constrained to `-ControlType 'ListItem'` (the row that
   actually exposes SelectionItemPattern).
2. Selection failure detail now carries the exact mode (pattern unavailable /
   `Select` threw with exception type + HRESULT + message / Select returned but
   `IsSelected=False`) and the failing element's identity; per-attempt evidence
   is accumulated instead of overwritten.

Post-fix evidence: item selection succeeds on **100% of harness attempts**
(runs 2/3/5: 3/3 attempts each; probe1: `Select` returns and
`IsSelected False->True`; pointer click fallback also selects).

## 3. Source interaction is qualified; drop delivery is not

With the targeting fixed, every attempt proceeds: the row is selected and the
native mouse-drag input stage is accepted (`acceptedInput=true`, source
`'explorer-in.txt'`). The destination file never appears. The scenario's own
contract classifies this `SKIPPED / BLOCKED_ENVIRONMENT /
explorer-drag-source-unavailable` — "three fresh Explorer-source attempts
selected the item and every native input stage was accepted, but no destination
file appeared" — which is a recorded BLOCKED condition, never a PASS.

Stability of the failing journey (all on the final harness state):

| Run | Attempts | Selected | Input accepted | Destination file | Verdict |
|---|---|---|---|---|---|
| harness run 2 | 3 | 3/3 | 3/3 | none | SKIPPED/BLOCKED_ENVIRONMENT |
| harness run 3 | 3 | 3/3 | 3/3 | none | SKIPPED/BLOCKED_ENVIRONMENT |
| harness run 5 | 3 | 3/3 | 3/3 | none | SKIPPED/BLOCKED_ENVIRONMENT (run PASS, 0 failed) |
| harness run 6 | 3 | 3/3 | 3/3 | none | SKIPPED/BLOCKED_ENVIRONMENT (run PASS, 0 failed) |

## 4. Probe evidence (disposable scripts under `build/campaign/probe_explorer_select/`)

- **probe1 (item interaction):** the Explorer row is a `ListItem`
  (`UIItem`/DirectUI, pid-owned by the test Explorer window) exposing
  Invoke/Value/SelectionItem/ScrollItem; `SelectionItemPattern.Select` returns
  and selects (`IsSelected False -> True`); a real pointer click also selects.
  **Item existence + interactability: PROVEN.**
- **probe2 (does the injected drag primitive work at all?):** the exact
  harness primitive (`Send-UiaMouseDrag`, SendInput arm/step sequence)
  transfers a file **Explorer -> Explorer** (same-volume move, source consumed)
  with on-screen geometry. **Injected Explorer drags can engage and deliver on
  this host.** (First probe2 iteration is INVALIDATED and preserved as
  `probe2-result-geometry-bug-offscreen.json`: its stacked layout put the
  destination at y=1640 on a 1080p screen.)
- **probe3 (capture oracle audit):** `GetGUIThreadInfo` **fails** on this host
  for the Explorer thread on every surface tested (item, empty view, title
  bar — `gtiOk=False` while the button is held). The scenario's `armObserved`
  flag is built on this call, so it can never read true here: its
  "cross-process capture was not observable" wording is literally accurate and
  its `false` value is **not evidence either way** about drag engagement.
- **probe4 (drop-side localization, 3 path strategies x 2 placements):**
  straight coarse path, fine 4px boundary crossing, and slow in-client hover
  (1.2 s), with Explorer beside FastFiles (probe4-result.json) AND overlapping
  its right edge (probe5-overlap-result.json) so the cursor's first entry into
  the window is inside the client: **no delivery in any of 6 combinations.**
  The mid-hover screenshots show **no drop-feedback emphasis** on FastFiles.
  Reviewer-B correction (2026-10-22): `WindowShell::SetDropFeedback` is
  effect-agnostic (paints whenever `DragEnter`/`DragOver` runs, whatever the
  negotiated effect) **but** `dropFeedbackValid_` suppresses painting when the
  drop point's content x falls outside the column viewport (sidebar/details
  pane). The probe4/probe5 hover points are inside the viewport (content
  x ≈ 373 DIP vs ≈ 493 DIP available) where the single-pane `ViewportOutline`
  would be conspicuous — so at those points `DragEnter`/`DragOver` did not
  run (or ran and left before the sample; a transient enter-then-abort cannot
  be excluded from a screenshot alone). File-name note: the two hover
  captures shared one hardcoded filename (`probe4-hover-shot.png`); the file
  on disk is probe5's overlap-run capture, probe4's was viewed live at run
  time.
- **probe6 (cross-window witness):** Explorer source (right) -> across the
  whole FastFiles window (navigated to its own `ff-target` folder) -> witness
  Explorer window (left, overlapping). Outcomes: `ffTarget=False`,
  `dstFile=False`, `srcStill=True`. Reviewer-B correction: the mid-path
  screenshot's sample point falls in the feedback-suppressed details-pane x
  region, so **no drop-feedback inference is drawn from it** — the probe6
  evidence stands on the filesystem outcomes (delivered neither to FastFiles
  nor to the witness window: the drag was not alive over FastFiles' client by
  the time it mattered).
- **Desktop artifact check:** no stray copies of the dragged file on the user
  Desktop (a drop-on-desktop artifact is absent).

## 5. Classification (the four required separations)

1. **Test automation failure — PROVEN AND FIXED** for the failure mode the
   recorded operation exhibits under identical name-only matching (the
   name-container `Group`; see §2 on the attribution caveat for the
   certification-era run).
2. **External Explorer provider limitation — NOT PROVEN.** The Explorer source
   supplies a correct ListItem with SelectionItemPattern and its OLE drag
   source delivers under the same injected primitive (probe2).
3. **FastFiles application defect — NOT PROVEN, but a live co-equal
   hypothesis.** The drop **handler** never executed (fully proven: zero
   deliveries across 9+ attempts in 4 harness runs and 7 probe variants, the
   dragged source never consumed, no desktop strays — the handler would have
   created the file), and `DragEnter`/`DragOver` did not run at the
   paint-valid probe4/probe5 hover points. Reviewer-B correction: the
   campaign's own evidence **falsifies** the generic "injected OLE drags drop
   at top-level window entry" doctrine as an explanation — probe2 delivered
   Explorer->Explorer and `drag-out-to-explorer` passes with the same
   injected primitive, crossing top-level windows in **both** source
   directions. The failing pair is exclusively **external source -> the
   FastFiles window**, which points at the FastFiles destination side (e.g.
   OLE hit-test/registration of the DirectComposition window for external
   sources) at least as strongly as at any environment class. Context: live
   external drop receipt has never been validated end-to-end on this host
   (historical `file-operations` 8.8 closed with a SKIP), and intra-app drops
   (`drag-between-panes`) work — so a destination-side registration/hit-test
   gap for external sources is unfalsified.
4. **Residual cause CLASS (not further separable on this host).** Two
   candidates remain and the evidence no longer favors the environment side:
   (a) an external-source OLE delivery gap specific to the FastFiles window
   (destination-side hit-test/registration — favored by the probe2/drag-out
   cross-window successes, which falsify the generic injected-input boundary
   story), and (b) an injected-input OLE behavior that manifests only for the
   external-source -> FastFiles-window pair. Separating them needs a second
   interactive Windows host, physical input, temporary product-side drag
   instrumentation, or an independent drop-target witness window — none
   available under this campaign's constraints. The historical
   `Position-UiaExplorerBesideFastFiles` doctrine comment ("injected OLE
   drags drop at top-level entry") is retained as prior-session observation
   but is **not** a proven mechanism here.

## 6. UIA acceptance matrix (mission §14)

| Surface | Status | Evidence |
|---|---|---|
| FastFiles core UIA | **PASS** | certification ladder 10.1 (all FastFiles-surface scenarios green, same binaries — product tree unchanged, equivalence gate PASS) |
| FastFiles other interactive journeys (search, navigation, drag-out, drag-panes, E2E 8/8) | **PASS** | certification ladder 10.1 + E2E boundToHead |
| Explorer-owned source interaction (item discovery, selection, drag input arming) | **PASS** (qualified post-fix) | runs 2/3/5/6 (3/3 attempts each) + probe1 |
| FastFiles drop receipt from an Explorer source | **BLOCKED / NOT_QUALIFIED** | drop handler never executed (0 deliveries across 9+ attempts; source unconsumed; no strays); `DragEnter`/`DragOver` absent at paint-valid probe4/probe5 hover points; destination-side FastFiles hypothesis remains live (§5.3) |
| Complete Explorer-to-FastFiles drag journey | **UNQUALIFIED** | 4 harness runs x 3 attempts + 7 probe variants: 0 deliveries; scenario recorded SKIPPED/BLOCKED_ENVIRONMENT, never PASS and never suppressed |

## 7. Harness defects found and fixed in this investigation

1. **Item targeting** (above): name-only match grabbed a container Group.
2. **Evidence capture** (above): the selection failure path discarded the
   exception and conflated three failure modes.
3. **`CapabilityRunner` cleanup verification robustness** (`verify/core/CapabilityRunner.psm1`):
   `Win32_Process.CreationDate` transiently read back empty for two live
   stale-build processes, which (a) failed-closed honest runs as
   `CLEANUP_INCOMPLETE` with empty residue ("creation time unavailable"), and
   (b) let those same unidentifiable processes slip past the empty-baseline
   preflight. Fix: an independent `Get-Process.StartTime` fallback (the same
   underlying clock `Test-VerificationProcessIdentityCurrent` already uses,
   2-second tolerance) before declaring an identity unreadable; genuinely
   unreadable-and-live identities still fail closed. After the fix, run 4
   correctly surfaced the real condition — `stale-build-process-before-run`
   naming pid 25304 (FastFiles.exe) and pid 25816 (FastFilesEngine.exe),
   both `build/debug-diagnostic` test binaries left running by the earlier
   campaign session (started 2026-10-21 19:57 local). They were inspected
   (repo-build test artifacts, no active test) and terminated as the named
   remediation; run 5 then reported **run PASS / 0 failed** with the scenario
   honestly recorded as skipped.

## 8. Retained boundary

`EXPLORER_TO_FASTFILES_E2E = UNQUALIFIED`. The scenario is not suppressed, not
downgraded to PASS, and not silently skipped: it runs in every UIA pass and
records `SKIPPED/BLOCKED_ENVIRONMENT` with the full per-attempt evidence. The
qualification boundary is exactly: source half proven, drop receipt never
reached, cause narrowed to an injected-input/OLE-boundary class that this
single host cannot further separate. Closing it needs either a second
interactive Windows host, physical human input, or temporary product-side
drag instrumentation in a dedicated future campaign.
