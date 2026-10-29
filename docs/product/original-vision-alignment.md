# FastFiles original-vision alignment

The July 30, 2026 product thesis is **find, navigate and understand your entire filesystem instantly**: WizFile discovery, Finder hierarchy, WizTree storage intelligence and trustworthy daily file operations in one native Windows application.

Initial audit source: fresh main `a26fe609`. PR14/15 performance evidence is a separate unmerged track, not evidence that main already contains those optimizations. Independent source audit and live Refero research completed 2026-10-25. Runtime results are recorded below as they become available; source inspection alone never establishes a verified user journey.

## 2026-10-28 safety frontier

A fresh independent review identified a possible collision between ordinary transfer results and later approved replacements. The owned native Move fixture confirms incoming data loss on PR32 source12b: two successes, zero errors, earlier incoming bytes gone after a vacated approved name is reused by Shell. This data-safety defect takes priority over compact-palette visual work.

The successor `protect-generated-transfer-results` now refuses overlapping replacement groups before mutation and retains all incoming replacement sources. Focused native Copy/Move/holding-folder Restore controls, adverse vacated names and grouped refusals pass with exact bytes/counts/cleanup; pure namespace and previous cancellation tests pass. Fresh independent source review found no additional P1/P2. Broader committed-source/combined qualification remains pending. Pillar D remains IMPLEMENTED_BUT_UNPOLISHED; this is not Explorer drag, Recycle Bin Undo, alias/reparse or missing-callback certification. Evidence: `openspec/changes/protect-generated-transfer-results/evidence/native-20261028/`.

## Current combined-product checkpoint — 2026-10-28

The original scorecard below records the initial main-based audit; this later checkpoint applies to unmerged local integration `90e6cdf4ecc0927672b96bedfb14edefbf966a08`, with separately reviewable draft successors. No pillar is rated fully aligned on source inspection alone.

| Original pillar | Current classification | Observed journey and evidence | Material remaining deviation |
| --- | --- | --- | --- |
| Find across the computer | PARTIALLY_IMPLEMENTED | Actual combined E2E searches from Home, checks exact/partial/no-match/supersession and reveals the exact selected path in its visible hierarchy (90e E2E8/8). The degraded UI labels browsed scope. Prior index correctness/performance evidence is separately tied to PR15/17 sources. | Degraded coverage is browsed/published locations, not whole-computer indexed discovery. Real production signed service and WizFile usability parity remain unverified. |
| Understand storage | PARTIALLY_IMPLEMENTED | Drive capacity, known-item ranking, incomplete coverage, logical/allocated sizes and Treemap navigation are implemented. Prior PR26 native narrow/wide ranking/reveal and focused selector journeys passed; 90e E2E reached a usable Treemap tile. | Whole-volume complete storage inventory and WizTree parity remain unverified; partial results must not imply full coverage. |
| Navigate hierarchically | IMPLEMENTED_BUT_UNPOLISHED | Linked columns, keyboard selection, exact Search reveal, tabs and dual-pane focus have native evidence. 90e navigation and cold restore passed, with three normal closes. | Finder comparative usability and physical display matrix remain unverified. Prior b0cf cold restore showed the file but the UIA tree lacked ColumnView; a later different-source three-client observation and E2E pass did not determine its cause. |
| Act and trust file operations | IMPLEMENTED_BUT_UNPOLISHED | Real 90e create/rename/copy/move/recycle operations passed; owned conflict/cancel journeys have separate native receipts. Draft PR33 prevents a reproduced same-request incoming-file overwrite and records stopped replacements; actual Copy/Move/holding-folder Restore controls pass. | Explorer-to-FastFiles drop delivery, multi-item Recycle Bin Undo, root-callback uniqueness and filesystem alias/reparse identity remain unqualified. The original failed data-loss fixture remains preserved. |
| Complete, cohesive native product | IMPLEMENTED_BUT_UNPOLISHED | Draft PR34 fits the real palette to results; 44 actual keyboard/UIA observations across three window widths, both themes and densities passed at measured120DPI, with independent visual review. Native Home, Search, Storage, Settings, status and accessibility improvements remain separately evidenced. | Some screenshots were partly obscured by an external overlay; intended-color fidelity, physical High Contrast, other DPI and reduced-motion matrices remain unverified. First-time discovery and everyday Explorer replacement are not yet established. |

The owner can find entry points for Fast Search and Storage from the shell without documentation, but broad first-run usability has not been independently demonstrated. Degraded search is useful within explicitly labeled browsed scope; it is not whole-computer search. The product is not yet certified as a complete Explorer replacement. Local tests and actual screenshots do not establish hosted CI, production signing or privileged NTFS operation. The 2026-10-28 draft PRs are isolated and unmerged.

Priority remains data safety first, then truthful result/evidence reporting, reliable search-to-location and storage/column journeys, and native visual/accessibility coherence. The current verified transfer and palette work changes those journeys; it does not close unrelated release, physical-display or Explorer drag boundaries.

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
2. **Immediately navigate from a result?** The repaired native Search journey now passes Home reveal, full hierarchy, exact selected path and physical visibility. PR19 additionally repairs public Column View action reconciliation; all changes remain unmerged.
3. **Useful degraded search?** It searches previously browsed snapshot locations, with explicit All Browsed Locations / browsed-folder scope labels. This is useful local discovery, not a whole-computer claim.
4. **Inspect consumption and drill down like WizTree?** Capacity, logical/allocated aggregation and treemap hierarchy exist. Global recursive largest-file ranking is absent from the current immediate-entry list implementation.
5. **Actionable storage?** Native tests now verify visible command targets, sorting identity, category refinement, matching Treemap filter and tile reveal. Global recursive ranking and whole-volume coverage remain separate gaps.
6. **Column View better for hierarchy?** Linked columns and exact Search reveal work in native fixtures. PR19 fixes wrong-target accessibility actions, offscreen focus and cross-pane paint spill with unmasked before/after review. Comparative usability superiority remains unverified.
7. **Tabs/dual panes intuitive?** Native typed Home/nested restore and narrow dual-pane focus/independent scrolling now pass. Comparative intuitiveness and richer cross-pane operation journeys remain unqualified.
8. **Everyday operations without confusion?** Hidden-target routing is repaired. Native create, rename, copy, move and recycle pass on owned fixtures; conflict, permanent-delete, cancellation and retry UI journeys still need qualification.
9. **Modern and cohesive?** Actual screenshots drove palette, Settings footer, search context and pane-paint fixes within the existing reference lock. Narrow status text is the active sibling slice; overall polish is not declared complete.
10. **Errors/degraded truthful?** Unknown allocation and partial aggregates retain explicit states; root availability now says completeness unverified. Default degraded operation remains usable and no whole-volume or signed-service coverage is claimed.
11. **Discover differentiators first run?** Home actions, chrome Fast Search and sidebar Search/Storage exist. No invented onboarding or usability claim replaces baseline inspection.
12. **Replace Explorer daily?** Not established: repaired signature journeys are locally verified, while remaining operation/interoperability qualification, whole-computer production indexing and release requirements prevent a product-complete claim.

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

## Current five-pillar evidence update — 2026-10-26

These are unmerged successor results, not a claim that main already contains them.

| Pillar | Current quality assessment | Verified improvement | Remaining deviation |
| --- | --- | --- | --- |
| Find | PARTIALLY_IMPLEMENTED | Exact Home/search/reveal and no-match/superseded-query native journey; visible location/metadata | Degraded scope is browsed/pinned; privileged whole-computer coverage and comparative WizFile usability unqualified |
| Understand | PARTIALLY_IMPLEMENTED | Action targets, category-to-Treemap scope, drill-down, fractional UIA bounds and truthful local scope | Recursive global ranking and whole-volume production aggregates unqualified |
| Navigate | IMPLEMENTED_BUT_UNPOLISHED | Original-identity actions, real owner focus, visible child/breadcrumb, independent narrow panes; strict native two-mode checks | Comparative Finder usability, full keyboard/display matrix and remaining surface polish |
| Act | IMPLEMENTED_BUT_UNPOLISHED | Native create/rename/copy/move/recycle and repeated clean lifecycles | Explorer drag skipped; native conflict/permanent-delete/cancellation/retry journeys not qualified |
| Complete/cohesive | IMPLEMENTED_BUT_UNPOLISHED | Native palette visible, Settings footer contained,21-scene light/dark120DPI catalog, actual focus/paint corrections | Narrow status wrapping under active sibling remediation; physical HC/other DPI/reduced-motion and native role gaps remain |

New high-impact findings: resolve-to-ordinal action race (impact5×confidence5×frequency4×executability5÷risk2=250); focused child offscreen/cross-pane paint spill (4×5×4×5÷2=200). Both are remediated and validated at PR19 implementation93cd2b6. Normal selected Debug126/126 and release-test126/126 explicitly exclude the restricted reaper; native E2E8/8, strict focus normal/narrow PASS, catalog21/21. Compact exact-source evidence and actual screenshots are in `openspec/changes/column-uia-identity-dispatch/evidence/validation.md`.

One narrow probe initially confused pane HasKeyboardFocus with child-row focus; the provider correctly reports focus on the child. Corrected row-based assertions preserve the failed observations. More importantly, actual pixels disproved a semantically passing dual-pane frame: missing paint clips let one pane cover the other. Fixed clipping and frame offsets were re-inspected in raw and exact-source captures. Masked screenshots alone never establish focus quality. No new independent reviewer was available; no old source approval is transferred to PR19. Product-level Explorer replacement readiness remains unproven.

## Narrow native status remediation — 2026-10-26

Actual120DPI screenshots exposed clipped second lines in the engine chip and long-path footer. Score4×5×5×5÷2=250. Immutable implementationcd1f429 uses cached one-line DirectWrite layouts, native ellipsis and display-only line-break flattening; detailed model/path values and multiline recovery formats remain intact. Compact degraded/unavailable labels preserve meaning, and transient completion/recovery notices precede long paths. Native four-frame Settings-driven light/dark/normal/narrow captures and E2E8/8 pass; release-test126/126 selected passes. Initial Debug125/126 had a stale build identity; rebuilt binding1/1 passes, not a fabricated full-green rerun. Standalone Gallery18/21 retains the PR16 theme dependency; combined certification is distinct. See [source-bound status evidence](../../openspec/changes/original-vision-journey-recovery/evidence/native-status-20261026.md). Pillar E improves but remains IMPLEMENTED_BUT_UNPOLISHED; live error/retry, physical display and whole-product qualification remain open.

Combined a3e20ab now passes selected full Debug131/131 and release-test131/131, native E2E8/8 and both physical focus modes; the combined narrow image shows readable status and independent panes. Catalog21/21 passes theme assertions but narrow debug Gallery is visually rejected for clipping. The native modal conflict audit is a separate ongoing gap. See [combined evidence](../../openspec/changes/original-vision-journey-recovery/evidence/combined-qualification-20261026.md). No full-product/Explorer-replacement approval or residency-contract closure is inferred.
## Native conflict decision gap — 2026-10-26

Actual source-bound120DPI collision audit finds painted Replace absent from native accessibility, white dark-mode background and unreadable checkbox label. Source also omits Tab stops. Priority5×5×3×5÷2=187.5; Pillar D/E remain IMPLEMENTED_BUT_UNPOLISHED. Native Cancel pointer fallback preserves incoming/destination hashes; no full conflict pass is claimed. The isolated task2.10 slice restores native labels/focus/theme and adds a real eight-decision light/dark content regression. See [red evidence](../../openspec/changes/original-vision-journey-recovery/evidence/conflict-actions-20261026.md); native green is pending.

Conflict source75d6d0e now passes8/8 real light/dark decisions, complete Tab traversal and exact content/sentinel assertions; both actual images reviewed. Broader E2E75 remains7/8 (Home-close failure). A separate demonstrated harness Alt+F4 syntax defect is repaired and headless-tested with structured route/exit evidence; native restore qualification is pending, and original attribution remains unproven. No full lifecycle or product-readiness approval is inferred.

Native conflict f106541 and combined63ffa59 each pass8/8 decisions plus full native E2E8/8. Real pointer/keyboard choices, complete Tab cycle, exact content/sentinel checks and measured120DPI light/dark images verify the scoped remediation. The close fallback has non-destructive headless red/green and serialized native route evidence; all observed current closes use UIA, while original75 Home-close attribution remains unproven. Pillars D/E remain IMPLEMENTED_BUT_UNPOLISHED because broader operation/display/drag journeys are open. See [qualified conflict evidence](../../openspec/changes/original-vision-journey-recovery/evidence/conflict-actions-20261026.md).

### Native lifetime qualification — 2026-10-27

Pillars D/E remain IMPLEMENTED_BUT_UNPOLISHED. Immutable09761df repairs confirmed COM lifetime, pending-work shutdown and HWND access hazards; actual ordinary native8/8 includes three normal closes and three cleanups. Selected Debug/release127/127 each and Analyze pass; independent scoped evidence review approves. See [shutdown evidence](../../openspec/changes/original-vision-journey-recovery/evidence/shutdown-lifetime-20261026.md). Historical919 Home-close failure is preserved without attributing its cause. Active Shell calls, Explorer drag, physical accessibility/display matrix and release-only boundaries remain unqualified; no daily-driver replacement claim.
## Trustworthy file operations — current evidence

Clean implementation 9195788352a7bdba1996e42bda3c48570b794037 passes seven real, marker-owned journeys: exact three-file Unicode copy/move; Recycle Bin positive control and native Undo; permanent-delete default Cancel and bounded observed exclusion; exclusive-lock error with manual retry; and cancellation after actual partial copying. The full run stops at 207 of 512 files, the focused run at 91 of 512. All source and remaining destination names/hashes plus unrelated sentinel are verified. Both owned lifecycles clean up completely.

The actual raw-code-only error now explains a source sharing violation and suggests closing the app using the file while preserving its original path/HRESULT. Mixed stopped batches visibly identify errors. Native child Escape routes through the canonical shell handler after modal/search/edit precedence. A non-destructive posted-message regression reproduces progress saturation and lost terminal events; bounded progress publication repairs that producer without changing cancellation callbacks or operation policy. Independent source and actual receipt/image review find no P1/P2 within those scopes.

Selected Debug and unsigned release-test each pass 126/126; MSVC analysis passes. This does not qualify restricted live reaper, privileged service/signing, physical display matrix, Explorer drag-in or whole-product readiness. The 4,096-item native cancellation attempts remain unqualified; the passing fixture has 512 items and the same bounded 64 MiB payload.

Ordinary current-source E2E is **7/8**, with **Home-close failure** despite successful UIA Close and no observed exit within the unchanged 10-second deadline. File operations, Find/reveal, Storage, preview and the other lifecycle checks pass. Source inspection independently finds OLE destruction ordering, pending icon backlog and async-future admission hazards; their correction and actual shutdown phase qualification are separate work. The inherited PR #21 footer wraps in its current capture, as separately addressed by PR #20; no new footer pixel approval. Pillars D/E remain IMPLEMENTED_BUT_UNPOLISHED. See [current source, before/after images and exact limits](../../openspec/changes/original-vision-journey-recovery/evidence/file-operation-feedback-20261026.md).

### Discoverable operation cancellation — 2026-10-27

Pillars D/E remain IMPLEMENTED_BUT_UNPOLISHED. Immutable650744f makes actual-worker-state cancellation discoverable through the native palette and prevents the duplicate Shell progress dialog from competing for input. Real selected-command/accepted-dispatch/partial-copy, idle refusal, source/destination hashes, calibrated recycle/Undo/permanent and manual recovery qualify eight owned journeys; conflict8/8 and ordinary8/8 pass. Exact-source selected Debug/release126/126 each pass with restricted reaper explicitly excluded. See [cancellation evidence](../../openspec/changes/original-vision-journey-recovery/evidence/discoverable-cancellation-20261027.md). Standalone inherited footer wrap remains rejected pending PR20 combined review;4096 navigation, Explorer drag, physical accessibility/display and release boundaries remain unqualified. No full Explorer-replacement claim.

## Shortcut safety successor — 2026-10-27

Pillar D/Act remains PARTIALLY_IMPLEMENTED overall. Controlled actual worker exposed a late-planned shortcut destination overwrite and ignored cancellation (256/256 created). Native stream serialization plus exclusive output now preserves existing data and reports failure; cancellation stops between items with actual counts. Real native Load/GetPath verifies produced links without executing them. Focused worker/writer, standalone130/130, safe combined140/140 Debug/release and two focused ASan cases pass with exact source evidence. Evidence: `openspec/changes/prevent-shortcut-creation-overwrite/evidence/qualification.md`. Explorer drag and Alt-drag remain UNVERIFIED; API success does not qualify either journey. High data-loss impact and deterministic proof justify this safety slice ahead of cosmetic work.

## Current bounded safety and native verification — 2026-10-28

Pillar D remains PARTIALLY_IMPLEMENTED overall: shortcut late destinations are preserved at exclusive creation, known cancellation is observed, and native link round-trip is verified. Current combined140/140 each, focused ASan2/2 and exact component evidence are in `openspec/changes/prevent-shortcut-creation-overwrite/evidence/final-certification-20261028/qualification.md`. Current4K directory navigation/palette cancellation425/4096 and Escape20/1024 with all hashes/sentinel/cleanup are qualified; historical4K stall is still preserved on its own source. No whole owned8/8 aggregate, Explorer drop, GUI Alt-drag or multi-item Bin recovery claim follows.

Native verification now enumerates actual chrome directly, bounds Enter retry to the real focused address edit and treats literal bracketed folder names correctly. Actual S/[Source] negative/positive journey passes; the original b868 missing-tab cause is still unknown, and the1024 palette completion race remains failed qualification. Draft [PR31](https://github.com/quantdale/FastFiles/pull/31) preserves these results; verification hardening is not presented as a new application navigation feature.

Pillar E/Enjoy: command palette is IMPLEMENTED_BUT_UNPOLISHED. The measured120DPI idle screenshot has one unavailable result occupying a fixed360DIP list, with a large empty region. The next product wave will fit content while keeping query position/native keyboard/focus/availability semantics and density/DPI geometry stable. Refero Raycast flow10105 and Linear command examples were actually inspected; native Windows tokens remain authoritative. This is planned remediation, not implemented UX. Overall Explorer replacement readiness remains unproven.
