## Why

FastFiles has 23 open, mostly stacked PRs containing search, navigation, storage, file-safety and verification improvements, but no single current-source candidate qualified as a daily-driver application. Consolidate accepted work into one independently reviewed native product candidate and close its integrated user journeys without confusing branch-level tests, hosted certification or production-release readiness.

## What Changes

- Use `integration/fastfiles-final-product-20261029` in its dedicated worktree as the sole consolidation destination, initially based on freshly fetched `origin/main`. This proposal adds planning artifacts only; no feature integration or product implementation is performed during propose.
- Establish a machine-readable integration ledger covering every live open PR and relevant unpublished local branch: pinned inputs, ancestry, unique commits/files/tests, predecessor OpenSpec contracts, reviews, conflicts, validation and disposition. Reconcile changed inputs before publication.
- Integrate by ancestry and semantic responsibility, with progressive Debug/release-test and affected-test gates; preserve compatible fixes and obtain independent review of material conflict resolutions. Treat the existing local integration simulation as evidence and an additional input, not an already-certified candidate.
- Freeze a finite acceptance matrix for first launch, search/reveal, Column View, tabs/dual panes, Storage, file operations, recovery, accessibility and external Explorer interop transfer. Repair demonstrated material defects in these existing journeys before certification; do not expand into unrelated features.
- Qualify rename/exclusive creation, execution-time conflict consent, generated transfer results, request-scoped apply-to-all, Undo/Restore and cancellation against owned filesystem fixtures in the actual integrated application. Worker, UIA and screenshot evidence remain complementary rather than interchangeable.
- Run the available current-source certification ladder, selectively remeasure affected performance, preserve initial failures, and bind results to executable/build inputs. Restricted, unavailable or skipped tests remain explicitly unqualified.
- Reconcile predecessor tasks and lifecycle through evidence rather than bulk closure. Publish one consolidation PR when its candidate is ready; merge only by ordinary policy-compliant procedure with all required gates satisfied. Account for original PRs without falsely describing closed-without-merge work as merged.

## Capabilities

### New Capabilities

- `integrated-product-acceptance`: one auditable contract for source consolidation, bounded daily-driver journeys, independent review, candidate certification, provenance and policy-compliant integration. This is a cross-change acceptance capability, not a replacement file manager architecture or a new runtime subsystem.

### Modified Capabilities

None at proposal time. Existing feature requirements remain authoritative. Integration and repair must preserve them; any genuinely changed product requirement must receive an explicit reviewed delta before implementation, not be silently redefined by this umbrella.

## Impact

- Planning/evidence: this change, `openspec/lifecycle.json`, `docs/agent/WORKSTATE.md`, the eventual integration ledger, five-pillar alignment report, native acceptance report, certification/performance/review records and source provenance manifest.
- Apply-time code/test seams: index/store/search; `WindowShell`, `ColumnView`, `SearchPanel`, `NavigationChrome`, `StorageAnalysis`, file operations; CMake test registration; native E2E/UIA/visual verification. Only accepted integration changes and demonstrated journey/gate repairs justify edits.
- Existing contracts include `large-index-lifecycle-and-search-scalability`, `durable-cold-search-index-and-hybrid-residency`, `original-vision-journey-recovery` and the bounded safety/verification successors. Some exist only on incoming refs until integration; their immutable intake references are documented in `evidence/explore-findings.md`.
- No new dependency, IPC command, privileged filesystem primitive, trust policy or installer behavior is proposed. Foundation decisions D1–D6 in `openspec/changes/establish-architecture-foundation/design.md` remain binding. Additive FTS5 migration and fail-closed older-schema behavior from incoming work must survive integration.

## Goals

- One clean, current candidate containing every accepted change, with no known accepted fix lost and every excluded/superseded input justified.
- A finite, source-bound acceptance result for each essential user journey and each of the five original product pillars.
- A truthful owner handoff separating implementation, integration, local qualification, hosted qualification and release-only prerequisites.

## Non-goals

- A framework rewrite, speculative feature expansion, new privileged access, production installation or release deployment.
- Force pushes, shared-history rewrites, discarding unrelated modifications, automatic branch deletion, or changing billing/protection/security settings.
- Treating browsed/pinned degraded coverage as whole-computer coverage, synthetic DPI as physical-display certification, or selected CTest totals as unrestricted full-suite results.
- Reopening unchanged expensive benchmarks or the deferred production-signing/clean-host campaign without a new input or separately authorized environment.

## Open questions

- Which local-only integration edits are unique, superseded or content-equivalent to published heads? Intake must resolve this before accepting or dropping them.
- Do the cold-restore UIA omission, preview terminal-state intermittent and in-panel search-refresh gap persist on the frozen candidate? Bounded diagnostics determine repair or explicitly unqualified disposition.
- Can restricted process-reaper qualification and hosted jobs execute within the permitted environment? If not, they remain required-but-unavailable and cannot support a certification or merge claim.
- Which mandatory branch checks/reviews apply? The protection/rules API returned HTTP 403 during explore; unknown policy is not evidence of no policy.
