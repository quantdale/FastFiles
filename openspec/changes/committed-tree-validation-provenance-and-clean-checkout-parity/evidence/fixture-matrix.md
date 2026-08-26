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
| detached clean HEAD | source-provenance model remains exact-tree eligible |
| missing `.git` metadata | fail closed |
| repeated reason projection | stable sorted reason codes |
| serialized provenance | no absolute fixture path |
| pre-existing qualifier destination with sentinel | qualifier exits nonzero and sentinel remains intact |

The matrix is implemented by `tests/drift/test_source_provenance_fixtures.ps1` and
registered as `ffsource_provenance_fixture_tests` under the existing PowerShell-gated
CTest drift suite.

Hosted run `32955810413` additionally supplied a negative integration fixture for the
orchestrator itself: it exposed command-shadow recursion and the existing status
system's detached-branch incompatibility. Both are explicitly root-caused and fixed
in the corrective implementation rather than hidden by a blind retry.
