## Context

Fresh baseline: origin/main a26fe609. See docs/product/original-vision-alignment.md and docs/product/original-vision-source-ledger.md. Source review identified wrong-target storage actions and a Home/search navigation split. Runtime evidence is collected separately; source presence is not runtime certification.

## Goals / Non-Goals

Restore trustworthy visible-selection commands and Find -> reveal -> navigate continuity. Preserve native rendering, UIA, DPI, degraded operation, canonical search and normal operation handlers. No framework rewrite or production privileged qualification.

## Decisions

### D1: Visible workspace owns command context

Why: a hidden Column View selection must never supply a Storage/Search file command. Resolve selection and applicability together; explicit invalid targets fail closed without fallback. Commands requiring a browsing destination remain unavailable in alternate workspaces unless that workspace defines the destination. Inline rename/select-all must not mutate a hidden view.

### D2: Category summaries are not files

Why: joining a category label to a folder can target an unrelated real sibling. Category activation may refine the current storage listing; category rows cannot supply filesystem command targets.

### D3: Search reveal uses the Home-aware navigation boundary

Why: history, visible pane kind, hierarchy, selection, focus, scroll and chrome must agree after activation. Preserve reconstructed indexed hierarchy and active-pane identity; test the actual resulting selected item rather than panel dismissal.

### D4: Preserve Native Precision Workspace

Primary existing Things direction, shadcn border discipline, Skiff filename/location hierarchy; live Refero style/screen/flow research recorded in the audit. Keep Segoe UI, current tokens and small native radii. Code-native geometry only, no bitmap assets. Location remains readable before secondary metadata; never scale a physical RECT twice. Functional tests and screenshots are separate gates.

### D5: Share virtual-list semantics for Storage accessibility

Live E2E found the native Storage owner-data list renders rows but the host exposes only a Pane with no rows or headers. Source inspection cannot qualify action targeting. Reuse the existing bounded result-list provider foundation with distinct surface identity/configuration and a real Storage row projection. Avoid an accessibility-only data model or copied provider implementation. Pointer, keyboard and UIA activation route through the same mutation; stale identities and owner lifetimes remain fail-closed. A successful semantic screenshot is still separate from filesystem-action correctness.

### D6: Presentation owns semantic workspace lifetime

Search and Storage replace Browse pixels, so hidden Browse/Home descendants must not remain discoverable or actionable. ColumnView owns the real Browse presentation state, updated by shell transitions and consumed by rendering and UIA. Reuse full-fragment retirement on hide/restore; structural pane counts remain intact. Native child tool controls and the separately active Treemap remain available. Do not emit semantic transitions from painting.

## Risks / Trade-offs

### Native palette paint correction

Visual review found a real command edit/list in hit-testing and UIA while Storage painted over it. SourceCopy/CAPTUREBLT captures were identical, disproving the capture-flag hypothesis. Setting sibling clipping in an isolated owned window exposed the palette. Apply WS_CLIPSIBLINGS to real direct child HWNDs at creation so workspace paints honor overlay z-order, including future controls. Remove the full-window layered dimmer, whose clipping hid useful surrounding context; keep the native edit/list and existing theme roles. Consume outside client clicks in the canonical message path to dismiss without accidentally activating a background file or navigation control. This adapts the existing Things/Linear direction's compact contextual command surface without new branding or a framework change.

Filesystem targets may disappear after validation; ordinary operation handlers still report real OS failures. This is UI intent validation, not a new security boundary. Storage summaries remain scoped to available snapshot coverage; recursive global ranking and privileged coverage are separate work. Full physical DPI/High Contrast coverage depends on the host and must be reported truthfully.

## Validation

### File-operation feedback follow-up — 2026-10-26

Actual clean combinedde94 owned-file validation reproduces a locked-source failure whose dialog is only a long path plus0x80270027. SDK sherrors.h identifies COPYENGINE_E_SHARING_VIOLATION_SRC, distinct from destination0x80270028. Preserve the existing native MessageBox, retained per-item failure list, original path/HRESULT and canonical manual Copy/Paste retry. Add presentation-only descriptions for actual source/destination sharing, ordinary Win32 locks, access, capacity and disappeared paths; bounded system-message lookup for other HRESULTs and a neutral fallback must always retain the code. No automatic retry, elevation prompt, new operation plan or success reclassification.

Refero design lock remains Things0796cd74-edc2-4e12-9c71-25fac02a1cb2 / existing Native Precision. Live screen retrieval refreshed0cbae453-88aa-444e-9ffa-eed388d87bc9 (Uber Eats error/retry) andbbf53250-f313-4040-bee0-2ed8183883a0 (Dropbox transfer outcomes), following prior live style/screen/flow research. Adapt adjacent recovery explanation and truthful partial/completed outcomes; reject the untranslated raw error key visible in the Uber reference. Do not copy web styling, bitmap assets or add a Retry button without a safe repeat plan. Existing Segoe UI/theme/Windows modal geometry remains authoritative.

Owned native acceptance uses a calibrated Shell Recycle Bin positive control before claiming permanent exclusion, actual terminal-column SelectionPattern sets, hash-bound multi-copy/move, a genuinely exclusive source lock followed by release/manual retry, and a bounded64MiB batch observed partially copied before real Escape. An unobserved or already finished batch is not passing cancellation evidence. Query only exact GUID-owned bin matches; never clear/restore unrelated entries. Standard MessageBox children expose Pane roles on this host, so class/name text is inspected truthfully without inventing a Text role. Preserve failed exploratory harness assumptions.

Focused command/selection and geometry regressions, Home search E2E with destination identity, storage summary/action tests, native visual catalog and broader Debug/release-test suites. Avoid restricted termination tests and expensive unchanged PR15 benchmarks.

### Native conflict decisions remain readable and reachable

The live a3e20ab conflict audit cannot find the painted Replace label through UIA; the HWND caption is empty. Its actual120DPI dark screenshot also shows a white custom-window background and black checkbox text on a dark field. Source omits WS_TABSTOP on every action/checkbox. The ConflictDialog/UiComponents/UITheme source is unchanged from the new branch's verified PR18 base981a9ba; full application equivalence is not implied.

Retain the existing Native Precision/Things/shadcn reference lock and localized warning treatment. Set the native Replace caption, add real Tab stops while keeping Skip initially focused, erase the custom window with its token brush, and reuse Settings' ApplyNativeExplorerTheme for ordinary buttons/checkbox with High Contrast reset. No bespoke UIA state or operation-policy rewrite. Native public UIA/real pointer input verifies named actions, focus traversal and each decision against marked fixture content; screenshots separately qualify the actual theme and geometry. Native buttons on this host may omit InvokePattern, so use the existing E2E real-pointer fallback rather than hidden commands or synthetic success.

### Native close qualification follows the driver's actual key contract

Broader conflict-source E2E exposed an uninstrumented Home-close failure. Independently, the old close helper's `%{F4}` is demonstrably plain F4 under the repository parser. Correct only the fallback to `{ALT}{F4}`, preserve UIA-first routing and the existing10s exit bound/ownership checks, and record return/error, actual fallback key events and observed exit. The closed-mock regression runs the real private helper and real key translator with -NoSend, so it cannot close a live window or terminate a process. Actual restore qualification must retain original failure and avoid claiming causality when old telemetry is absent.

Close observations must be dictionary keys in the existing ordered visibility map; ETS note properties disappear from JSON. A closed-mock execution of the real three-lifecycle restore scenario verifies round-trip visibility plus both close observations without live processes or input.

### Discoverable cancellation follow-up (4.7)

Adapt the explicit Cancel all discoverability from the already retrieved Dropbox transfer reference bbf53250-f313-4040-bee0-2ed8183883a0 in evidence/file-operation-feedback-20261026. Keep the native Things/Native Precision command palette, existing tokens and operation semantics; no new research or assets are claimed. Register one selection-independent Global File command, without a default shortcut, visible but disabled when idle with an explanatory reason. Registry-driven Browse/Search menus inherit it; Storage's hardcoded menu is explicitly outside this slice.

HasOutstandingWork reads executing plus queued work under the worker's existing queue mutex. Both shell command contexts use it; invocation and the shared Escape/command handler revalidate it rather than trusting presentation events. Preserve native edit, palette, search and modal Escape precedence. Cancel still scopes only work running or queued at the cancel instant. Palette context updates change enabled flags in place, preserving its query, command order, selection and scroll. Because terminal events are posted before executing clears, the worker additionally posts a payload-free availability notification after that locked transition; the UI queries real worker state. It carries no synthetic operation state and changes no progress/status behavior.

Pure command coverage qualifies discovery, workspace/selection independence and fresh-context rejection. Existing worker fixtures add lifecycle assertions, but their execution and actual native discovery/cancellation qualification are pending serialized root validation on the integrated source. Task4.6 on919 remains historical pending acceptance, not silently closed by this successor.

The owned native driver now offers `-CancellationRoute Palette` while its default Escape path remains unchanged. Pending root qualification command: `pwsh ./verify/e2e/Test-OwnedFileOperations.ps1 -ArtifactsDir <fresh-owned-evidence-directory> -Filter cancellation -CancellationRoute Palette` (two cases), then `-Filter all -CancellationRoute Palette` if needed (eight cases). Escape retains one/seven cases. Palette cancellation must follow observed partial copy, exact public command selection and accepted product dispatch, then the same terminal/batch/name/hash/sentinel checks. The second case captures actual idle discovery, sends Enter and requires the owned standard refusal dialog with exact descriptor reason; native listbox per-row IsEnabled is not used as command-availability evidence. Public semantic state, actual input route/timing, measured-DPI screenshots and all owned names/hashes are retained; implementation/parsing does not certify these native outcomes.
