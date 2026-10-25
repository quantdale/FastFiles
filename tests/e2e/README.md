# Real FastFiles E2E tests

The executable black-box harness is maintained in [`verify/e2e/`](../../verify/e2e/).
This directory keeps the test tree discoverable beside the component tests;
the harness remains outside CTest because it needs an interactive Windows
desktop and launches the real application.

Run it from the repository root:

```powershell
.\dev.ps1 test-e2e
.\dev.ps1 test-e2e -Filter navigation
```

For CMake-aware IDEs, the same explicit live runner is exposed as the
`fastfiles_e2e` target under `Tests/E2E`; it is never part of the default
build or CTest dependency graph.

See [`docs/testing/e2e.md`](../../docs/testing/e2e.md) for coverage,
timeouts, fixture safety, and failure evidence.

The focused native conflict runner exercises Cancel, Skip, Keep both and Replace
on marker-owned collisions in both themes, including names, physical bounds,
Tab traversal and exact file-content outcomes. Light uses native pointer input;
dark uses keyboard activation. Run it alone after all session-pipe CTests exit:

```powershell
pwsh ./verify/e2e/Test-FileConflictActions.ps1 -RepoRoot . -ArtifactsDir build/conflict-actions-fresh
```

It requires a clean committed `debug-diagnostic` build and a fresh artifact
folder. Review the actual light/dark PNGs as well as `result.json`; a passing
functional result is not visual approval. It seeds settings only inside its
isolated session sandbox and never changes system appearance preferences.
