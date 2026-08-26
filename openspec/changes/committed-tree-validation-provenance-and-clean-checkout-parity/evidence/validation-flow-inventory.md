# Validation-flow inventory

| Flow | Source state actually read | Existing identity before campaign | Can be dirty? | Can affect qualification? | Gap / disposition |
|---|---|---|---|---|---|
| Direct `ctest` / focused scripts | caller working directory + configured build tree | none beyond console context | yes | only by human narration | development feedback; not exact-tree evidence |
| `verify.ps1 build/run` capability run | caller working directory | runId, invocationId, provider/environment fingerprint | yes | can be cited in handoff/status when selected | runtime identity existed; local Git/source identity absent |
| `verify/status/Get-ProjectStatus.ps1` | live repo plus optional selected run-result | live HEAD/branch/dirty flag; selected runId/invocationId | yes | yes | selected run was not rebound to current HEAD/tree |
| `verify/status/New-ReleaseReadinessReport.ps1` | live status + release composition + optional selected run-result | same as status | yes | yes | inherited selected-result provenance gap |
| OpenSpec lifecycle evidence | tracked manifest + local Git history | typed repository-path/git-commit/hosted-run evidence | n/a | yes | already exact for full Git/hosted references; preserved |
| semantic doc/source/policy gates | caller working directory | test exit status | yes | handoff/CI | dirty local PASS could describe content absent from pushed commit |
| hosted GitHub Actions | checked-out event commit | `head_sha`; full-history checkout | no tracked source dirtiness at checkout | yes | already exact-SHA remote evidence; preserved |
| final session handoff | Git state + accumulated command evidence | procedural clean/push/parity rule | yes during implementation | yes | needed executable local exact-tree proof before citation |

## Result

The missing authority was local **source-state identity**, not capability identity or
hosted-run identity. The implementation therefore adds one shared repository-state
module and reuses existing run-result selection rather than adding a parallel
validation-result system.

Development checks remain allowed on dirty source. Only the dedicated exact-tree lane
and consumers that successfully re-bind its provenance may describe a local PASS as
proof for a specific Git candidate.
