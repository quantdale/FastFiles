$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\ProjectStatus.psm1') -Force
Import-Module (Join-Path $repoRoot 'verify\core\Schema.psm1') -Force

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "ok: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$clean = Get-ProjectStatusGitStateFromLines -Branch 'main' -Sha ('a' * 40) -StatusLines @()
$dirty = Get-ProjectStatusGitStateFromLines -Branch 'feature' -Sha ('b' * 40) -StatusLines @(' M progress.md') -DiffCheckPassed:$false
Check (-not $clean.dirty -and $clean.diffCheck -eq 'PASS') 'clean Git fixture is classified clean'
Check ($dirty.dirty -and $dirty.diffCheck -eq 'FAIL' -and $dirty.changedPaths.Count -eq 1) 'dirty Git fixture preserves the changed path and diff failure'

Check ((Get-ProjectStatusOutcome -Pass $true) -eq 'PASS') 'successful evidence is PASS'
Check ((Get-ProjectStatusOutcome -Pass $false -Available $false -Required $false) -eq 'SKIPPED') 'optional unavailable evidence is SKIPPED'
Check ((Get-ProjectStatusOutcome -Pass $false -Available $false -Required $true) -eq 'REQUIRED-BUT-UNAVAILABLE') 'required unavailable evidence stays externally blocked'
Check ((Get-ProjectStatusOutcome -Pass $false -Available $true) -eq 'FAIL') 'available failing evidence is FAIL'

$signing = Get-ProjectStatusTaskClassification -TaskText '[external] Signed privileged install requires authorized production certificate and pins.' -ChangeName 'sample' -TaskId '4.2' -SourcePath 'openspec/changes/sample/tasks.md:4'
$provider = Get-ProjectStatusTaskClassification -TaskText 'Validate the snapshot-restorable Hyper-V Windows guest over WinRM.' -ChangeName 'sample' -TaskId '5.1' -SourcePath 'openspec/changes/sample/tasks.md:5'
$interactive = Get-ProjectStatusTaskClassification -TaskText 'Run the visual DPI and High Contrast matrix on the interactive desktop.' -ChangeName 'sample' -TaskId '5.2' -SourcePath 'openspec/changes/sample/tasks.md:6'
$local = Get-ProjectStatusTaskClassification -TaskText 'Add a deterministic parser regression test.' -ChangeName 'sample' -TaskId '1.1' -SourcePath 'openspec/changes/sample/tasks.md:1'
$pathBearing = Get-ProjectStatusTaskClassification -TaskText 'Inspect C:\Users\Example\private\fixture.vhdx before running the provider.' -ChangeName 'sample' -TaskId '1.2' -SourcePath 'openspec/changes/sample/tasks.md:2'
Check ($signing.primaryCategory -eq 'production-signing' -and $signing.status -eq 'REQUIRED-BUT-UNAVAILABLE') 'signing task is production-signing blocked'
Check ($provider.primaryCategory -eq 'external-provider' -and $provider.status -eq 'REQUIRED-BUT-UNAVAILABLE') 'guest task is provider blocked'
Check ($interactive.primaryCategory -eq 'interactive' -and $interactive.status -eq 'BLOCKED_ENVIRONMENT') 'display task is environment blocked'
Check ($local.primaryCategory -eq 'local' -and $local.status -eq 'SKIPPED' -and $local.classificationConfidence -eq 'low') 'unmarked task remains visible as local work'
Check ($pathBearing.text -notmatch '(?i)C:\\Users\\Example' -and $pathBearing.text -match '<external-path>') 'task evidence text redacts absolute paths'
$explicitLocal = Get-ProjectStatusTaskClassification -TaskText 'Run signed interactive validation only after the local parser fixture is ready.' -ChangeName 'sample' -TaskId '1.3' -SourcePath 'openspec/changes/sample/tasks.md:3' -ExplicitCategories @('local') -ExplicitStatus 'SKIPPED'
Check ($explicitLocal.primaryCategory -eq 'local' -and $explicitLocal.classificationSource -eq 'explicit-lifecycle-metadata' -and $explicitLocal.status -eq 'SKIPPED') 'explicit lifecycle category overrides misleading task prose'

# Lifecycle/frontier fixtures are repository-independent and exercise the model
# without a live desktop, elevation, signing material, or network access.
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-status-lifecycle-' + [guid]::NewGuid().ToString('N'))
try {
    $fixtureChangesRoot = Join-Path $fixtureRoot 'openspec\changes'
    New-Item -ItemType Directory -Path $fixtureChangesRoot -Force | Out-Null
    $fixtureTasks = [ordered]@{
        'active-local' = @('- [ ] 1.1 Add a deterministic parser regression test.')
        'completed-external' = @('- [ ] 2.1 Run signed provider acceptance against the authorized guest.')
        'completed-administrative' = @('- [ ] 3.1 Reconcile with origin/main and inspect hosted CI.')
        'superseded-old' = @('- [ ] 4.1 Implement the old local behavior.')
        'superseding-new' = @('- [ ] 1.1 Replacement behavior is implemented.')
        'explicit-local' = @('- [ ] 5.1 Run signed interactive validation only after the local parser fixture is ready.')
        'mixed-work' = @('- [ ] 6.1 Implement the parser and run signed service validation.')
        'external-signing' = @('- [ ] 7.1 Provide an authorized production certificate and pins.')
        'external-provider' = @('- [ ] 8.1 Validate the snapshot-restorable Hyper-V guest.')
        'external-interactive' = @('- [ ] 9.1 Run the UIA visual matrix on the interactive desktop.')
        'external-elevated' = @('- [ ] 10.1 Run elevated service validation.')
        'external-clean-host' = @('- [ ] 11.1 Run on a clean supported Windows host.')
    }
    foreach ($change in $fixtureTasks.GetEnumerator()) {
        $changeDir = Join-Path $fixtureChangesRoot $change.Key
        New-Item -ItemType Directory -Path $changeDir -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $changeDir 'tasks.md') -Value $change.Value -Encoding utf8NoBOM
    }
    $fixtureEvidence = {
        param([string] $ChangeName)
        [pscustomobject]@{ kind = 'repository-path'; path = "openspec/changes/$ChangeName/tasks.md" }
    }
    $fixtureOverrideAdmin = [pscustomobject]@{ task = '3.1'; disposition = 'historical'; categories = @('administrative'); status = 'PASS'; evidence = @(& $fixtureEvidence 'completed-administrative') }
    $fixtureOverrideLocal = [pscustomobject]@{ task = '5.1'; disposition = 'active'; categories = @('local'); status = 'SKIPPED'; evidence = @(& $fixtureEvidence 'explicit-local') }
    $fixtureOverrideMixed = [pscustomobject]@{ task = '6.1'; disposition = 'mixed'; categories = @('local', 'production-signing'); status = 'REQUIRED-BUT-UNAVAILABLE'; evidence = @(& $fixtureEvidence 'mixed-work') }
    $fixtureEntries = @(
        [pscustomobject]@{ name = 'active-local'; state = 'active'; evidence = @(& $fixtureEvidence 'active-local'); supersededBy = $null; taskOverrides = @() }
        [pscustomobject]@{ name = 'completed-external'; state = 'completed'; evidence = @(& $fixtureEvidence 'completed-external'); supersededBy = $null; taskOverrides = @() }
        [pscustomobject]@{ name = 'completed-administrative'; state = 'completed'; evidence = @(& $fixtureEvidence 'completed-administrative'); supersededBy = $null; taskOverrides = @($fixtureOverrideAdmin) }
        [pscustomobject]@{ name = 'superseded-old'; state = 'superseded'; evidence = @(& $fixtureEvidence 'superseded-old'); supersededBy = 'superseding-new'; taskOverrides = @() }
        [pscustomobject]@{ name = 'superseding-new'; state = 'active'; evidence = @(& $fixtureEvidence 'superseding-new'); supersededBy = $null; taskOverrides = @() }
        [pscustomobject]@{ name = 'explicit-local'; state = 'active'; evidence = @(& $fixtureEvidence 'explicit-local'); supersededBy = $null; taskOverrides = @($fixtureOverrideLocal) }
        [pscustomobject]@{ name = 'mixed-work'; state = 'active'; evidence = @(& $fixtureEvidence 'mixed-work'); supersededBy = $null; taskOverrides = @($fixtureOverrideMixed) }
        [pscustomobject]@{ name = 'external-signing'; state = 'active'; evidence = @(& $fixtureEvidence 'external-signing'); supersededBy = $null; taskOverrides = @() }
        [pscustomobject]@{ name = 'external-provider'; state = 'active'; evidence = @(& $fixtureEvidence 'external-provider'); supersededBy = $null; taskOverrides = @() }
        [pscustomobject]@{ name = 'external-interactive'; state = 'active'; evidence = @(& $fixtureEvidence 'external-interactive'); supersededBy = $null; taskOverrides = @() }
        [pscustomobject]@{ name = 'external-elevated'; state = 'active'; evidence = @(& $fixtureEvidence 'external-elevated'); supersededBy = $null; taskOverrides = @() }
        [pscustomobject]@{ name = 'external-clean-host'; state = 'active'; evidence = @(& $fixtureEvidence 'external-clean-host'); supersededBy = $null; taskOverrides = @() }
    )
    $fixtureLifecycle = [pscustomobject]@{ schemaVersion = 2; kind = 'fastfiles-openspec-lifecycle'; changes = $fixtureEntries }
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'openspec\lifecycle.json') -Value ($fixtureLifecycle | ConvertTo-Json -Depth 20) -Encoding utf8NoBOM
    $fixtureFrontier = Get-ProjectStatusOpenSpecFrontier -RepoRoot $fixtureRoot
    $fixtureProjection = Get-ProjectStatusFrontierProjection -OpenSpec $fixtureFrontier
    Check ($fixtureFrontier.status -eq 'PASS' -and $fixtureFrontier.totalOpenTasks -eq 12) 'lifecycle fixture parses the complete raw unchecked inventory'
    Check (@($fixtureFrontier.changes | Where-Object { $_.name -eq 'active-local' }).openTasks[0].taskDisposition -eq 'active' -and @($fixtureFrontier.changes | Where-Object { $_.name -eq 'active-local' }).openTasks[0].classificationConfidence -eq 'low') 'active local task remains local with low-confidence fallback provenance'
    Check (@($fixtureFrontier.changes | Where-Object { $_.name -eq 'completed-external' }).openTasks[0].taskDisposition -eq 'external' -and @($fixtureProjection.providerBlocked | Where-Object { $_.change -eq 'completed-external' }).Count -eq 1) 'completed external task stays visible in a blocked bucket, not local-open'
    Check (@($fixtureProjection.historicalOpen | Where-Object { $_.change -eq 'completed-administrative' }).Count -eq 1) 'later-satisfied administrative task stays historical when its old checkbox remains open'
    Check (@($fixtureProjection.supersededOpen | Where-Object { $_.change -eq 'superseded-old' }).Count -eq 1 -and (@($fixtureProjection.supersededOpen | Where-Object { $_.change -eq 'superseded-old' })[0].evidence[0].kind -eq 'repository-path')) 'superseded task preserves typed evidence and avoids local-open'
    Check (@($fixtureProjection.localOpen | Where-Object { $_.change -eq 'explicit-local' }).Count -eq 1 -and (@($fixtureProjection.localOpen | Where-Object { $_.change -eq 'explicit-local' })[0].classificationSource -eq 'explicit-lifecycle-metadata')) 'explicit metadata wins over misleading signing/interactive prose'
    Check (@($fixtureProjection.mixedOpen | Where-Object { $_.change -eq 'mixed-work' }).Count -eq 1 -and @($fixtureProjection.localOpen | Where-Object { $_.change -eq 'mixed-work' }).Count -eq 0) 'mixed local/external task is not flattened into local-open'
    Check (@($fixtureProjection.productionSigningBlocked | Where-Object { $_.change -eq 'external-signing' }).Count -eq 1 -and @($fixtureProjection.providerBlocked | Where-Object { $_.change -eq 'external-provider' }).Count -eq 1) 'signing and provider tasks remain in their explicit external buckets'
    Check (@($fixtureProjection.environmentBlocked | Where-Object { $_.change -eq 'external-interactive' }).Count -eq 1 -and @($fixtureProjection.cleanHostElevatedBlocked | Where-Object { $_.change -eq 'external-elevated' }).Count -eq 1 -and @($fixtureProjection.cleanHostElevatedBlocked | Where-Object { $_.change -eq 'external-clean-host' }).Count -eq 1) 'interactive, elevated, and clean-host tasks remain blocked'
    $activeTaskPath = Join-Path $fixtureChangesRoot 'superseding-new\tasks.md'
    Set-Content -LiteralPath $activeTaskPath -Value '- [x] 1.1 Replacement behavior is implemented.' -Encoding utf8NoBOM
    $activeWithoutTasksModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check ($activeWithoutTasksModel.status -eq 'FAIL' -and @($activeWithoutTasksModel.semanticErrors | Where-Object { $_ -eq 'openspec-lifecycle-active-without-open-tasks:superseding-new' }).Count -eq 1) 'active lifecycle entries cannot silently lose every unchecked task'
    Set-Content -LiteralPath $activeTaskPath -Value '- [ ] 1.1 Replacement behavior is implemented.' -Encoding utf8NoBOM
    $fixtureOrder1 = @($fixtureFrontier.changes | ForEach-Object { $changeName = $_.name; $_.openTasks | ForEach-Object { "$changeName/$($_.task)" } }) -join '|'
    $fixtureOrder2 = @(Get-ProjectStatusOpenSpecFrontier -RepoRoot $fixtureRoot).changes | ForEach-Object { $changeName = $_.name; $_.openTasks | ForEach-Object { "$changeName/$($_.task)" } }
    Check ($fixtureOrder1 -eq (@($fixtureOrder2) -join '|')) 'lifecycle frontier ordering is stable across repeated generation'
    $fixtureLifecycleJson = Get-Content -LiteralPath (Join-Path $fixtureRoot 'openspec\lifecycle.json') -Raw | ConvertFrom-Json
    $fixtureLifecycleSchema = Get-Content -LiteralPath (Join-Path $repoRoot 'verify\schemas\openspec-lifecycle.schema.json') -Raw | ConvertFrom-Json
    Check ((Test-JsonSchema -Data $fixtureLifecycleJson -Schema $fixtureLifecycleSchema).Valid) 'lifecycle fixture validates against its schema'

    $fixtureLifecyclePath = Join-Path $fixtureRoot 'openspec\lifecycle.json'
    $writeFixtureLifecycle = {
        param($Manifest)
        $Manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $fixtureLifecyclePath -Encoding utf8NoBOM
    }
    $invalidPathManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    (@($invalidPathManifest.changes | Where-Object { $_.name -eq 'active-local' })[0]).evidence = @([pscustomobject]@{ kind = 'repository-path'; path = '../outside.txt' })
    & $writeFixtureLifecycle $invalidPathManifest
    $invalidPathModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check ($invalidPathModel.status -eq 'FAIL' -and @($invalidPathModel.evidenceErrors | Where-Object { $_ -match 'path-unsafe' }).Count -eq 1) 'repository traversal evidence fails closed'

    $absolutePathManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    (@($absolutePathManifest.changes | Where-Object { $_.name -eq 'active-local' })[0]).evidence = @([pscustomobject]@{ kind = 'repository-path'; path = 'C:\Users\Example\proof.md' })
    & $writeFixtureLifecycle $absolutePathManifest
    $absolutePathModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check (@($absolutePathModel.evidenceErrors | Where-Object { $_ -match 'path-unsafe' }).Count -eq 1) 'absolute repository evidence is rejected'

    $missingPathManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    (@($missingPathManifest.changes | Where-Object { $_.name -eq 'active-local' })[0]).evidence = @([pscustomobject]@{ kind = 'repository-path'; path = 'missing-proof.md' })
    & $writeFixtureLifecycle $missingPathManifest
    $missingPathModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check (@($missingPathModel.evidenceErrors | Where-Object { $_ -match 'path-not-found' }).Count -eq 1) 'missing repository evidence is diagnosed'

    $reparseTarget = Join-Path $fixtureRoot 'reparse-target'
    $reparseLink = Join-Path $fixtureRoot 'reparse-link'
    New-Item -ItemType Directory -Path $reparseTarget -Force | Out-Null
    try {
        New-Item -ItemType Junction -Path $reparseLink -Target $reparseTarget -ErrorAction Stop | Out-Null
        $reparseManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
        (@($reparseManifest.changes | Where-Object { $_.name -eq 'active-local' })[0]).evidence = @([pscustomobject]@{ kind = 'repository-path'; path = 'reparse-link' })
        & $writeFixtureLifecycle $reparseManifest
        $reparseModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
        Check (@($reparseModel.evidenceErrors | Where-Object { $_ -match 'path-reparse-point' }).Count -eq 1) 'repository reparse-point evidence is rejected'
    } catch {
        Check $false 'repository reparse-point evidence is rejected'
    }

    $legacyManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $legacyManifest.schemaVersion = 1
    (@($legacyManifest.changes | Where-Object { $_.name -eq 'active-local' })[0]).evidence = @('legacy-string-evidence')
    & $writeFixtureLifecycle $legacyManifest
    $legacyModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check ($legacyModel.status -eq 'FAIL' -and @($legacyModel.structuralErrors | Where-Object { $_ -eq 'openspec-lifecycle-schema-version-invalid' }).Count -eq 1 -and @($legacyModel.evidenceErrors | Where-Object { $_ -match 'evidence-object-required' }).Count -eq 1) 'schema-v1 string evidence is never trusted'

    $schemaTypeManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $schemaTypeManifest.schemaVersion = '2'
    & $writeFixtureLifecycle $schemaTypeManifest
    $schemaTypeModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check (@($schemaTypeModel.structuralErrors | Where-Object { $_ -eq 'openspec-lifecycle-schema-version-invalid' }).Count -eq 1) 'non-integer lifecycle schema versions fail closed'

    $noteManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    (@($noteManifest.changes | Where-Object { $_.name -eq 'completed-external' })[0]).evidence = @([pscustomobject]@{ kind = 'note'; text = 'context only' })
    & $writeFixtureLifecycle $noteManifest
    $noteModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check (@($noteModel.semanticErrors | Where-Object { $_ -eq 'openspec-lifecycle-authoritative-evidence-required:completed-external' }).Count -eq 1) 'non-authoritative notes cannot complete a change'

    $selfManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    (@($selfManifest.changes | Where-Object { $_.name -eq 'superseded-old' })[0]).supersededBy = 'superseded-old'
    & $writeFixtureLifecycle $selfManifest
    $selfModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check (@($selfModel.semanticErrors | Where-Object { $_ -eq 'openspec-lifecycle-supersession-self:superseded-old' }).Count -eq 1) 'self-supersession is rejected'

    $cycleManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $cycleOld = @($cycleManifest.changes | Where-Object { $_.name -eq 'superseded-old' })[0]
    $cycleNew = @($cycleManifest.changes | Where-Object { $_.name -eq 'superseding-new' })[0]
    $cycleNew.state = 'superseded'
    $cycleNew.supersededBy = 'superseded-old'
    & $writeFixtureLifecycle $cycleManifest
    $cycleModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check (@($cycleModel.semanticErrors | Where-Object { $_ -match 'supersession-cycle' }).Count -gt 0) 'multi-node supersession cycles are rejected'

    $externalCategoryManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $externalCategoryOverride = @((@($externalCategoryManifest.changes | Where-Object { $_.name -eq 'explicit-local' })[0]).taskOverrides)[0]
    $externalCategoryOverride.disposition = 'external'
    $externalCategoryOverride.categories = @('local')
    & $writeFixtureLifecycle $externalCategoryManifest
    $externalCategoryModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check (@($externalCategoryModel.semanticErrors | Where-Object { $_ -match 'external-task-category-required|external-task-local-category-contradiction' }).Count -gt 0) 'external local-only override is rejected'

    $mixedCategoryManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $mixedCategoryOverride = @((@($mixedCategoryManifest.changes | Where-Object { $_.name -eq 'mixed-work' })[0]).taskOverrides)[0]
    $mixedCategoryOverride.categories = @('local')
    & $writeFixtureLifecycle $mixedCategoryManifest
    $mixedCategoryModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check (@($mixedCategoryModel.semanticErrors | Where-Object { $_ -eq 'openspec-lifecycle-mixed-task-external-component-required:mixed-work/6.1' }).Count -eq 1) 'mixed override requires an external component'

    $passManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $passOverride = @((@($passManifest.changes | Where-Object { $_.name -eq 'explicit-local' })[0]).taskOverrides)[0]
    $passOverride.status = 'PASS'
    & $writeFixtureLifecycle $passManifest
    $passModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check (@($passModel.semanticErrors | Where-Object { $_ -eq 'openspec-lifecycle-active-unchecked-pass:explicit-local/5.1' }).Count -eq 1) 'unchecked active PASS override is rejected'

    $hostedManifest = $fixtureLifecycle | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    (@($hostedManifest.changes | Where-Object { $_.name -eq 'active-local' })[0]).evidence = @([pscustomobject]@{ kind = 'hosted-run'; runId = '12345'; sha = ('a' * 40); record = 'openspec/changes/active-local/tasks.md' })
    & $writeFixtureLifecycle $hostedManifest
    $hostedModel = Get-ProjectStatusLifecycleModel -RepoRoot $fixtureRoot
    Check (@($hostedModel.evidenceErrors | Where-Object { $_ -match 'hosted-run-record|git-commit' }).Count -gt 0) 'hosted run without a durable binding is non-authoritative'

    $badRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-status-lifecycle-bad-' + [guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path (Join-Path $badRoot 'openspec\changes\bad-change') -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $badRoot 'openspec\changes\bad-change\tasks.md') -Value '- [ ] 1.1 A task with no trustworthy lifecycle.' -Encoding utf8NoBOM
        New-Item -ItemType Directory -Path (Join-Path $badRoot 'openspec') -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $badRoot 'openspec\lifecycle.json') -Value '{ "schemaVersion": 1, "kind": "fastfiles-openspec-lifecycle",' -Encoding utf8NoBOM
        $badFrontier = Get-ProjectStatusOpenSpecFrontier -RepoRoot $badRoot
        $badProjection = Get-ProjectStatusFrontierProjection -OpenSpec $badFrontier
        Check ($badFrontier.status -eq 'FAIL' -and $badFrontier.lifecycle.errors.Count -gt 0) 'malformed lifecycle metadata is a first-class status failure'
        Check (@($badProjection.ambiguousOpen | Where-Object { $_.change -eq 'bad-change' }).Count -eq 1 -and @($badProjection.localOpen | Where-Object { $_.change -eq 'bad-change' }).Count -eq 0) 'malformed lifecycle metadata retains the task as ambiguous, never local-open'
    } finally {
        Remove-Item -LiteralPath $badRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
} finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$futureRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-status-future-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path (Join-Path $futureRoot 'openspec\changes\future-campaign-2099-xyz') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $futureRoot 'openspec\changes\future-campaign-2099-xyz\tasks.md') -Value '- [ ] 1.1 Future local work that should be frontier-visible.' -Encoding utf8NoBOM
    $futureEntry = [pscustomobject]@{ name = 'future-campaign-2099-xyz'; state = 'active'; evidence = @([pscustomobject]@{ kind = 'repository-path'; path = 'openspec/changes/future-campaign-2099-xyz/tasks.md' }); supersededBy = $null; taskOverrides = @() }
    $futureLifecycle = [pscustomobject]@{ schemaVersion = 2; kind = 'fastfiles-openspec-lifecycle'; changes = @($futureEntry) }
    Set-Content -LiteralPath (Join-Path $futureRoot 'openspec\lifecycle.json') -Value ($futureLifecycle | ConvertTo-Json -Depth 20) -Encoding utf8NoBOM
    $futureFrontier = Get-ProjectStatusOpenSpecFrontier -RepoRoot $futureRoot
    $futureProjection = Get-ProjectStatusFrontierProjection -OpenSpec $futureFrontier
    Check ($futureFrontier.status -eq 'PASS' -and $futureFrontier.activeLocalOpenTasks -eq 1) 'future campaign fixture active-local count is lifecycle-derived'
    Check ($futureProjection.localOpen.Count -eq $futureFrontier.activeLocalOpenTasks -and @($futureProjection.localOpen | Where-Object { $_.change -eq 'future-campaign-2099-xyz' }).Count -eq 1) 'future campaign appears in lifecycle-driven local-open without hard-coded test name'
    $futureActiveMismatch = @($futureProjection.localOpen | Where-Object { $_.lifecycleState -ne 'active' }).Count
    $futureDispositionMismatch = @($futureProjection.localOpen | Where-Object { $_.taskDisposition -ne 'active' }).Count
    Check (($futureActiveMismatch -eq 0) -and ($futureDispositionMismatch -eq 0)) 'future local-open tasks are lifecycle-active with active disposition'
} finally {
    Remove-Item -LiteralPath $futureRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$junitPath = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-status-junit-' + [guid]::NewGuid().ToString('N') + '.xml')
Set-Content -LiteralPath $junitPath -Value '<testsuites tests="4" failures="1" skipped="1"><testsuite name="fixture" tests="4" failures="1" skipped="1" /></testsuites>' -Encoding utf8NoBOM
$junitResult = Get-ProjectStatusJUnitResult -Path $junitPath
Check ($junitResult.status -eq 'FAIL' -and $junitResult.passed -eq 2 -and $junitResult.failed -eq 1 -and $junitResult.skipped -eq 1) 'JUnit last-result parser preserves pass/fail/skip counts'
Remove-Item -LiteralPath $junitPath -Force -ErrorAction SilentlyContinue

$repoRelative = ConvertTo-ProjectStatusSafePath -Path (Join-Path $repoRoot 'verify\status\generated\project-status.json') -RepoRoot $repoRoot
$external = ConvertTo-ProjectStatusSafePath -Path (Join-Path ([IO.Path]::GetTempPath()) 'private-user-path.txt') -RepoRoot $repoRoot
Check ($repoRelative -eq 'verify/status/generated/project-status.json') 'repository paths are projected relative to the root'
Check ($external -eq '<external-path>') 'external absolute paths are redacted'
$headSha = ((& git -C $repoRoot rev-parse HEAD 2>$null) | Out-String).Trim()
$validGitEvidence = Resolve-ProjectStatusEvidence -RepoRoot $repoRoot -Evidence ([pscustomobject]@{ kind = 'git-commit'; sha = $headSha }) -Context 'test/valid-git'
$shortGitEvidence = Resolve-ProjectStatusEvidence -RepoRoot $repoRoot -Evidence ([pscustomobject]@{ kind = 'git-commit'; sha = $headSha.Substring(0, 7) }) -Context 'test/short-git'
$missingGitEvidence = Resolve-ProjectStatusEvidence -RepoRoot $repoRoot -Evidence ([pscustomobject]@{ kind = 'git-commit'; sha = ('f' * 40) }) -Context 'test/missing-git'
Check ($validGitEvidence.valid -and $validGitEvidence.authoritative -and $validGitEvidence.value.sha -eq $headSha) 'full reachable Git evidence resolves authoritatively'
Check (-not $shortGitEvidence.valid -and @($shortGitEvidence.errors | Where-Object { $_ -match 'sha-not-full' }).Count -eq 1) 'abbreviated Git evidence is rejected'
Check (-not $missingGitEvidence.valid -and @($missingGitEvidence.errors | Where-Object { $_ -match 'commit-not-found' }).Count -eq 1) 'nonexistent Git evidence is rejected'

$verificationFixture = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-status-run-result-' + [guid]::NewGuid().ToString('N') + '.json')
$verificationPass = [pscustomobject]@{
    runId = 'run-status-fixture'; invocationId = 'inv-status-fixture'; status = 'PASS'; classification = 'PASS'; capabilities = @(
        [pscustomobject]@{ capabilityId = 'fixture'; status = 'PASS'; classification = 'PASS'; reason = $null }
    )
}
$verificationPass | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $verificationFixture -Encoding utf8NoBOM
$verificationEvidence = Get-ProjectStatusVerificationEvidence -RepoRoot $repoRoot -RunResultPath $verificationFixture
Check ($verificationEvidence.status -eq 'PASS' -and $verificationEvidence.passCount -eq 1 -and $verificationEvidence.runId -eq 'run-status-fixture') 'selected verifier result is projected with identity and counts'
$verificationContradiction = [pscustomobject]@{
    runId = 'run-status-bad'; invocationId = 'inv-status-bad'; status = 'PASS'; classification = 'PASS'; capabilities = @(
        [pscustomobject]@{ capabilityId = 'fixture'; status = 'FAIL'; classification = 'ASSERTION_FAILURE'; reason = 'fixture-failed' }
    )
}
$verificationContradiction | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $verificationFixture -Encoding utf8NoBOM
$verificationBadEvidence = Get-ProjectStatusVerificationEvidence -RepoRoot $repoRoot -RunResultPath $verificationFixture
Check ($verificationBadEvidence.status -eq 'FAIL' -and $verificationBadEvidence.classification -eq 'HARNESS_ERROR' -and $verificationBadEvidence.reason -eq 'verification-run-result-contradiction') 'selected verifier contradiction is a harness failure'
Remove-Item -LiteralPath $verificationFixture -Force -ErrorAction SilentlyContinue

$fixtureStatus = [pscustomobject]@{
    tests = [pscustomobject]@{ discoveredCount = 75; builds = @() }
    openSpec = [pscustomobject]@{ totalOpenTasks = 2; changesWithOpenTasks = 1; changes = @() }
    git = $clean
    generatedAtUtc = '2026-08-24T00:00:00Z'
    capabilities = [pscustomobject]@{ status = 'PASS'; loadedCount = 1; rejectedCount = 0 }
    verification = [pscustomobject]@{ status = 'SKIPPED'; reason = 'explicit-verification-run-result-not-supplied'; source = $null }
    release = [pscustomobject]@{ status = 'SKIPPED'; classification = 'NOT_RUN' }
    frontier = [pscustomobject]@{ locallyCertified = @(); localOpen = @(); environmentBlocked = @(); providerBlocked = @(); productionSigningBlocked = @(); cleanHostElevatedBlocked = @() }
}
$markdown = New-ProjectStatusMarkdown -Status $fixtureStatus
$consistent = Test-ProjectStatusClaimConsistency -Markdown '75 CTest tests and 2 open OpenSpec tasks.' -Status $fixtureStatus
$stale = Test-ProjectStatusClaimConsistency -Markdown '74 CTest tests and 1 open OpenSpec task.' -Status $fixtureStatus
Check ($consistent.valid) 'matching dynamic claims pass the drift seam'
Check (-not $stale.valid -and $stale.errors.Count -eq 2) 'stale test/frontier claims fail with regeneration guidance'
Check ($markdown -notmatch 'C:\\Users\\') 'Markdown projection does not emit a host-user path'

$releaseFixture = Join-Path $PSScriptRoot 'fixtures\release-readiness-composition.json'
$releaseEvidence = Get-ProjectStatusReleaseEvidence -RepoRoot $repoRoot -ReleaseReportPath $releaseFixture
Check ($releaseEvidence.status -eq 'PASS' -and $releaseEvidence.classification -eq 'NON-PRODUCTION-OR-BLOCKED') 'release evidence stays non-production without a production gate'
$invalidLifecycleRelease = Get-ProjectStatusReleaseEvidence -RepoRoot $repoRoot -ReleaseReportPath $releaseFixture -LifecycleStatus FAIL
Check ($invalidLifecycleRelease.status -eq 'FAIL' -and $invalidLifecycleRelease.classification -eq 'NON-PRODUCTION-OR-BLOCKED' -and -not $invalidLifecycleRelease.productionReady) 'lifecycle integrity failure cannot project production readiness'

$frontierOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-status-frontier-' + [guid]::NewGuid().ToString('N'))
& (Get-Command pwsh.exe).Source -NoProfile -NonInteractive -File (Join-Path $repoRoot 'verify\status\New-ReleaseReadinessReport.ps1') `
    -OutputDirectory $frontierOutput -ReleaseReportPath $releaseFixture -SkipCtest -Format json | Out-Null
Check ($LASTEXITCODE -eq 0) 'release-readiness frontier generator completes from a fixture composition'
if ($LASTEXITCODE -eq 0) {
    $frontierJson = Get-Content -LiteralPath (Join-Path $frontierOutput 'release-readiness-frontier.json') -Raw | ConvertFrom-Json
    $frontierSchema = Get-Content -LiteralPath (Join-Path $repoRoot 'verify\schemas\release-readiness-frontier.schema.json') -Raw | ConvertFrom-Json
    $frontierSchemaResult = Test-JsonSchema -Data $frontierJson -Schema $frontierSchema
    Check $frontierSchemaResult.Valid 'generated release-readiness frontier validates against its schema'
    Check ($frontierJson.releaseCandidate.classification -eq 'NON-PRODUCTION-OR-BLOCKED' -and $frontierJson.status -eq 'PASS') 'release composition cross-check never infers production readiness'
    $frontierText = Get-Content -LiteralPath (Join-Path $frontierOutput 'release-readiness-frontier.json') -Raw
    Check ($frontierText -notmatch '(?i)C:[\\/]Users[\\/]Example') 'release frontier redacts slash-normalized absolute paths'
}
Remove-Item -LiteralPath $frontierOutput -Recurse -Force -ErrorAction SilentlyContinue

$timestampSchema = [pscustomobject]@{ type = 'string' }
$timestampValue = [DateTime]::UtcNow
$timestampResult = Test-JsonSchema -Data $timestampValue -Schema $timestampSchema
Check $timestampResult.Valid 'schema validator accepts ISO timestamp values round-tripped by ConvertFrom-Json'

$generatedStatus = New-ProjectStatus -RepoRoot $repoRoot -OutputDirectory (Join-Path $repoRoot 'verify\status\generated') -SkipCtest
$statusSchema = Get-Content -LiteralPath (Join-Path $repoRoot 'verify\schemas\project-status.schema.json') -Raw | ConvertFrom-Json
$statusSchemaResult = Test-JsonSchema -Data $generatedStatus -Schema $statusSchema
Check $statusSchemaResult.Valid 'generated project status validates against its schema'
$liveLifecycleJson = Get-Content -LiteralPath (Join-Path $repoRoot 'openspec\lifecycle.json') -Raw | ConvertFrom-Json
$liveLifecycleSchema = Get-Content -LiteralPath (Join-Path $repoRoot 'verify\schemas\openspec-lifecycle.schema.json') -Raw | ConvertFrom-Json
Check ((Test-JsonSchema -Data $liveLifecycleJson -Schema $liveLifecycleSchema).Valid) 'live lifecycle manifest validates against its schema'

$liveGit = Invoke-ProjectStatusGit -RepoRoot $repoRoot
$frontier = Get-ProjectStatusOpenSpecFrontier -RepoRoot $repoRoot
Check ($liveGit.sha.Length -eq 40 -and $liveGit.branch.Length -gt 0) 'live Git evidence contains branch and SHA'
Check ($frontier.status -eq 'PASS' -and $null -ne $frontier.totalOpenTasks) 'live OpenSpec frontier parses all task ledgers'
$liveProjection = Get-ProjectStatusFrontierProjection -OpenSpec $frontier
Check (@($liveProjection.localOpen | Where-Object { $_.change -in @('nonblocking-indexed-read-plane', 'phase8-release-readiness-convergence', 'real-user-dogfooding-release-engineering', 'runtime-certification-and-verifier-convergence', 'ship-ready-ui-ux-hardening-convergence') }).Count -eq 0) 'historical and external campaign tasks do not reappear as active local work'
Check ($liveProjection.localOpen.Count -eq $frontier.activeLocalOpenTasks) 'live local-open projection count matches frontier active-local count'
$liveActiveMismatch = @($liveProjection.localOpen | Where-Object { $_.lifecycleState -ne 'active' }).Count
$liveDispositionMismatch = @($liveProjection.localOpen | Where-Object { $_.taskDisposition -ne 'active' }).Count
Check (($liveActiveMismatch -eq 0) -and ($liveDispositionMismatch -eq 0)) 'live local-open tasks are lifecycle-active with active disposition'
$liveCategoryMismatch = @($liveProjection.localOpen | Where-Object { $_.category -ne 'local' }).Count
Check ($liveCategoryMismatch -eq 0) 'live local-open tasks are local category'
$liveLifecycleModel = Get-ProjectStatusLifecycleModel -RepoRoot $repoRoot
Check ($liveLifecycleModel.status -eq 'PASS' -and $liveLifecycleModel.schemaVersion -eq 2 -and $liveLifecycleModel.integrity.status -eq 'PASS') 'live lifecycle model passes structural, evidence, and semantic integrity'
$liveHostedEvidence = @($liveLifecycleModel.entries | Where-Object { $_.name -eq 'openspec-frontier-lifecycle-truthfulness' })[0].evidence | Where-Object { $_.kind -eq 'hosted-run' }
Check ($liveHostedEvidence.valid -and $liveHostedEvidence.authoritative -and $liveHostedEvidence.record -and $liveHostedEvidence.sha.Length -eq 40) 'live hosted-run evidence is an exact-SHA durable binding'
$shortHostedRunId = $liveHostedEvidence.runId.Substring(0, $liveHostedEvidence.runId.Length - 1)
$shortHostedEvidence = Resolve-ProjectStatusEvidence -RepoRoot $repoRoot -Evidence ([pscustomobject]@{ kind = 'hosted-run'; runId = $shortHostedRunId; sha = $liveHostedEvidence.sha; record = $liveHostedEvidence.record }) -Context 'test/short-hosted-run'
Check (-not $shortHostedEvidence.valid -and @($shortHostedEvidence.errors | Where-Object { $_ -match 'record-run-id-missing' }).Count -eq 1) 'hosted-run binding requires an exact run ID token'
Check (@($liveLifecycleModel.entries | ForEach-Object { $_.evidence } | Where-Object { $_.kind -eq 'invalid' }).Count -eq 0) 'live lifecycle projection contains no invalid evidence objects'

if ($failures -gt 0) { exit 1 }
Write-Host 'All project-status tests passed.'
exit 0
