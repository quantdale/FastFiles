# Negative fixture matrix

`tests/drift/test_doc_truth_fixtures.ps1` builds disposable fixture trees and
runs `DocTruth.psm1` against them. Each case asserts exact expected reason
codes where applicable. No repo mutation, no network, no elevation.

| # | Fixture case | Mutation vs. green tree | Expected result / reason codes |
| --- | --- | --- | --- |
| 1 | Missing supported verb in fenced verb enumeration | remove `status` from fence | FAIL `VERB_MISSING:status` |
| 2 | Extra nonexistent verb in fence | add `teleport` | FAIL `VERB_UNKNOWN:teleport` |
| 3 | Exit-code table drift | change `10` label | FAIL `EXITCODE_MISMATCH` |
| 4 | Stale current-change claim | reinsert "current … change is <completed-change>" | FAIL `STALE_CURRENT_CLAIM` |
| 5 | Current open work points at external-evidence-only change | CLAUDE-style pointer | FAIL `STALE_CURRENT_CLAIM` |
| 6 | Lifecycle/prompt disagreement surface | claim unknown change name as current | FAIL `STALE_CURRENT_CLAIM` (unknown state) |
| 7 | Stale snapshot mechanics | "double-buffered"/"activeSlot" in current-facing doc | FAIL `REMOVED_MECHANIC` |
| 8 | Preset drift (equality contract) | drop a preset from all-fence | FAIL `PRESET_MISSING`; add fake → `PRESET_UNKNOWN` |
| 9 | Executable topology drift | rename target in CMake fixture so fence ≠ derived set | FAIL `TARGET_MISSING` |
| 10 | Unclosed fence | delete closing marker | FAIL `FENCE_UNCLOSED` |
| 11 | Unknown contract id in fence | `ff-truth:not-a-contract` | FAIL `FENCE_UNKNOWN_CONTRACT` |
| 12 | Duplicated fence for same contract on one surface | two opens | FAIL `FENCE_DUPLICATE` |
| 13 | Unreadable authority input | verify.ps1 missing from fixture | FAIL `AUTHORITY_UNREADABLE` (fail closed, not vacuous pass) |
| 14 | Historical document with legacy wording | same stale text placed outside surface set | PASS (no findings) — false-positive discipline |
| 15 | Reconciled surfaces + intact fences (positive control) | copy of reconciled docs + real seams | PASS, deterministic ordering across two runs (byte-identical diagnostics) |
| 16 | Diagnostics redaction | force failure; inspect output | No drive-letter absolute paths; repository-relative forward-slash paths only |

Cases 1–13 prove false-negative resistance; 14–16 prove false-positive
discipline, determinism, and path safety.

## Runner contract

- Exit 0 = all fixture expectations held; exit 1 = any expectation violated
  (including a check that failed to fire).
- Each case prints one `PASS:`/`FAIL:` line with the fixture id.
- Fixtures materialize under `$env:TEMP` and are removed on completion.
