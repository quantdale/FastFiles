# Original-vision journey recovery evidence

## Source and isolation

Product baseline `a26fe609439a18c128b2a2e557bc7eb2534fa128`; first implementation checkpoint `908b3f14` (derive full SHA from Git). Main-based product work does not include PR14/15/16. Acceptance successor `949fbb9b0113a8ff42453a9e268875aa259e8851` is separately pushed as draft PR17, stacked on PR15. Existing PRs remain unmerged.

## Red-capable native journeys

The preserved baseline binary set is under ignored `build/baseline-native/`, built clean at a26fe609 before product edits. It is not relabeled as a current build.

| Journey / command | Observed result | Identity and limitation |
| --- | --- | --- |
| Baseline `Run-FastFilesE2E.ps1 -Exe ./build/baseline-native/src/ui/FastFiles.exe -Filter search -ArtifactsDir build/original-vision-20261025/search-before` | FAIL exact selected file/hierarchy assertion after Search closes | Test initially added only the stronger Home reveal postcondition |
| Same baseline, final typing/selection harness; `search-baseline-final-harness` | FAIL `selected-replacement-retarget`: beta inherited alpha's selected ordinal | Discriminates unsafe result replacement with actual UIA selection; no file mutations |
| Product diagnostic runtime, `search-native-focus` | PASS1 / FAIL0 / SKIP0 | Home -> exact/partial/no-match -> selected alpha replacement -> beta not selected -> complete hierarchy -> exact selected visible file; source at development checkpoint before final certification |
| Product Storage, `storage-after` | FAIL `storage-command-target` | Three rendered storage items, but native owner-data UIA tree exposed only Pane7201 and empty header. This is an accessibility defect, not an accepted skip or action-target pass |
| Baseline visual catalog `before` | 16 captures /0fail, actual120DPI | Light/dark,620/800/1280/1600 widths. Old nominal100% mismatch retained; actual scale125%. Screens inspected include Home and Column View. Some personal known-folder content is privacy-redacted; white redaction rectangles are not blank production controls |

No successful screenshot or panel dismissal substitutes for actual selected destination correctness.

## Focus diagnosis and repair

First strengthened search replacement tests failed deterministically after alpha selection. The foreground helper unconditionally injected Alt/Escape even if already foreground, closing Search. New driver regression executes the actual PowerShell function body against substituted native calls: old source fails four discriminating checks; updated full headless driver suite passes. Escape now occurs only for an observed target-owned active menu after foreground verification.

The remaining failure was separately traced: command log showed `selection.select-all;accepted=false`, query stayed `partial-alpha`, and observed keyboard focus was `SysListView32`. The stock Edit provider did not transfer focus from the selected result. The helper now requires query keyboard focus before editing, uses the real Ctrl+F command when SetFocus fails, then verifies focus before sending text. Generic default-action invocation is no longer treated as text focus. This preserves real typing and exercises the intended keyboard journey. Intermediate failure trees/logs are retained under `search-after`, `search-after-focus`, `search-after-foreground`, `search-focus-trace`, `search-verified-focus`.

## Independent adversarial review

Fresh native read-only agent `/root/independent_acceptance_review` reviewed source repeatedly, without builds or UI. Material findings were fixed:

- Scope/global commands no longer require existence of an unrelated stale file; selection-consuming commands alone validate live paths.
- Search clears native selection before replacing result models; Storage captures/restores real row identity across sorting and same-scope refresh.
- Context menu and palette item commands bind displayed surface and selected paths; nested refresh cannot silently retarget.
- Keyboard Search menus anchor to real Search geometry; Storage preserves screen coordinates and keyboard sentinel and never falls back to hidden Browse after cancel.
- Category summaries group by stable category ID; fallback filter ID is chosen distinctly from arbitrary configured IDs. Custom Other/other collision regression added.
- Missing engine/snapshot clears old Storage rows before scope reuse; stale A rows cannot become B paths.

The reviewer found no additional P1/P2 after the final stale-row branch repair was proposed; final source/runtime approval still must be bound to the final implementation. Review is not a product-complete or merge-ready claim.

## Local test observations

- Focused diagnostic regressions7/7 PASS: navigation, command, category filtering, storage visibility, shortcut routing, Search UIA, metadata geometry. Geometry includes96/120/192DPI and narrow bounds.
- Full diagnostic build PASS, but diagnostic CTest is not portable trust certification: six negative-pin assertions failed because the explicit non-shipping unsigned bypass compiles into ffsetup. Direct WinTrust rejection passed. No root stores or trust controls changed.
- Normal Debug build PASS; selected suite124/126 at the first run. Two failures were fake-engine session-pipe collisions with concurrent native UI (`ERROR_ACCESS_DENIED` from intentional first-instance pipe exclusivity). After normal UI/engine teardown, focused handshake/active-path rerun2/2 PASS. Initial failures remain recorded and are not silently erased.
- The live reaper test is deliberately not executed under the owner's restriction. It is excluded explicitly by `-E '^ffverify_process_reaper_ps_tests$'`; no full127/127 claim is made. The first diagnostic suite was interrupted before that test; subsequent matrices use explicit exclusion.
- Current-source full normal Debug/release-test and Analyze results will be updated after the semantic Storage follow-up; do not infer those results from earlier checkpoints.
- Strict OpenSpec at the first product delta:92/92 PASS. Source diff/PowerShell parse checks pass at each checkpoint.

## External boundaries

PR17 CI run40174345016: six failed jobs, zero steps. Annotation explicitly reports failed account payments/spending limit. Inspected once, no rerun or billing change. Production signer/pins, clean-host install, real privileged NTFS scenarios, raw-volume completion bound and physical display/High Contrast matrix remain separate unqualified boundaries. Existing exact-SHA PR15 expensive performance/oracle evidence is retained without repetition.

## Subsequent qualification frontier

- Clean implementation1208cb6449090ecc585d8f0050b7f2e36bf50a79: normal Debug126/126 in424.32s and release-test126/126 in231.41s, explicitly excluding restricted ffverify_process_reaper_ps_tests. Both include portable trust rejection and build-identity binding. Final Analyze74/74 warning-clean; strict OpenSpec92/92. Combined simulation6e060fbb34569ac9d21b964cff84d36743f350f3 builds479/479 and focused11/11 passes, including real non-destructive identity fixture. These exact-source results precede the two bounded successor corrections below.
- Search exact-tree native journey at84564bd passes1/0/0, with Home, replacement selection, full hierarchy, exact selected path and visibility. Corrected raw local-only screenshot inspected at measured120DPI: location and metadata visible, no horizontal scrollbar. Raw personal-path capture stays ignored. Full E2E at1208cb6 passes1 and fails7 before intended interaction due external GameInputSvc foreground owner; initial failures retained.
- Outward physical enclosure1208cb6 is independently source-reviewed and red-capable (three headless geometry failures before, focused1/1 after). Native category-treemap-red progresses through all three tile geometry assertions successfully, then fails a NEW category continuity assertion: Document filter shows small.bin, storage-note.txt and Document in Treemap. Category activation had only updated the list. Passing the same category into the existing Treemap filter fixes it; native category-treemap-green passes1/0/0 including target/sort/sentinel/category/bounds/tile Invoke.
- Home restoration successor: a disposable production-format saved Home configuration exposed no Analyze Storage action before repair. The startup resolver returned Home but initialization only applied filesystem locations. Canonical NavigateToLocation now applies Home after engine startup. Public native Home Invoke reaches actual Storage and rejects the retained hidden Home action; a strengthened cold-restore test passes1/0/0 across real Nested close/relaunch and Home close/relaunch without address intervention on restoration. One transient probe hit a reserved PowerShell variable name; corrected before the verified run, never counted as a product failure or pass.
- Additional review capacity returned workspace-credit exhaustion. No exhausted-provider retry loop. Independent source approval covers1208cb6; the two bounded successor calls and their native red/green/restore assertions are not represented as independently reviewed.

- Integrated Home/visibility/Search/Storage semantic implementation builds58/58 and focused UIA/geometry7/7 passes. Independent final source review explicitly approves88c044/5456 and the integrated viewport/visibility/Storage additions with no remaining P1/P2 in that reviewed scope. No independent runtime rerun is implied.
- Corrected Storage helper now passes exact target, sorting, sentinel and category assertions. The scenario then fails a genuine geometry boundary: a Treemap tile reports native Rect.Empty with valid group bounds. Source diagnosis: one43-byte sibling retains its correct proportional subpixel layout beside3MiB; shared physical-coordinate truncation collapses its bounding extent to zero. Preserve accurate area/tiling and test assertions; a bounded outward pixel-enclosure repair is being validated separately. Full Storage scenario remains FAIL until that repair passes.

- Additional isolated source milestones:669fc9e moves Search list queries/actions/geometry/focus to its owning thread with the original identity;88c044a closes dispatch and invalidates lifetime before message draining. Visibility1b841c8 ties Browse/Home fragment lifetime to actual presentation. Home5456f1 moves Invoke/Clear through closed identity/lifetime requests to the owner thread; no worker-thread fallback. Parent integration retains shell transition synchronization and removes obsolete index-only Search activation messages. Focused provider/geometry7/7 and Search follow-up5/5 pass; final integrated validation follows.
- Storage current runtime reached real semantic rows and eight headers. First current test falsely reported missing small.bin because its helper called a non-exported private pattern function. Public pattern checking repaired that harness defect. Next run passed selected-row identity, sort relocation, Copy Path and Cut clipboard target with independent sentinel hash unchanged, then falsely failed category recovery because the helper threw on an expected absent row. Failure tree shows only storage-note.txt and Documents filter, proving refinement occurred; helper now enumerates real direct list children and returns null for absence. No failed run is relabeled PASS; full corrected scenario must rerun.
- A foreground-independent Home probe was inconclusive: fresh sessions start at This PC, so Analyze Storage was not present. No Home action or Storage interaction occurred in that probe; owned-process teardown left no residue. It does not qualify a Storage journey.

- Storage semantic follow-up imported from isolated worker commit62f3792136b976bb3afda84c5d4a27d6bb7a6986. Strict compilation of provider/Storage/test translation units and focused normal Debug provider test1/1 passed in the worker; parent diagnostic build and focused5/5 pass. Source review repaired original-identity dispatch, COM lifetime/refcount, category identity and root focus marshaling. Live Storage test is now blocked at scope navigation by titleless GameInputSvc foreground HWND21762224/PID7872 in session1; the earlier private viewport capture met the same boundary. No changes to this unrelated service, no injected input after failed foreground verification, no Storage interaction PASS claim.
- The first after catalog captured20/21 (including Gallery) at observed120DPI; `search-no-results` failed because the older ValuePattern path did not replace the query. It now uses the same verified typing helper as functional E2E. Physical other-DPI/High Contrast/reduced-motion matrices remain unqualified.
- Private local-only Search capture (ignored build/private-review, not committed) showed metadata offscreen beyond the rendered viewport despite nonoverlapping layout math. Six old native report columns supplied an oversized row RECT. Search now has one viewport-width native column; all fields remain in the semantic provider and sort controls. Rendering bounds clamp to the actual client viewport and refit after result count/resize changes. Final corrected screenshot remains blocked by foreground ownership.
- Full MSVC Analyze build445/445 completed warning-clean before the semantic Storage follow-up; changed UI/provider sources require incremental final analysis. ASan parser/IPC/scanner4/4 passed67.27s; those source subtrees are unchanged by UI work.
- Serialized release-test125/126 passed, with only build-identity binding failing because HEAD moved to908b3f1 during execution while binaries still embeddeda26fe609. That rejection is correct. Refresh identity-bearing binaries after the final source checkpoint and rerun the identity gate; do not call this a126/126 pass or weaken provenance.
- Disposable local integration simulation: PR16 fast-forward -> PR14 (test-registration tail conflict resolved preserving both sets) -> PR15 -> PR17 -> product908b3f1 (workstate/lifecycle conflicts resolved preserving both histories). No C++ conflict. Local simulation d53fc5756a39be2d5958bb00a9f41cd9bfb58473 is not pushed or merged into main and still needs the final semantic follow-up before qualification.
