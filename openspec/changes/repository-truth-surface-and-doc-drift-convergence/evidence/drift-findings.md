# Drift findings (live audit at baseline `5758bbf`)

All findings revalidated against the live tree; planner examples confirmed
plus negative-space extras. Line numbers refer to baseline.

## F1 — Verifier verb enumeration stale (planner finding 7) — HIGH

- Claim: README.md §Runtime verification ("fixed, non-interactive verb set")
  lists only `build install run diagnose report repair gate list doctor`.
- Authority: `verify/verify.ps1:24` ValidateSet additionally includes
  `status`, `release-readiness`, `release-candidate`.
- False action: an agent believes status/release composition does not exist,
  hand-rolls evidence, or skips required gates.
- AGENTS.md and verify/README.md are correct; drift is README-only.

## F2 — Stale current-campaign claims (planner finding 8, extended) — HIGH

- README.md:157: "The current interactive hardening change is
  `openspec/changes/interactive-ui-runtime-determinism/`" — that change is
  recorded `external-evidence-only` in lifecycle.json; multiple later campaigns
  completed.
- README.md:144: points to the same directory for "the current root-cause
  evidence".
- CLAUDE.md:24: "Current open work:
  `openspec/changes/close-independent-validation-gaps/tasks.md`" — that change
  is `external-evidence-only`; lifecycle projects 0 active-local tasks.
- False action: executor resumes a closed frontier or treats external gates as
  local work.

## F3 — Snapshot publication mechanics stale (planner finding 9, extended) — MEDIUM

- AGENTS.md:24: "publishes an immutable, double-buffered index into a read-only
  memory-mapped section (`Local\FastFiles.IndexSnapshot.<SessionId>`)".
- CODE_INDEX.md:240–242: "`SnapshotPublisher` publishes the double-buffered
  memory-mapped section … `Publish` writes the inactive slot then atomically
  flips `activeSlot`/`generation`."
- CODE_INDEX.md:359–363: data-flow step 2 says Publish "flips a generation in
  the mapped section" and the UI maps "the section".
- Source truth: `SnapshotPublisher.cpp` composes per-generation section names
  (`GenerationSectionName`: `<prefix>.<instance>.<generation>`, prefix from
  `kSnapshotSectionNameFormat` = session-scoped), publishes a canonical first
  generation during `Start`, writes a candidate mapping fully, then
  `CommitGeneration(&header->generation, ...)` before pointer/generation
  handoff; no `activeSlot` exists. Reader (`EngineClient.cpp:246–273`) maps the
  named current-generation section and parses in place after verifying the
  committed header generation.
- Also imprecise: `src/protocol/include/ffprotocol/UiProtocol.h:86` comment
  still shows the bare `<SessionId>` example without the generation suffix.

## F4 — Drift gate structurally weak (planner findings 11–12) — ROOT CAUSE

- `tests/drift/check_doc_drift.ps1` checks four exact strings + line endings;
  none of F1–F3 violate it. False-green demonstrated by coexistence.

## F5 — Preset list ambiguity (audit extra) — LOW

- README §Building preset list reads enumerative but omits `debug-diagnostic`
  and `asan`. AGENTS' list is complete. Resolve by marking README's list as a
  subset behind a subset-contract fence.

## F6 — Negative-space checks with no finding (recorded so future audits do not re-chase)

- AUTONOMOUS.md loop-stage list matches intake contract; no contradiction.
- verify/README.md verb table matches verify.ps1 exactly.
- progress.md contains no hard counts presented as current truth beyond the
  dated campaign narrative sections which point at the generator.
- No doc hard-codes product version numbers (searched).
- CI (`ci.yml`) already executes `ctest --preset debug/release-test`, hence
  runs `ffdoc_drift_tests`; no missing mandatory path found for the gate
  itself (fixtures target added by this campaign rides the same matrices).

## Findings discovered during implementation (beyond planner examples)

- F7 — AGENTS.md §Workflow enumerated `allocated-size-storage-accuracy`,
  `indexed-search-allocated-size`, and `semantic-treemap-uia` as changes that
  "currently … have open tasks"; all three have zero unchecked tasks and none
  is recorded in lifecycle.json. Replaced with the manifest-pointer
  formulation. The open-tasks contract (`STALE_CURRENT_CLAIM` with
  unrecorded/failed states) now guards this class.
- F8 — AGENTS.md §Testing line contradicted itself on CTest presets:
  "CTest presets exist for `debug` and `release` only" vs the correct trio one
  paragraph earlier. Removed the stale sentence; both remaining statements are
  fenced (`test-presets-all`).
- F9 — `src/engine/src/IndexPipeline.h` comment still described the removed
  "double-buffered snapshot mechanism". Reworded to committed-generation
  publication (comment-only change; no behavior).
- F10 — progress.md opened with "## Active lifecycle evidence integrity
  campaign" and present-tense narration of a completed campaign. Retitled to
  Completed and reworded; the guard's sentence-segmented patterns now cover
  this class (verified by fixture case `stale-current-change-completed`).
- F11 — `src/protocol/include/ffprotocol/UiProtocol.h` snapshot-section comment
  showed the pre-generation `<SessionId>`-only name. Updated to the full
  `<SessionId>.<Instance>.<Generation>` composition.
