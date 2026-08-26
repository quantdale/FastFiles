# Source-provenance fixture matrix

| Case | Expected result |
|---|---|
| clean HEAD | full 40-char commit/tree; exact-tree eligible |
| repeat clean sample | deterministic source-state digest |
| unstaged tracked edit | non-qualifying; `unstaged-tracked-changes` |
| staged tracked edit | non-qualifying; `staged-tracked-changes` |
| tracked deletion | non-qualifying tracked delta |
| non-ignored untracked source/config | non-qualifying; `untracked-source-input` |
| ignored build/status output only | remains source-clean / exact-tree eligible |
| dirty local correction over stale commit | same HEAD/tree but development-only; historical false-green reproduced |
| source mutation between start/finish | `source-state-changed-during-validation`; qualification false |
| clean unchanged exact-tree state | exact-tree qualification true |
| prior-commit result selected after new commit | full HEAD and tree mismatch reasons |
| exact result selected for same candidate | valid binding |
| detached clean HEAD | supported and exact-tree eligible |
| missing `.git` metadata | fail closed |
| repeated reason projection | stable sorted reason codes |
| serialized provenance | no absolute fixture path |

The matrix is implemented by `tests/drift/test_source_provenance_fixtures.ps1` and
registered as `ffsource_provenance_fixture_tests` under the existing PowerShell-gated
CTest drift suite.

The exact-tree orchestrator separately proves owned detached-worktree materialization,
fast gate execution, start/finish source identity, run-result schema compatibility,
and cleanup restricted to the GUID-owned path it created.
