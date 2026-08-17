# Evidence plan — deterministic-missing-path-state

This directory starts with an evidence plan only. Do not convert hypotheses below into PASS claims until the executing session records the observed result.

## Baseline

- Authored against `main` commit `2169e858f9cb3e757d613946756481593b188c2e`.
- The preceding semantic-treemap-uia change reported 21 PASS / 5 SKIP / 0 FAIL in the full live UIA capability.
- One remaining non-privileged SKIP was `in-column-error-state-late` with reason `error-state-trigger-not-deterministic`: the expected stale/missing wording appeared only on a refresh race after an externally deleted folder.
- The engine's degraded enumerator already maps `ERROR_FILE_NOT_FOUND`, `ERROR_PATH_NOT_FOUND`, and related absence errors to `NotFound`.
- Column View already maps `DirectoryEnumerationStatus::NotFound` to `SnapshotFailureReason::NoLongerAvailable` and the visible wording `This folder is no longer available.`.

These facts make source ordering / watcher terminal refresh / merge precedence the primary hypothesis, not an established root cause.

## Required pre-fix evidence

Record:

1. disposable path and lifecycle in sanitized form (do not record user-private directory contents);
2. successful pre-delete snapshot generation/status/item count;
3. deletion time/order;
4. watcher callbacks or terminal diagnostics;
5. explicit degraded enumeration result after deletion;
6. indexed merge(s) affecting the same path after deletion;
7. published snapshot generation/status for the path;
8. Column View freshness state and semantic status;
9. whether prior rows remain visible/current;
10. outcome across repeated runs.

The root cause must be stated from this evidence, not inferred from the prior SKIP label.

## Required post-fix evidence

### Ordering matrix

- indexed `Success` -> explicit `NotFound` -> stale indexed `Success` => `NotFound` remains authoritative;
- `NotFound` -> recreate -> explicit `Success` => recovery;
- watch terminal failure -> final refresh -> correct status;
- explicit shutdown/cancel -> no fabricated failure;
- existing empty directory -> `Success` with zero entries;
- `AccessDenied` and `IoError` remain distinct.

### Live UIA matrix

- initial sentinel item discoverable;
- external deletion;
- semantic status becomes `This folder is no longer available.`;
- old row not current;
- old provider stale safely;
- recreate same path with different sentinel;
- normal re-request/navigation recovers;
- new sentinel appears, old sentinel does not;
- 10 consecutive affected-scenario runs PASS;
- full capability has zero unexpected FAIL;
- `error-state-trigger-not-deterministic` absent from the late scenario.

### Validation matrix

Record exact outcomes for:

- focused watcher/UiServer/snapshot/ColumnView/UIA tests;
- Debug configure/build/CTest;
- release-test configure/build/CTest;
- Analyze configure/build;
- `verify.ps1 build`;
- `verify.ps1 doctor`;
- full `windows-ui-automation-validation`;
- source-marker check;
- diagnostic-policy check;
- doc-drift check;
- strict OpenSpec validation;
- `git diff --check`;
- final commit/push/clean-tree state;
- hosted CI bound to the final pushed SHA.

## Historical-evidence rule

Do not rewrite the prior 21 PASS / 5 SKIP / 0 FAIL Treemap-session evidence or make the old SKIP look as though it never happened. Add a later resolution note after this change is proven.

## Security / privacy

No evidence file may contain credentials, private keys, PFX material, tokens, raw MFT/USN bytes, file contents, or unnecessarily identifying user paths. Use disposable fixture names and sanitized diagnostics.
