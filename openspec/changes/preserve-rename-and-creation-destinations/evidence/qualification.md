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

No source-identity or ancestor-reparse race protection, filesystem alias equivalence or cross-volume rename is claimed. Fixtures use ordinary owned temporary paths, message-only windows and native APIs; no Explorer, Recycle Bin, GUI-input recovery, process termination, privileged service, signing or global changes. Interactive app qualification remains separate and is not inferred from worker tests. Full committed-source certification and publication are tracked by tasks 2.1/2.2.
