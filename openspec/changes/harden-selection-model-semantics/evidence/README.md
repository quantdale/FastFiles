# Evidence plan — harden-selection-model-semantics

Implementation evidence belongs in `implementation-evidence.md` and must be written from executed results, not anticipated outcomes.

## Required evidence

### 1. Baseline/source contract

Record:

- starting/final SHA;
- relevant canonical requirements from `multi-selection-and-dragdrop` and `ui-automation`;
- production call graph (`SelectionModel.h`, Column View plain/Ctrl/Shift/Ctrl+A paths, UIA action path);
- exact pre-change coverage inventory showing why `ffselection_identity_tests` and fake-surface UIA tests did not directly prove the selection state machine;
- confirmation that no privileged production-validation work is required.

### 2. Headless selection truth table

Provide the exact focused test command and a compact transition table containing, for every required case:

- starting selected set;
- starting anchor/focus;
- action + modifiers;
- expected/observed selected set;
- expected/observed anchor;
- expected/observed focus;
- PASS/FAIL.

Include plain replace, Ctrl add/remove/first-interaction, forward/backward Shift, repeated Shift with stable anchor, Shift without anchor, mixed sequences, Select All non-empty, and Select All empty.

### 3. Production-path convergence

Show how the real Ctrl+A path is tied to the tested Select All seam after the change. If code changes beyond the expected small consolidation were required, explain the defect, why it violated the canonical contract, and why the fix is the narrowest safe correction.

### 4. UIA provider parity

Record focused `ffcolumnview_uia_tests`/`ffuia_events_tests` outcomes and specific assertions proving:

- `Selection.GetSelection()` exact membership after Select/Add/Remove;
- `SelectionItem.IsSelected` agreement;
- unrelated item stability;
- dual-pane same-index isolation;
- existing identity/lifetime/events remain green.

### 5. Live native-input scenario

Record:

- capability probe result and exact app binary/lifecycle used;
- fixture path policy and row names/count (do not dump unrelated user filesystem contents);
- semantic method used to locate rows and derive click geometry;
- modifier input method;
- step-by-step observed selected names/counts after plain, Ctrl add, Ctrl remove, first Shift range, repeated Shift range, Ctrl+A, and dual-pane isolation;
- bounded waits/timeouts and failure diagnostics;
- isolated repetition result: 10/10 required;
- confirmation of no OCR, fixed row coordinates, screenshot-state inference, or test-only production hook.

Screenshots/tree dumps may be local run artifacts but SHALL NOT be committed unless the repository explicitly requires a sanitized durable artifact.

### 6. Regression/full validation matrix

Record command + result for at least:

- dedicated selection-model test;
- selection-identity test;
- Column View UIA test;
- UIA events test;
- column hit-test / directly affected UI tests;
- full Debug CTest;
- full release-test CTest;
- analyze `/W4 /WX` gate;
- `verify.ps1 build`;
- `verify.ps1 doctor`;
- source-marker check;
- diagnostic-policy check;
- doc-drift gate when present;
- strict OpenSpec validation;
- `git diff --check`;
- full `windows-ui-automation-validation` summary with zero unexpected FAIL.

### 7. Scope/repository closure

Record:

- changed-file inventory;
- statement that drag/drop/file operations/protocol/schema/index/service/signing were not broadened unless a directly required narrow fix says otherwise;
- historical `selection-model test depth` deferred-note reconciliation;
- clean tracked worktree;
- `HEAD == origin/main`;
- exact final SHA;
- hosted Actions run id/status/conclusion for that exact SHA as queried after push.

Do not create a recursive commit solely to add the final hosted-CI conclusion after the implementation commit. Report that result in the executor handoff if it completes after the final tree is already immutable.

## Evidence quality rules

- Never mark a task complete from intended behavior alone.
- Do not convert a real selection assertion failure into SKIP.
- Do not use retry-until-pass to satisfy the 10-run requirement.
- If the interactive UIA capability is genuinely unavailable, include the failed capability probe and leave live-dependent tasks incomplete rather than fabricating closure.
- Preserve prior evidence/history; add current truth rather than rewriting old run records.
