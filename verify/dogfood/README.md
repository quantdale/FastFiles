# FastFiles dogfood tools

These commands exercise disposable fixtures and local, non-production builds.
All measurement output should be written outside the repository; the scripts
refuse repository-local output paths.

## Fixture

```powershell
pwsh ./verify/dogfood/New-DogfoodFixture.ps1 -Root "$env:TEMP/FastFiles-dogfood" -Profile Small
pwsh ./verify/dogfood/Compare-DogfoodFixture.ps1 `
  -ExpectedManifest "$env:TEMP/FastFiles-dogfood/fixture-manifest.jsonl" `
  -ActualRoot "$env:TEMP/FastFiles-dogfood"
pwsh ./verify/dogfood/Reset-DogfoodFixture.ps1 -Root "$env:TEMP/FastFiles-dogfood"
```

The generator is deterministic and the reset command requires its marker file.
The compare command independently checks paths, type, and size by default; add
`-HashFiles` for a stronger content check on small fixtures. Use the snapshot/
compare commands to verify filesystem truth after file-operation scenarios.

## Performance and resources

The release-test benchmark is the reproducible indexed-engine baseline. It can
run without an interactive UI, but its unsigned engine intentionally refuses
startup when production Authenticode pins are absent. Use `-SkipLaunch` for
that configuration:

```powershell
pwsh ./verify/dogfood/Measure-DogfoodPerformance.ps1 `
  -Configuration release-test -SkipLaunch -Diagnostics `
  -OutputPath "$env:TEMP/FastFiles-dogfood-performance-release-test.json"
```

For launch timings and UI/engine process ownership, use the debug-diagnostic
build that is explicitly permitted to run without production pins:

```powershell
pwsh ./verify/dogfood/Measure-DogfoodPerformance.ps1 `
  -Configuration debug-diagnostic -SkipBuild -Scales 10000,100000 `
  -OutputPath "$env:TEMP/FastFiles-dogfood-performance-diagnostic.json"
pwsh ./verify/dogfood/Invoke-DogfoodSoak.ps1 `
  -Configuration debug-diagnostic -DurationSeconds 30 -IntervalSeconds 5 `
  -OutputPath "$env:TEMP/FastFiles-dogfood-soak.json"
```

The performance command reports engine/index/search and process-launch
measurements. Folder-open, preview, and storage timing remain UIA scenarios;
they are not inferred from an engine-only benchmark.

## Semantic acceptance

The acceptance wrapper delegates to the existing semantic UIA capability and
records the exact verifier run under `verify/runs/`:

```powershell
pwsh ./verify/dogfood/Run-UserAcceptance.ps1 -Scenario all
pwsh ./verify/dogfood/Run-UserAcceptance.ps1 -Scenario search
```

It requires an interactive Windows desktop. If that context is unavailable,
the verifier must report `SKIPPED` with a reason; screenshots or coordinate
automation are not substitutes.
