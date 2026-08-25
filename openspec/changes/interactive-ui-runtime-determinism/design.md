## Context

The current main branch has a real failure in the supported interactive validation path: `selection-model-interactions` reaches `Ensure-UiaDualPane`, invokes the real Toggle Dual Pane palette command, and fails to discover `Pane.1`. A historical version of the scenario completed 10/10, while later UI, provider, state, and harness changes changed the surrounding lifecycle. The current product handler intentionally updates both `NavigationWorkspace` and `ColumnView`; that contract must remain intact.

The baseline is recorded in `evidence/baseline-pane1-toggle.md`. The first run was not accepted on retry. The driver currently proves neither unique command selection nor a command-specific postcondition for Toggle Dual Pane, and the scenario cleanup does not preserve exact before/after process ownership. These facts make several failure classes plausible; they do not establish a timing root cause.

## Goals / Non-Goals

**Goals:**

- Establish evidence that separates product state, command/input, provider, readiness, persisted-state, process, harness, and external-desktop failures.
- Make dual-pane enable/disable and UIA provider replacement deterministic at their existing production seams.
- Add headless regression coverage for the production workspace/view contract and provider identity/lifetime rules.
- Give interactive scenarios semantic command, focus, cleanup, and repeated-run contracts.
- Preserve reproducible, privacy-bounded diagnostics and honest external skips.

**Non-Goals:**

- No production signing, trust-store, administrator, raw MFT/USN, service, IPC, or engine-lifetime changes.
- No OCR, pixel interpretation, arbitrary sleeps as proof, UIA-only product state, broad process-name killing, or duplicate Enter workaround.
- No replacement of the complete UI automation module for style reasons.

## Decisions

### D1. Classify the earliest failed semantic boundary

The campaign will capture a transition record with command identity, workspace pane state, ColumnView pane state, active pane, current main window, foreground/focus, provider identity, layout readiness, process ownership, and persisted-fixture identity. The first boundary that cannot be proven determines the primary class; later symptoms remain secondary observations.

**Why:** A missing Pane.1 can be caused by a product transition defect, failed dispatch, wrong active window, provider replacement, layout readiness, contaminated state, leaked process, discovery bug, or desktop limitation. Treating all of them as timing hides ownership.

**Alternative rejected:** Increasing a single timeout or accepting a later pass would produce no causal evidence and violates the clean-run gate.

### D2. Keep workspace and view state as separate but synchronized authorities

The existing production handler remains responsible for updating `NavigationWorkspace` and `ColumnView` in one command path. Tests will exercise both seams and assert their invariants; they will not create a test-only state model. If a mismatch is found, repair the lowest production ownership layer and keep the dual update explicit.

**Why:** Workspace navigation state must survive rendering/provider reconstruction, while ColumnView owns the visible interaction model. Removing either update would regress restoration or accessibility.

**Alternative rejected:** Making UIA state authoritative would hide a product defect from keyboard and normal rendering paths.

### D3. Make command-palette success semantic and command-specific

The driver will query the live palette tree, require a unique enabled result and selected identity, verify foreground ownership, activate once, observe the palette transition, and then evaluate a caller-supplied postcondition. Toggle Dual Pane will use pane/workspace/provider postconditions rather than a generic “Enter was sent” result.

**Why:** The current helper accepts Toggle Dual Pane without an expected result or postcondition, so it can report success even when the workspace is unchanged.

**Alternative rejected:** Sending Enter repeatedly or relying on a fixed close delay would mask input routing and dispatch defects.

### D4. Treat fragment replacement as an explicit reacquisition boundary

After a state change that can alter the ColumnView fragment, all descendants are discarded. The driver re-roots at the current main-window provider and verifies AutomationId, runtime identity, geometry, and pane count. Product providers continue to use identity checks and safe stale-element errors.

**Why:** The semantic UIA contract already requires stale elements to fail safely and never retarget a new occupant. A cached root/child traversal must not survive an intentional fragment change by accident.

**Alternative rejected:** Keeping old elements and retrying property reads creates stale-provider ambiguity.

### D5. Use bounded semantic waits as the only completion proof

Reusable waits will poll explicit predicates and return structured timeout state. A small polling interval or render-settle cadence is allowed, but sleep duration never proves that an action succeeded. Provider replacement, pane count, foreground/focus, and layout predicates will have distinct wait labels.

**Why:** Different failures require different evidence and remediation; one broad wait cannot distinguish them.

**Alternative rejected:** A monolithic retry loop would blur failure ownership and make clean-run counting impossible.

### D6. Isolate persisted state without deleting user settings

Interactive scenarios will launch with disposable profile roots and a test-owned workspace fixture. A known-state precondition will be observed and recorded before the action. Product restoration tests will use disposable in-process or fixture state, while user settings outside the scenario remain untouched.

**Why:** Prior dual-pane state is a plausible contaminant, and deleting real settings is unsafe and outside the test contract.

**Alternative rejected:** Resetting the user’s normal settings or assuming the first launch is clean.

### D7. Make process ownership evidence explicit

The harness will snapshot repository-build UI/engine candidates before launch, record executable path, PID, start time, and launch/adoption relationship, and terminate only scenario-owned UI or explicitly adopted test-engine processes. Installed production copies and the privileged service remain excluded. Cleanup waits for exit and records named-pipe/lifecycle residue.

**Why:** The engine is intentionally persistent, so broad name-based cleanup is both unsafe and unable to explain contamination.

**Alternative rejected:** Killing every process with a matching image name or changing the engine lifetime.

### D8. Test the real seams at two levels

Plain C++ tests will cover workspace/view transitions, active-pane/focus invariants, Home/file-system typing, restoration, selection isolation, provider identity/lifetime, and repeated toggles. PowerShell tests will cover driver mechanics with recorded/synthetic UI trees only where product semantics are not being claimed. The interactive scenario remains the final product proof.

**Why:** Headless tests make state-machine regressions cheap to localize; live UIA runs validate actual Win32/UIA/input integration.

**Alternative rejected:** A fake provider or driver-only pass standing in for the native product.

### D9. Emit bounded, privacy-safe evidence

Diagnostics use stable command IDs, counts, booleans, HWND/PID, provider IDs, pane geometry, stage, duration, and test-fixture labels. They do not include arbitrary user path contents, file contents, secrets, or trust material. High-volume tracing remains opt-in/test-scoped.

**Why:** The missing information is lifecycle metadata, not user data.

### D10. Count repeated passes only after clean lifecycle validation

The selection scenario will be run in isolated lifecycles for at least twenty consecutive passes when an interactive desktop exists. Any unexplained retry, cleanup leak, stale-provider escape, or classified failure resets the count. A genuine external prerequisite limitation is recorded separately and cannot relabel the known Pane.1 defect.

**Why:** A retry can prove recoverability but cannot prove determinism.

## Risks / Trade-offs

- [Risk] A real UIA fragment can be replaced while a provider call is in flight. → Re-root after `UIA_E_ELEMENTNOTAVAILABLE`, keep provider work on the UI thread contract, and add repeated stale/lifetime tests.
- [Risk] Native foreground rules can block input even when the product is healthy. → Record foreground owner and classify external blocking only after product and harness checks; do not turn it into a product pass.
- [Risk] A persistent engine may be useful to another local process. → Preserve it by default, record it as pre-existing, and use exact ownership evidence for cleanup.
- [Risk] Workspace restoration can reintroduce dual-pane state between scenarios. → Use disposable profiles and prove the initial state before every semantic operation.
- [Risk] More diagnostics may expose sensitive paths or create noisy production logs. → Keep evidence bounded, fixture-labeled, and test-scoped.
- [Risk] A headless state test can pass while native input remains broken. → Retain the real interactive capability and the 20-clean-run gate as separate evidence.

## Migration Plan

1. Land the OpenSpec contracts and baseline evidence.
2. Add/repair production seam tests and fix any product defect at its owning layer.
3. Add structured driver primitives and scenario-owned lifecycle cleanup.
4. Run focused tests, full portable gates, and the interactive capability where available.
5. Update workstate and current-state documentation with the root cause and exact evidence, then commit and reconcile/push `main`.

Rollback is a normal Git revert of the campaign commit(s); no persistent schema, trust-store, service, or user-profile migration is introduced.

## Open Questions

None that change the chosen architecture or task breakdown. Whether the final Pane.1 failure is product, provider, harness, contamination, or external will be answered by the recorded transition evidence during implementation.
