# Rename and creation destination safety

## Scope and baseline

Fresh isolated branch from PR33 `020726519985477d8cb21d163b08a5e4cc37cf25`; parent ref unchanged after fetch. No existing PR is merged. Relevant original-vision acceptance: reliable daily file operations (original-vision-journey-recovery 4.1).

Baseline command: `pwsh -NoProfile -File dev.ps1 test-fast -Preset debug -SkipBuild -Filter '^ffcreation_collision_tests$'`. Six failures, 0.57 s: actual worker CreateFile/CreateFolder on occupied names each returns completed=1/failures=0/createdPaths=1, publishing the existing requested path for inline rename (existing bytes/children stay intact); direct Shell queue-to-perform RenameItem returns S_OK after replacing a destination introduced with CREATE_NEW after queuing and removing the incoming source. Existing worker occupied-target Rename already refuses with 0x80070050. Baseline preserved in baseline.log.gz.

The late Shell observation is NOT injection inside the real worker's race window. It proves the native primitive's replacement behavior with the exact previous flags; the earlier existence check does not prevent that race. The NewItem baseline does not independently establish whether Shell created an additional generated-name sibling; the demonstrated incorrect path publication alone is sufficient. The chosen contract is exact-name exclusive creation, not an idempotent success or automatic keep-both rename.

## Implementation and focused validation

RenameWithoutReplacement uses MoveFileExW with zero flags (no replacement or cross-volume copy permission). The worker retains name validation and produces exact source/result rename history itself. Empty-file creation reuses WriteNewFile's handle-bound CREATE_NEW and cleanup; folder creation uses CreateDirectoryW. Both report native errors on occupied names and never populate createdPaths from existence alone. Cancellation is checked before the short mutation; successful mutations stay successful even if cancellation races afterwards.

Focused MSVC Debug build is /W4 /WX clean. Initial fixed collision test PASS0.22s; nine related file-operation suites PASS22.07s. Added ordinary/case-only folder controls following independent review; final focused collision test PASS0.29s (includes actual filesystem leaf-casing checks for file and folder). Existing worker e2e includes ordinary rename/undo and creation flows.

Independent fresh-context read-only `reviewer` (run ce2fb8da-3cd9-4eea-9392-b51e59e0e5a7) found no introduced P1/P2 in production primitive, cancellation/history/outcome branches, collision seams or CMake registration. It correctly distinguishes native-primitive proof from a timed worker race injection. Its missing folder-positive-control note was addressed with owned ordinary/case-only folder rename and reverse rename checks; it did not run tests.

Editor/LSP diagnostics initially used missing includes/pre-C++20 flags for this new worktree (including FileOperations.h not found). Exported the CMake compile database; refreshed source=lsp probes still return inconclusive, not confirmed clean. Actual MSVC compile and later /analyze are authoritative validation here; no source includes were removed to hide editor findings.

## Limits

No source-identity or ancestor-reparse race protection, filesystem alias equivalence or cross-volume rename is claimed. Fixtures use ordinary owned temporary paths, message-only windows and native APIs; no Explorer, Recycle Bin, GUI-input recovery, process termination, privileged service, signing or global changes. Interactive app qualification remains separate and is not inferred from worker tests. Interactive app journeys have not been run for this change; prior session input preconditions failed, and no system-wide irrecoverability or historical flake root cause is inferred. Publication remains tracked by task 2.2.

## Committed-source certification

Implementation `f601998`; validated committed tree `33a2cbe` adds only the parent lifecycle correction (product/tests/CMake identical). Full Debug build and selected CTest **132/132 PASS225.64s**; full release-test build and selected CTest **132/132 PASS237.35s**; affected UI MSVC Analyze **68/68 build steps, warning-clean**; ASan through the supported developer environment **3/3 PASS49.95s** (actual worker e2e, creation-collision test, new-file writer). All configured tests run except the exactly named `ffverify_process_reaper_ps_tests` restricted qualification; it was not removed, weakened or called passing. No expensive scale benchmark was repeated.

The first full Debug run on `f601998` was **131/132**: ffproject_status_ps_tests correctly failed because PR33 had closed every transfer-protection task but left its lifecycle entry active. Root-cause error `openspec-lifecycle-active-without-open-tasks:protect-generated-transfer-results` was reproduced by the status generator, fixed administratively on the parent as `cb23f8e`, then carried into this successor. Parent focused status/strict95/drift PASS; successor full Debug rerun passed132/132. Original failing full run is retained as full-debug-tests.log.gz, not relabeled or hidden. The correction changes no product code or test assertion.

Parent ancestry is reconciled at `afde7d6`; `git diff 33a2cbe afde7d6 -- src tests cmake` is empty. Final metadata/evidence-only commits preserve that product-tree invariant; derive the live tip after fetching rather than requiring a self-referential SHA in this note. Strict OpenSpec96/96 and drift checks passed before certification and are repeated for final task/lifecycle updates. `source-identity.json` binds the reviewed/validated files and the implementation baseline.

## Publication

Draft **PR36** on PR33 was published at `6c18395d7119ff6781cf1bfc7bbc0d539e5a0c0d`. All owner-controlled PRs and main remain unmerged/unchanged. Parent administrative correction `cb23f8e` was normal-pushed separately. Hosted run40745252238 (successor) and run40742714965 (parent correction) each have six jobs, zero executed steps and the actual failed-payment/spending-limit annotation, inspected once without reruns or account changes. Hosted verification is **BLOCKED_EXTERNAL**, not a local pass. Task/lifecycle closure here is limited to this bounded repair, not whole-product or release readiness. Later metadata-only handoff commits do not require a self-referential SHA/CI loop; re-derive live refs and hosted results after fetch.
