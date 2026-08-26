# Design: repository-truth-surface-and-doc-drift-convergence

## Context

The lifecycle campaign made machine state fail-closed; guidance prose stayed a
manually synchronized narrative. The audit (see
`evidence/drift-findings.md`) found live contradictions between current-facing
docs and executable sources while `ffdoc_drift_tests` passed. The root cause is
structural: the drift guard validates string presence, not the semantics agents
consume.

## D1 — Authority ownership: derive, do not duplicate

Every checkable fact must be derived from an existing executable seam; the
guard must not become a second hand-maintained truth manifest.

| Fact class | Authoritative source (existing seam) |
| --- | --- |
| Verifier verbs | `[ValidateSet(...)]` on `$Verb` in `verify/verify.ps1` |
| Verifier exit codes | `$ExitPass/$ExitFail/$ExitSkipped/$ExitError/$ExitNotImplemented` assignments in `verify/verify.ps1` |
| CMake configure/build presets | `CMakePresets.json` (`configurePresets`, non-hidden names) |
| CMake test presets | `CMakePresets.json` (`testPresets`) |
| First-party executables | `add_executable(` targets under `src/**/CMakeLists.txt` named `FastFiles*` (naming convention documented here and in the spec) |
| Shared static libraries | `add_library(... STATIC)` targets under `src/**/CMakeLists.txt` named `ff*` |
| Current/completed/superseded campaigns | `openspec/lifecycle.json` states + `.agent/EXECUTION_PROMPT.md` Status |
| Snapshot publication mechanics | Source seam: `src/engine/src/SnapshotPublisher.cpp` (`GenerationSectionName`, `CommitGeneration(&header->generation, ...)`), reader in `src/ui/src/EngineClient.cpp` |
| Dynamic status counts | Never copied into fences; prose points to `pwsh ./verify/verify.ps1 status` |

Rationale for the `FastFiles*` / `ff*` naming-convention derivation: it makes
the executable/static-lib topology derivable without embedding a target list in
the test. The convention itself becomes the contract and is recorded in
`evidence/authority-map.md`.

## D2 — Explicit fenced claims instead of prose parsing

Free-form prose parsing is brittle in both directions. Current-facing docs wrap
machine-checkable enumerations in paired HTML-comment markers:

```
<!-- ff-truth:verifier-verbs -->
...enumeration...
<!-- /ff-truth:verifier-verbs -->
```

Rules:

- Markers are invisible in rendered Markdown.
- An unclosed, duplicated, or unknown-contract fence in a current-facing
  surface fails closed with a reason code (`FENCE_UNCLOSED`,
  `FENCE_DUPLICATE`, `FENCE_UNKNOWN_CONTRACT`) — orphaned fences cannot rot
  silently after contract renames.
- Everything outside fences is subject only to whole-file anti-staleness rules,
  never to enumeration equality.

## D3 — Contract set (v1)

Implemented in `tests/drift/DocTruth.psm1`; all deterministic and offline:

1. `verifier-verbs` — backticked verb tokens inside the fence must equal the
   ValidateSet exactly (order-insensitive).
2. `exit-codes` — `N`=`LABEL` pairs inside the fence must equal the exit-code
   constants and their canonical labels from `verify.ps1`.
3. `configure-presets-all` — enumerated preset names must equal the non-hidden
   configure preset names.
4. `presets-subset` — every enumerated preset name must exist (for docs that
   intentionally list a subset).
5. `test-presets-all` — enumerated test-preset names must equal `testPresets`.
6. `product-executables` — must equal all `FastFiles*` executable targets.
7. `shared-libs-subset` — every enumerated `ff*` static library must exist.
8. `lifecycle-pointer` — surface must reference `openspec/lifecycle.json` as
   the current-work authority at least once (stable pointer requirement).
9. Whole-file anti-staleness (no fence needed):
   - `current-change-reference`: patterns asserting a named change/campaign is
     *current/active/current open work* resolve against `openspec/lifecycle.json`;
     allowed only when that change's state is `active`. A completed,
     superseded, external-evidence-only, or ambiguous target fails
     (`STALE_CURRENT_CLAIM`). Unknown names fail too — the claim must be
     backed by manifest state, not memory.
   - Banned removed-mechanics strings in current-facing surfaces:
     `double-buffered`, `activeSlot` — tied to the source-derived fact that
     the publisher no longer has slots; if the publisher legitimately changes
     shape again, the source seam and this list change together in one review.

## D4 — Snapshot publication seam check

The engine reads structural markers from `SnapshotPublisher.cpp`
(`GenerationSectionName(` composition and `CommitGeneration(&header->generation`
before handoff). This keeps the doc claims anchored to behavior-level facts:
the checker verifies docs no longer describe slot-flip mechanics while the
source still proves generation-commit publication. It deliberately does not
assert implementation wording, so refactors that keep the mechanism do not
break the gate.

## D5 — Fail-closed diagnostics without host leakage

All failures print: reason code, authority seam, surface path (repository-
relative, forward-slash), and the offending region/fence id. Absolute host
paths are never printed. Unreadable authority input (missing `verify.ps1`,
unparseable JSON) fails with `AUTHORITY_UNREADABLE:<seam>` rather than passing
vacuously.

## D6 — Composition and entry point

`check_doc_drift.ps1` remains the single fast gate: banned phrases + line
endings + DocTruth contracts. Registered as `ffdoc_drift_tests` (unchanged
name, so CI matrices pick it up via existing `ctest --preset` runs).
Adversarial fixtures live in `tests/drift/test_doc_truth_fixtures.ps1`,
registered as `ffdoc_truth_fixture_tests`; they run the module against
temporary fixture trees (no repo mutation, no network).

## D7 — Historical boundary

Only declared current-facing surfaces are scanned:
README.md, AGENTS.md, CLAUDE.md, CODE_INDEX.md, AUTONOMOUS.md, verify/README.md,
progress.md. `openspec/**`, `.agent/**`, `docs/**` historical reports are out
of scope by design; old wording there is preserved evidence, not current truth.

## Risks

- **Marker rot** — mitigated by fail-closed fence validation (D2).
- **Over-constraining legitimate prose** — enumerations are checked only
  inside explicit fences; anti-staleness patterns are narrow (current/active
  assertions with a resolvable name).
- **False green via weak regex** — adversarial fixtures prove each rule fires
  on its negative case and stays silent on its positive/historical cases.
