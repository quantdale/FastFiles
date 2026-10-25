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

Reference lock: existing Native Precision Workspace remains authoritative. Preserve Segoe UI, cool neutral surfaces, blue active/focus states, filename-first density and non-color focus cues. Borrow only Skiff's filename/location hierarchy and shadcn alignment discipline. Assets remain native code/icons. Reject decorative cards, oversized helper copy, wholesale layout copying and style averaging.

## Runtime and remediation evidence

Pending current-source native build, focused regressions, E2E destination assertions and screenshot review. Evidence under ignored `build/` is session-specific; durable result summaries and selected compact screenshots will be linked from this campaign's evidence record. Unavailable/failed cells stay explicit.
