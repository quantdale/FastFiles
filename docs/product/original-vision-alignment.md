# FastFiles original-vision alignment

The July 30, 2026 product thesis is **find, navigate and understand your entire filesystem instantly**: WizFile discovery, Finder hierarchy, WizTree storage intelligence and trustworthy daily file operations in one native Windows application.

Initial audit source: fresh main `a26fe609`. PR14/15 performance evidence is a separate unmerged track, not evidence that main already contains those optimizations. Independent source audit and live Refero research completed 2026-10-25. Runtime results are recorded below as they become available; source inspection alone never establishes a verified user journey.

## Five-pillar scorecard

| Pillar / original intent | Initial classification | Implementation and real journey | Limitations / missing functionality | Engineering action |
| --- | --- | --- | --- | --- |
| A: instant whole-computer filename discovery | PARTIALLY_IMPLEMENTED | Chrome Fast Search, sidebar and Home entries -> typing -> virtual results -> scope/sort -> reveal; local snapshot search and indexed engine routes | Degraded scope is browsed/pinned coverage, not all files. Home search reveal can leave Home covering the result; deep leaf/chrome not synchronized. WizFile usability parity unverified | Repair actual result destination and E2E assertions; preserve PR14/15 search semantics/performance work |
| B: understand space and act safely | PARTIALLY_IMPLEMENTED | Fixed-drive OS capacity -> Storage list/treemap -> folder drill-down -> file reveal; logical/allocated sizes and partial aggregates | Storage context target discarded; categories treated as files. Largest Files/Folders and category totals currently operate on immediate children, not recursive global ranking. Root snapshot success incorrectly implies full volume coverage | Correct target ownership and category routing first; make coverage and local ranking scope truthful |
| C: spatial Finder-style hierarchy | IMPLEMENTED_BUT_UNPOLISHED | Linked columns, keyboard traversal, active pane, history, horizontal reveal, preview, cached icons, stable UIA identity | Search bypasses the canonical Home-aware boundary. Comparative usability and physical display matrix unverified | Use shared navigation lifecycle and verify selected destination plus visible geometry |
| D: daily-driver manager | IMPLEMENTED_BUT_UNPOLISHED | Tabs, dual panes, bookmarks/recents, clipboard, queued IFileOperation, conflicts, recycle/permanent delete, undo/cancel/error, OLE source/target, palette/settings | Wrong-target alternate-workspace commands undermine safety. Explorer drag-in remains UNVERIFIED; old skipped scenarios are not passes | Repair visible context; exercise disposable file-operation and supported interoperability journeys |
| E: complete, cohesive native surface | IMPLEMENTED_BUT_UNPOLISHED | Existing native precision tokens, light/dark, High Contrast fallbacks, reduced-motion gate, responsive chrome/Home, semantic UIA | Long search location/metadata can collide due double DPI scaling; first-run effectiveness and visual coherence need actual screenshots | Preserve design direction; repair measured composition defects and review native screenshots |

No pillar is classified ALIGNED_AND_VERIFIED from source alone. Privileged signing, clean-host/raw-volume and physical display certification remain BLOCKED_EXTERNAL or UNVERIFIED according to their existing acceptance records.

## Prioritized gaps

Scores use impact × confidence × frequency × executability ÷ regression risk, each component 1–5. Data-safety findings take precedence over the numeric score.

| Gap | Components | Score | Planned / completed remediation |
| --- | --- | --- | --- |
| Storage actions target hidden browse selection | 5×5×4×5÷2 | 250 | Visible workspace owns context; stale explicit targets reject without fallback; preserve existing operation handlers |
| Category label can become unrelated sibling path | 5×5×3×5÷2 | 187.5 | Synthetic summaries cannot supply file targets; activation refines storage listing |
| Home -> Search -> result leaves wrong pane kind / offscreen leaf | 5×5×5×5÷2 | 312.5 | Home-aware navigation with reconstructed hierarchy, selection, scroll and chrome sync; real destination E2E |
| Search location and metadata overlap at high DPI | 4×5×5×5÷2 | 250 | Shared physical-pixel bounds, independent ellipsis, narrow and 96/120/192 DPI tests |
| Storage coverage / largest-item scope overclaim | 4×5×3×5÷2 | 150 | Label proven coverage and current-folder ranking truthfully; recursive ranking remains separate capability |
| Explorer drag-in qualification | 4×3×4×3÷3 | 48 | Supported existing UIA drag scenario; preserve OS/foreground limitations rather than fake success |
| Hidden Browse/Home remains discoverable through UIA under tool workspaces | 4×5×4×5÷2 | 200 | Tie semantic fragment lifetime to actual workspace presentation; preserve Treemap ownership and restore fresh fragments |
| Palette semantically present but painted behind native Storage controls | 4×5×5×5÷2 | 250 | Actual sibling paint clipping; native command choices stay visible, outside dismissal consumes its click |
| Settings completion buttons clipped by caption/frame at120DPI | 4×5×3×5÷2 | 150 | Reserve intended client area plus measured non-client frame; assert both footer bounds and real pointer dismissal |

## Answers to the twelve alignment questions

1. **Find across the computer as intuitively as WizFile?** Entry points and typed search exist; usability parity and whole-computer production coverage are unverified. Degraded mode is deliberately bounded.
2. **Immediately navigate from a result?** Intended behavior exists; confirmed Home/scroll/chrome composition defects are this campaign's primary navigation repair.
3. **Useful degraded search?** It searches previously browsed snapshot locations, with explicit All Browsed Locations / browsed-folder scope labels. This is useful local discovery, not a whole-computer claim.
4. **Inspect consumption and drill down like WizTree?** Capacity, logical/allocated aggregation and treemap hierarchy exist. Global recursive largest-file ranking is absent from the current immediate-entry list implementation.
5. **Actionable storage?** File-tile reveal exists, but context-target loss must be fixed before trusting operations. Category summaries must not be file objects.
6. **Column View better for hierarchy?** The spatial model is real; comparative user validation is unverified. Search reveal must preserve its strongest navigation behavior.
7. **Tabs/dual panes intuitive?** Typed per-pane state, history, restore and tests exist. Runtime composition and focus need qualification.
8. **Everyday operations without confusion?** Rich operations exist, but hidden browse targets are a material counterexample. File preservation takes priority over styling.
9. **Modern and cohesive?** An existing coherent reference lock exists. Actual screenshots, rather than another token rewrite, determine necessary improvements.
10. **Errors/degraded truthful?** Unknown allocation and partial aggregates have explicit states. Root-only snapshot coverage must not claim full-volume completeness.
11. **Discover differentiators first run?** Home actions, chrome Fast Search and sidebar Search/Storage exist. No invented onboarding or usability claim replaces baseline inspection.
12. **Replace Explorer daily?** Not established: the confirmed signature-journey defects, interoperability qualification and external release requirements prevent a product-complete claim.

## Refero research and native adaptation

Tools actually used: `refero_search_styles` (neutral productivity/Linear, Things and shadcn angles), `refero_get_style` (Things and shadcn), `refero_search_screens` (file search and storage usage), `refero_search_flows`, `refero_get_screen` and `refero_get_flow`. Live tools were accessible; findings are not invented from memory.

- Primary: [Things](https://culturedcode.com/things/) style `0796cd74-edc2-4e12-9c71-25fac02a1cb2`: cool neutral canvas, system typography, restrained blue interaction, small functional control radii. Preserve existing FastFiles token roles; reject large marketing cards and hero spacing.
- Bounded craft: [shadcn/ui](https://ui.shadcn.com) style `c14c0a94-1037-449e-bf5b-4cb972656ac7`: thin borders, compact alignment and minimal elevation. Do not transplant its web pill geometry or branding.
- Product hierarchy: [Skiff search screen](https://refero.design/pages/9d3fab02-94fb-4398-9f05-9509ba3c66fb) emphasizes filename first and location second. FastFiles adapts that hierarchy to persistent native virtual results, with location taking precedence over secondary size/time metadata.
- Journey: [Skiff flow 1926](https://refero.design/flows/1926) shows folder -> global search -> no-results -> matches with locations. Its documented exit is the result modal, so it does **not** establish FastFiles reveal correctness. FastFiles adds an explicit selected-folder/file postcondition.
- Storage search surfaced capacity summaries, tables and contextual details (including [storage file details](https://refero.design/pages/83f7187d-905c-4a06-aae0-d3ae71e74ea0)); only the narrow principle of selection-bound actions is used. No recursive-treemap finding is inferred from those references.
- Additional live searches covered hierarchical file tables/breadcrumbs, shortcut/settings surfaces and loading/error/retry states. [Kitchen folder hierarchy](https://refero.design/pages/ffd5440d-aacc-4cdd-b466-4b35826d872a) reinforces consistent breadcrumb/sidebar orientation; [Linear command choices](https://refero.design/pages/7be65bdd-5156-44c6-94c1-61cd115b0d83) reinforces action/shortcut alignment. These bounded patterns support synchronized shell state and keyboard reachability; their branding, banners and onboarding are not copied.
- [Skiff move flow1919](https://refero.design/flows/1919), retrieved in full, confirms selection -> destination -> changed source listing and truthful success. FastFiles preserves native operation handlers rather than copying a web picker; visible selection identity must survive sorting until the operation is explicitly invoked. Error/retry screen search returned compact explicit failure/retry patterns, not evidence of file-operation recovery correctness.

Reference lock: existing Native Precision Workspace remains authoritative. Preserve Segoe UI, cool neutral surfaces, blue active/focus states, filename-first density and non-color focus cues. Borrow only Skiff's filename/location hierarchy and shadcn alignment discipline. Assets remain native code/icons. Reject decorative cards, oversized helper copy, wholesale layout copying and style averaging.

## Runtime and remediation evidence

See [runtime and review evidence](../../openspec/changes/original-vision-journey-recovery/evidence/runtime-and-review.md). Search from Home passes exact hierarchy/selection/visibility and replacement-identity assertions; the preserved baseline fails the stronger tests. Native Storage now passes real semantic selection, identity-preserving sorting, exact Copy Path/Cut targets with independent sentinel hash unchanged, category refinement retained in Treemap, contained geometry and actual tile Invoke. Typed Home and filesystem restoration pass real close/relaunch cycles. GameInputSvc caused earlier recorded pre-interaction failures; later fresh lifecycles pass without service changes or input-policy bypass. Explorer drag-in remains skipped/unqualified.

Baseline captures16/16 at actual120DPI and first expanded after20/21 remain retained; the no-results harness uses verified real typing after its earlier failure. Private local-only Search screenshots expose the legacy six-column viewport defect and verify corrected location/metadata with no horizontal scrollbar. Clean be8cddc full E2E8/8 passes. Semantic catalog21/21 initially concealed a missing palette image; visual review rejected it and found native sibling paint occlusion. The fc2fab7 catalog21/21 visibly includes the repaired palette, and its full E2E8/8 includes consumed outside dismissal. Settings footer clipping was then reproduced by actual bounds and repaired; final footer-aware catalog certification is pending. Privacy-redacted images alone never certify row text geometry.

## Visual and accessibility audit coverage

| Area | Evidence / assessment | Remaining boundary |
|---|---|---|
|Light and dark|Native Home, Search, Storage, Settings, palette and Gallery captures; shared tokens preserved|Final footer-aware catalog refresh pending|
|High Contrast|Existing system-color fallback and overlay gates retained; software theme/style tests|Live High Contrast profile unverified; no OS preference mutation|
|Typography / spacing|Segoe UI hierarchy, filename-first results, separate context/metadata bounds; actual120DPI review|Comparative user testing unverified|
|Icons / row height|Native glyph/icon cache retained; measured result rows and shared density controls|Physical icon/render matrix beyond current desktop unqualified|
|Selection / focus|Search replacement rejects ordinal reuse; Storage sort retains identity; linked-column reveal exposes focused leaf; non-color text/shape cues retained|Broader native root-focus role audit remains separate from passed custom-provider scopes|
|Hit targets|Actual Storage headers/rows, palette dismissal and Settings Cancel pointer journeys|No touch-device qualification|
|Modal positioning|Settings reserves client size plus measured caption/frame; footer bounds red/green|Additional monitor-placement and physical scale matrix unqualified|
|Command palette|Real native choices paint above workspaces; outside click dismisses without background navigation|Further independent review unavailable after workspace-credit exhaustion|
|Settings|Actual footer containment and Cancel click pass; six typed pages and draft semantics retained|Native host projected footer controls without the expected Button role; role coverage not claimed complete|
|Status indicators|Browsed coverage and current-folder ranking labeled truthfully; no-match explains degraded recovery|Whole-volume production index coverage remains externally unqualified|
|Responsive widths|Native catalog620/800/1280/1600 requested window widths, including narrow Home and Gallery|Unmeasured physical displays not inferred from labels|
|DPI|Runtime measured120DPI; pure geometry tests96/120/192DPI; positive subpixel bounds conservatively enclosed|Synthetic tests do not certify physical96/192DPI desktops|
|Reduced motion|Existing SystemAnimationsEnabled/instant-snap logic preserved and software-tested|Live OS reduced-motion profile unverified|
|UIA structure|Distinct Storage/Search identities, real rows/headers/actions, stale/lifetime safety and hidden Browse/Home retirement; actual native journeys|Native-role/root-focus gaps outside reviewed seams remain explicitly unverified|

Debug/release-test builds, Analyze and ASan checks are tracked separately from hosted CI and native visual qualification. The current main-based product branch does not claim PR14/15 scale-performance evidence as its own. No pillar is called product-complete while a signature journey remains unqualified.

## Native conflict decision gap — 2026-10-26

Actual source-bound120DPI collision audit finds painted Replace absent from native accessibility, white dark-mode background and unreadable checkbox label. Source also omits Tab stops. Priority5×5×3×5÷2=187.5; Pillar D/E remain IMPLEMENTED_BUT_UNPOLISHED. Native Cancel pointer fallback preserves incoming/destination hashes; no full conflict pass is claimed. The isolated task2.10 slice restores native labels/focus/theme and adds a real eight-decision light/dark content regression. See [red evidence](../../openspec/changes/original-vision-journey-recovery/evidence/conflict-actions-20261026.md); native green is pending.

Conflict source75d6d0e now passes8/8 real light/dark decisions, complete Tab traversal and exact content/sentinel assertions; both actual images reviewed. Broader E2E75 remains7/8 (Home-close failure). A separate demonstrated harness Alt+F4 syntax defect is repaired and headless-tested with structured route/exit evidence; native restore qualification is pending, and original attribution remains unproven. No full lifecycle or product-readiness approval is inferred.

Native conflict f106541 and combined63ffa59 each pass8/8 decisions plus full native E2E8/8. Real pointer/keyboard choices, complete Tab cycle, exact content/sentinel checks and measured120DPI light/dark images verify the scoped remediation. The close fallback has non-destructive headless red/green and serialized native route evidence; all observed current closes use UIA, while original75 Home-close attribution remains unproven. Pillars D/E remain IMPLEMENTED_BUT_UNPOLISHED because broader operation/display/drag journeys are open. See [qualified conflict evidence](../../openspec/changes/original-vision-journey-recovery/evidence/conflict-actions-20261026.md).

## Trustworthy file operations — current evidence

Clean implementation 9195788352a7bdba1996e42bda3c48570b794037 passes seven real, marker-owned journeys: exact three-file Unicode copy/move; Recycle Bin positive control and native Undo; permanent-delete default Cancel and bounded observed exclusion; exclusive-lock error with manual retry; and cancellation after actual partial copying. The full run stops at 207 of 512 files, the focused run at 91 of 512. All source and remaining destination names/hashes plus unrelated sentinel are verified. Both owned lifecycles clean up completely.

The actual raw-code-only error now explains a source sharing violation and suggests closing the app using the file while preserving its original path/HRESULT. Mixed stopped batches visibly identify errors. Native child Escape routes through the canonical shell handler after modal/search/edit precedence. A non-destructive posted-message regression reproduces progress saturation and lost terminal events; bounded progress publication repairs that producer without changing cancellation callbacks or operation policy. Independent source and actual receipt/image review find no P1/P2 within those scopes.

Selected Debug and unsigned release-test each pass 126/126; MSVC analysis passes. This does not qualify restricted live reaper, privileged service/signing, physical display matrix, Explorer drag-in or whole-product readiness. The 4,096-item native cancellation attempts remain unqualified; the passing fixture has 512 items and the same bounded 64 MiB payload.

Ordinary current-source E2E is **7/8**, with **Home-close failure** despite successful UIA Close and no observed exit within the unchanged 10-second deadline. File operations, Find/reveal, Storage, preview and the other lifecycle checks pass. Source inspection independently finds OLE destruction ordering, pending icon backlog and async-future admission hazards; their correction and actual shutdown phase qualification are separate work. The inherited PR #21 footer wraps in its current capture, as separately addressed by PR #20; no new footer pixel approval. Pillars D/E remain IMPLEMENTED_BUT_UNPOLISHED. See [current source, before/after images and exact limits](../../openspec/changes/original-vision-journey-recovery/evidence/file-operation-feedback-20261026.md).
