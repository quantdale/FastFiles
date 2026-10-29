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

### Treemap lower bounds and persistent facts (4.10)

The root's actual complete-folder Treemap capture shows a settled tile painted with the canvas brush and only its name; size is hover-only and mode visibility hides the reserved status control. Separately, source inspection identifies positive partial aggregate loss: ResolveDegradedTreemapTotals marks an incomplete 3 MiB folder unresolved/partial, then native RecomputeTotals recursively sums its unrepresented children and replaces the subtotal with zero while clearing partial state. A complete-folder fixture cannot detect this interaction.

Use one extracted production rollup for the native view and headless regression. Explicit aggregateLeaf distinguishes immediate folder summaries from represented containers, including an empty filtered root. Preserve leaf values/states and roll up container bytes with saturation rather than wrap or a misleading zero. Pending and partial child states propagate independently. Allocation totals/counts retain independent incompleteness; a complete logical enumeration does not make allocation exact, and a missing logical descendant cannot make an observed allocation subtotal exact. Folder own allocation records contribute to their parent independently of descendant aggregates. Tiny synthetic aggregation carries the same incompleteness.

Show the shared logical-size text below names when tile geometry permits. Positive partial sizes retain their lower bound, while full UIA/hover descriptions share the same model. Use the existing surface token for settled tiles and preserve the existing High Contrast canvas fallback; no new color literals or category color meaning. Keep coverage feedback visible across Treemap mode in the existing reserved status row, expanding only into the hidden category-filter space. Reposition on mode transition; no additional toolbar rows or new data source. Root compiles native UI and exercises pointer-away complete/partial fixtures and actual screenshots before task4.10 closes.

### D7: Recursive Largest Files ranks known data, with exact path ownership

Original-vision-journey-recovery4.9 uses existing CurrentFolderAndSubfolders indexed search, logical Size descending and max100. Add kind:file to owning and view predicates so directories are excluded before the true top-N heap. Indexed records are labelled coverage-unverified; neither a successful snapshot directory nor a volume ID proves recursive coverage. An unavailable or empty indexed result falls back to successful published/browsed listings on a cancellable worker, labelled partial even when service-connected. Missing descendants remain unknown. No filesystem walk, aggregate-per-file requests, new schema, privileged API or complete-storage claim.

The existing Largest Files mode is recursive only with All categories. Configurable category refinement remains immediate-folder and says so; filtering a capped all-category rank cannot promise a category top-N. Every recursive row owns its actual containing folder. Native/UIA actions, Copy Path, selection restoration and reveal use that path; duplicate basenames and two hard-link paths remain distinct. Location is displayed beside filename and logical size. Sorting reorders the displayed top100 only; the status says that. Unknown allocated bytes remain unknown, and no whole-subtree percentage is computed from the capped sum. Treemap keeps its original hierarchical dataset.

Normalize only matching scope spelling: absolute drive paths, drive roots, trailing separators and extended drive/UNC prefixes. Preserve original operation paths. Reject relative/dot-component scopes; UNC uses published snapshot fallback rather than claiming durable UNC support. Search Hide already cancels its indexed request before Storage opens. Storage hide/refresh/mode/source changes invalidate its generation and cancel its own indexed request before Search reopens. Weak callback ownership, worker cancellation/join and posted-payload draining retire ranking work on normal WM_DESTROY.

Refero research adapts Things Native Precision and Lovable storage-table metadata/location hierarchy; Skiff's visible usage/data scope supports explicit source labels. Clipchamp10908 and Lovable12320 flows reinforce inspect→identify→contextual action rather than a decorative dashboard. Retain Segoe UI/shared tokens; do not copy cloud-provider cards, branding, upsells, warm palettes or bitmap assets. Root-owned actual native/visual qualification is required independently from the pure tests.

Focused command/selection and geometry regressions, Home search E2E with destination identity, storage summary/action tests, native visual catalog and broader Debug/release-test suites. Avoid restricted termination tests and expensive unchanged PR15 benchmarks.

### Bounded status text follow-up — 2026-10-26

Actual120DPI narrow captures show the engine badge and footer wrapping into clipped second lines. Preserve the existing Things/Native Precision target and native UITheme roles. Live Refero style retrieval reaffirms native typography/quiet secondary hierarchy; screen f4eced6d separates file metadata from primary names and screen1a805882 separates indexing status from its detailed explanation. Neither is evidence of Windows truncation behavior. The native decision is a cached DirectWrite single-line layout, with character ellipsis and clipping, rebuilt only for text/width changes. Do not change shared wrapping formats used by details/loading/error panels. A compact badge keeps degraded/retry meaning visible while the existing full description remains available to semantic status consumers. New code does not alter connection/privilege state, data coverage or search semantics. This isolated sibling successor starts from verified PR18; PR19's unrelated action/paint work remains separately stacked.

A real DirectWrite red regression confirms NO_WRAP alone still lays out explicit CR/LF as a second line. The cached layout creator flattens CR/LF/Unicode line separators only in its display copy, preserving the original model/path and the shared wrapping format. The regression uses long Unicode/error text, actual line/trimming metrics and zero-width stale-layout rejection. Caption copy explicitly distinguishes degraded/unavailable/retry states even with a stale privileged flag.

Transient recovery/completion notices take precedence over selection/path metadata in the one-line footer, so truncation cannot hide feedback behind a long folder path. The original status/model text remains available; only displayed composition changes.

### Compact Storage navigation and coverage (4.12)

Actual59f6a8d620px screenshot shows eight stacked Back/Up/mode rows, only four data rows and a clipped-before-warning ranking status. Use the existing Refero Native Precision decision: content stronger than chrome, adaptive rather than compressed controls, native Windows semantics and non-color status. No new reference finding or asset is claimed. Below370DIP inner content, retain Back/Up together and expose the six actual view modes through one labeled native dropdown using the same SetViewMode mutation. Wider layouts retain visible radio choices. Synchronize selector/radios on every mode/open, transfer focus only when the previously focused mode control becomes hidden, and reject cached hidden-radio UIA actions. No shadow accessibility state or alternate data path.

Keep category selection and coverage in separate full-width rows below wrapped/compact controls. Put coverage limitations first in actual status text so ellipsis cannot hide the partial warning. Native controls/theme/High Contrast branch remain authoritative. Validate actual narrow selector keyboard input and data/action consequences, wider radio flow, physical geometry and light/dark screenshots; headless layout alone cannot close4.12.

### Coherent ranking refresh (4.9 follow-up)

A snapshot update previously cleared selection, rows and native item count while its asynchronous replacement was pending. Source review confirms this can retire a just-acquired row and causes visible flicker; no exact attribution is claimed for earlier generic native Select errors. Keep completed display identity separate from request generation: normalized scope plus engine source epoch, with visibility and recursive All-category mode required. Only snapshot refresh may retain it. Source callback even with unchanged active flag, mode/category/scope transition and Hide invalidate it. Retained rows say refreshing previous known data with their original coverage limitation; failed replacement clears unavailable data. Completion uses current selection, including intentional deselection, rather than overwriting it with an older captured selection. Existing request-generation and exact-row identity rejection remain mandatory. Opt-in metadata-only rejection diagnostics distinguish unavailable owner, zero/current count, changed identity and rejected Select dispatch without names/paths/content.

### Native Storage keyboard traversal and repaint

The shell does not use IsDialogMessage; WS_TABSTOP alone cannot supply native workspace traversal. Reuse the existing filtered adjacency/owner-escape policy for Back, Up, the active radio or compact selector, category and the visible data list. Handle only real control Tab/Shift+Tab and unmodified radio arrows; close combo popups before moving focus and preserve Ctrl/Alt shortcut routing. Owner escape remains a real content/chrome focus stop. Repaint the selector field after mode synchronization and avoid redundantly resetting its already-selected native item. Actual narrow acceptance must prove category/selector and Up/selector focus traversal, keyboard mode changes, no stale painted mode label and unchanged data/actions.
### Shutdown qualification follow-up

Atomic publication of the shell HWND removes the C++ read/write race between WM_DESTROY and preview, file-type, engine and aggregate callbacks. Each background post loads one target and retains existing heap-payload ownership on failure. A null repaint target is ignored rather than invalidating the desktop. This does not extend HWND lifetime or prevent destruction after a target was loaded.

The initial a25633e targeted restore passes both explicit close/relaunch boundaries. Actual phase intervals from WM_DESTROY through OLE end are 44 and 67 ms, and shell destruction precedes OLE teardown. Its third restored Home app uses the existing framework teardown and therefore has no natural-close qualification. Strengthen the real scenario to close that third app through the same public helper and unchanged ten-second deadline, record all three observations, and fail if that close fails. The headless actual-scenario fixture fails the old two-close behavior and then verifies both three-close serialization and rejection of third-close failure. Earlier ordinary919 native7/8 remains an immutable failure; new phase timing does not establish its cause.
### Restore conflict policy and real checkbox qualification (4.11)

The PR25 base Restore loop independently requests every collision decision and ignores applyToAllRemaining, despite showing the same checkbox as Copy/Move. It records Skip as ERROR_CANCELLED in submissionFailures. Extract the actual restore decision/target planning into BuildRestorePlan before any Shell submission. Preserve each original opaque Shell identity by index; keep a request-local remaining choice, reserve case-insensitive destinations across original folders and use GenerateKeepBothName. A planned destination can never be overwritten by a later Replace. A later Cancel discards the entire plan, including noncolliding items. Filter executable sources from the plan, count omitted items as skipped, and retain original total accounting, including all-skip completion without Shell execution and setup-failure outcomes. Existing flags, ACL behavior, conflict lifetime, worker ownership and history-pop behavior remain unchanged.

Pure tests execute this production planner. Controlled worker tests use Shell identities of generated holding-folder files so Restore MoveItem callbacks and counts can be checked without mutating the user's Recycle Bin. Those tests do not certify real multi-item Recycle Bin Undo. The actual native driver adds `-Filter apply-all` while preserving the existing default eight single-conflict cases. Six Copy/Move×Replace/Skip/Keep-both cases use real checkbox pointer input, public ToggleState, complete inventories/hashes, a noncollision and pre-existing suffix, then a fresh unchecked next-request prompt. A default Skip followed by a semantically distinct second-file prompt and Cancel must preserve the whole batch. The marked session owns fixture cleanup; fixture names never authorize closing Explorer windows. Native execution, image review and the real multi-item Undo qualification remain with the integrator.

### Cancellation keeps one native progress owner

Actual timed palette qualification at bcc7285 observes focused eltPauseButton on the same PID as FastFiles while its palette edit loses focus and remains empty. The Shell IFileOperation progress window can compete with the application that already renders ProgressSink events in its title/status. Add only FOF_SILENT to suppress that duplicate progress dialog, retaining sink callbacks, cancellation checks, existing error/no-confirmation/recycle flags, preflight conflicts and permanent-delete confirmation. Honor SetOperationFlags failure before submitting filesystem mutation. Microsoft documents this flag as suppressing the progress dialog: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperation-setoperationflags . Actual partial-copy cancellation and original operation/error journeys must qualify the changed flag; source inspection alone is insufficient.

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

### Native shutdown lifetime and pending work (4.8)

The 9195788 ordinary E2E result remains 7/8: Home UIA Close returned true, process exit was not observed by the unchanged10s deadline, and window/process were absent by13s. Root cause remains UNPROVEN. Independent source defects warrant bounded repairs: destroy WindowShell and its COM owners before OleUninitialize on both successful and failed initialization; stop IconCache without draining queued requests, destroy any active result observed after stop, and join the active shell call; remove only completed file-type async states and decline new requests at32 unfinished states before inserting cache placeholders. Preserve full path keys and final task joins; no detachment or termination. An active third-party shell call has no promised duration bound.

With FASTFILES_UIA_DIAGNOSTICS=1, append metadata-only FFDIAG_UI_SHUTDOWN_PHASE records for WM_DESTROY, file-operation stop, preview clear, provider release, message-loop exit, EngineClient stop, icon join, shell destruction and OLE teardown. Existing writer supplies PID and timestamps; paths stay empty. Controlled headless async tests qualify admission only. The integrator owns instrumented native close and broader certification; no timeout waiver, synthetic pass or original-cause attribution follows from these source repairs.
### Discoverable cancellation follow-up (4.7)

Adapt the explicit Cancel all discoverability from the already retrieved Dropbox transfer reference bbf53250-f313-4040-bee0-2ed8183883a0 in evidence/file-operation-feedback-20261026. Keep the native Things/Native Precision command palette, existing tokens and operation semantics; no new research or assets are claimed. Register one selection-independent Global File command, without a default shortcut, visible but disabled when idle with an explanatory reason. Registry-driven Browse/Search menus inherit it; Storage's hardcoded menu is explicitly outside this slice.

HasOutstandingWork reads executing plus queued work under the worker's existing queue mutex. Both shell command contexts use it; invocation and the shared Escape/command handler revalidate it rather than trusting presentation events. Preserve native edit, palette, search and modal Escape precedence. Cancel still scopes only work running or queued at the cancel instant. Palette context updates change enabled flags in place, preserving its query, command order, selection and scroll. Because terminal events are posted before executing clears, the worker additionally posts a payload-free availability notification after that locked transition; the UI queries real worker state. It carries no synthetic operation state and changes no progress/status behavior.

Pure command coverage qualifies discovery, workspace/selection independence and fresh-context rejection. Existing worker fixtures add lifecycle assertions, but their execution and actual native discovery/cancellation qualification are pending serialized root validation on the integrated source. Task4.6 on919 remains historical pending acceptance, not silently closed by this successor.

The owned native driver now offers `-CancellationRoute Palette` while its default Escape path remains unchanged. Pending root qualification command: `pwsh ./verify/e2e/Test-OwnedFileOperations.ps1 -ArtifactsDir <fresh-owned-evidence-directory> -Filter cancellation -CancellationRoute Palette` (two cases), then `-Filter all -CancellationRoute Palette` if needed (eight cases). Escape retains one/seven cases. Palette cancellation must follow observed partial copy, exact public command selection and accepted product dispatch, then the same terminal/batch/name/hash/sentinel checks. The second case captures actual idle discovery, sends Enter and requires the owned standard refusal dialog with exact descriptor reason; native listbox per-row IsEnabled is not used as command-availability evidence. Public semantic state, actual input route/timing, measured-DPI screenshots and all owned names/hashes are retained; implementation/parsing does not certify these native outcomes.
