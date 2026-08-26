# Authority map

One authoritative owner per machine-derived fact class; the drift guard
derives expectations from these seams and never embeds its own copy.

| Contract id | Authority seam (owner) | Derivation | Surfaces checked |
| --- | --- | --- | --- |
| `verifier-verbs` | `verify/verify.ps1` `[ValidateSet(...)]` on `$Verb` | Regex-extract quoted tokens from the param attribute | README, verify/README, AGENTS (fenced) |
| `exit-codes` | `$ExitPass/$ExitFail/$ExitSkipped/$ExitError/$ExitNotImplemented` assignments in `verify/verify.ps1` | Parse numeric assignments + canonical label map | AGENTS, verify/README (fenced) |
| `configure-presets-all` / `presets-subset` | `CMakePresets.json` `configurePresets` (non-hidden) | JSON parse; equality or subset per contract flavor | README (subset), AGENTS (all) |
| `test-presets-all` | `CMakePresets.json` `testPresets` | JSON parse; equality | AGENTS |
| `product-executables` | `add_executable(` in `src/**/CMakeLists.txt` filtered to names starting `FastFiles` | Naming convention: product executables are `FastFiles*`; documented here + spec | README, AGENTS (fenced) |
| `shared-libs-subset` | `add_library(... STATIC)` in `src/**/CMakeLists.txt` named `ff*` | Subset check of enumerated libs | README (fenced) |
| `lifecycle-pointer` | Existence of a stable pointer to `openspec/lifecycle.json` | Required reference in each current-facing guidance surface | README, AGENTS, CLAUDE, CODE_INDEX, AUTONOMOUS |
| anti-staleness `current-change-reference` | `openspec/lifecycle.json` change states | Whole-file patterns; claim allowed only for state `active` | All current-facing surfaces |
| snapshot mechanics | `src/engine/src/SnapshotPublisher.cpp`: presence of `GenerationSectionName(` composition and `CommitGeneration(&header->generation`, absence of slot identifiers | Behavior-level source markers; docs must not describe removed mechanics (`double-buffered`, `activeSlot`) | AGENTS, CODE_INDEX (+fences), global ban list |

## Naming-convention note

The `FastFiles*` executable convention is itself part of the contract: if a
future first-party executable intentionally uses another prefix, the authority
table above and the guard's derivation change together in one review. Internal
helpers that do not match the convention (e.g. probes under `src/indexsvc/`,
`fftest`) are not forced into doc enumerations.

## Non-goals preserved

- No new truth manifest file; every expectation traces to a seam above.
- No network access at validation time.
- Dynamic status counts remain generator-owned.
