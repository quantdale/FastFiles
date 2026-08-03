<#
    Tier-1 filesystem/index validation (autonomous-runtime-verification tasks
    7.1, 7.2, and 7.3): runs the engine + indexstore component test suites
    behind the four-state contract, mapping each suite's scenario battery to
    the task's required scenarios:
      7.1  volume enumeration / NTFS traversal & ingestion / USN journal
           handling / incremental indexing / rescans / staleness detection
      7.2  NTFS metadata & special files (junctions, symlinks, reparse-point
           identity, ADS, extended-length paths, locked files) via the
           degraded-mode enumerator scenario battery
      7.3  incremental create/delete/rename/move reflection / USN journal
           recovery (matching journal resumes, changed journal resets to the
           new journal's current point) / stale-index recovery
    Every scenario names the exact test functions that evidence it; a suite
    that is not built is SKIPPED with a reason, never a silent pass.
#>

$script:SuitePaths = @{
    store       = 'build\debug\tests\indexstore\ffindexstore_store_tests.exe'
    projection  = 'build\debug\tests\indexstore\ffindexstore_projection_tests.exe'
    pipeline    = 'build\debug\tests\engine\ffengine_index_pipeline_tests.exe'
    sessions    = 'build\debug\tests\engine\ffengine_volume_session_manager_tests.exe'
    specialfile = 'build\debug\tests\engine\ffengine_degraded_special_files_tests.exe'
}

function New-FsSubResult {
    param([string] $Id, [string] $Status, [string] $Reason, [double] $DurationMs, [string] $Detail)
    [pscustomobject]@{
        id = $Id; tier = 1; status = $Status; reason = $Reason; requiredContext = $null
        durationMs = [math]::Round($DurationMs, 0); detail = $Detail; diagnostics = @()
    }
}

function Test-FilesystemValidationAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows NTFS index component suites' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-FilesystemValidationDiagnostics {
    return @('volume-enumeration', 'ntfs-traversal-and-ingestion', 'usn-journal-handling', 'incremental-indexing', 'rescan-and-staleness-detection', 'index-change-and-recovery', 'special-files-and-ntfs-metadata')
}

function Invoke-TestSuite {
    param([string] $Exe, [string] $LogPath)
    $started = Get-Date
    if (-not (Test-Path -LiteralPath $Exe)) {
        return [pscustomobject]@{ Ran = $false; ExitCode = -1; DurationMs = 0 }
    }
    $output = @(& $Exe 2>&1 | ForEach-Object ToString)
    $exit = $LASTEXITCODE
    $output | Set-Content -LiteralPath $LogPath -Encoding utf8
    [pscustomobject]@{
        Ran = $true; ExitCode = $exit
        DurationMs = [math]::Round(((Get-Date) - $started).TotalMilliseconds, 0)
        Output = $output
    }
}

function Add-ScenarioResult {
    param([hashtable] $Suites, [string] $ArtifactsDir, [string] $Id, [string] $Scenario, [string[]] $SuiteKeys, [string[]] $Evidence)
    $detail = @()
    $failed = $false
    $skipped = $false
    $durationTotal = 0.0
    foreach ($key in $SuiteKeys) {
        $exe = $Suites[$key]
        $suiteName = Split-Path -Leaf $exe
        if (-not (Test-Path -LiteralPath $exe)) {
            $detail += "$suiteName=not-built"
            $skipped = $true
            continue
        }
        $logPath = Join-Path $ArtifactsDir ($Id + '-' + $suiteName + '.log')
        $outcome = Invoke-TestSuite -Exe $exe -LogPath $logPath
        $durationTotal += $outcome.DurationMs
        if ($outcome.Ran -and $outcome.ExitCode -ne 0) {
            $detail += "$suiteName=exit-$($outcome.ExitCode)"
            $failed = $true
        } else {
            $detail += "$suiteName=exit-0"
        }
    }
    if ($failed) {
        return New-FsSubResult -Id $Id -Status 'FAIL' -Reason 'suite-failed' -DurationMs $durationTotal -Detail "$Scenario; $($detail -join '; ') (evidence: $($Evidence -join ', '))"
    }
    if ($skipped) {
        return New-FsSubResult -Id $Id -Status 'SKIPPED' -Reason 'suite-not-built' -DurationMs $durationTotal -Detail "$Scenario; $($detail -join '; ') (evidence: $($Evidence -join ', '))"
    }
    return New-FsSubResult -Id $Id -Status 'PASS' -Reason $null -DurationMs $durationTotal -Detail "$Scenario; $($detail -join '; ') (evidence: $($Evidence -join ', '))"
}

function Invoke-FilesystemValidationCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $suites = @{}
    foreach ($entry in $script:SuitePaths.GetEnumerator()) {
        $suites[$entry.Key] = Join-Path $Options.RepoRoot $entry.Value
    }

    $subResults = @()
    $artifacts = @()
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-filesystem-validation"; type = 'suite-logs' }

    # Task 7.1 scenarios.
    $subResults += Add-ScenarioResult -Suites $suites -ArtifactsDir $ArtifactsDir -Id 'volume-enumeration' `
        -Scenario 'volume enumeration, reachability, disable teardown, pause, pending-decision status' `
        -SuiteKeys @('sessions') `
        -Evidence @('TestCollectVolumeStatusReportsReachableScannedVolumes', 'TestDisableTearsDownSessionLive', 'TestPendingDecisionForObservedUnselectedVolume', 'TestDisappearanceMarksUnavailableAndWithdrawsPublishedVolume')

    $subResults += Add-ScenarioResult -Suites $suites -ArtifactsDir $ArtifactsDir -Id 'ntfs-traversal-and-ingestion' `
        -Scenario 'MFT batch ingestion, snapshot export, rebuild, directory-listing projection' `
        -SuiteKeys @('pipeline', 'projection') `
        -Evidence @('TestApplyMftBatchAndExportSnapshot', 'TestRebuildAfterRestartMatchesPriorState', 'TestChildrenLookupSupportsDirectoryListing', 'TestPathIsReconstructedOnDemand')

    $subResults += Add-ScenarioResult -Suites $suites -ArtifactsDir $ArtifactsDir -Id 'usn-journal-handling' `
        -Scenario 'journal resume, journal ID change recovery, USN delete reasons' `
        -SuiteKeys @('sessions') `
        -Evidence @('TestMatchingJournalResumesWithoutReconciliation', 'TestChangedJournalTriggersReconciliation', 'TestUsnDeleteReasonRemovesEntry')

    $subResults += Add-ScenarioResult -Suites $suites -ArtifactsDir $ArtifactsDir -Id 'incremental-indexing' `
        -Scenario 'incremental batch upsert/remove into the store' `
        -SuiteKeys @('store', 'pipeline') `
        -Evidence @('TestBatchUpsertAndRemove', 'TestEntryIdentityIsKeyedByVolumeAndFrnNotPath')

    $subResults += Add-ScenarioResult -Suites $suites -ArtifactsDir $ArtifactsDir -Id 'rescan-and-staleness-detection' `
        -Scenario 'reconciliation removes entries absent from full scan; rebuild/WAL replay recover persisted state' `
        -SuiteKeys @('pipeline', 'store') `
        -Evidence @('TestReconciliationRemovesEntryNotSeenInFullScan', 'TestRebuildAfterRestartMatchesPriorState', 'TestWalReplayRecoversCommittedDataWithoutAnExplicitCheckpoint', 'TestDataSurvivesReopenAfterClose')

    # Task 7.3 scenario battery: incremental change reflection + journal recovery.
    $subResults += Add-ScenarioResult -Suites $suites -ArtifactsDir $ArtifactsDir -Id 'index-change-and-recovery' `
        -Scenario 'create/delete/rename/move reflected incrementally; journal recovery; stale-index recovery' `
        -SuiteKeys @('projection', 'store', 'pipeline') `
        -Evidence @('TestBatchUpsertAndRemove', 'TestReparentingUpdatesParentChildIndex', 'TestRemoveDropsEntryButLeavesSiblingsIntact', 'TestUsnDeleteReasonRemovesEntry', 'TestChangedJournalTriggersReconciliation', 'TestReconciliationRemovesEntryNotSeenInFullScan')

    # Task 7.2 scenario battery: NTFS metadata & special files through the
    # degraded-mode enumerator -- junctions/symlinks (reparse-point identity),
    # alternate data streams, extended-length (\\?\) paths, locked/in-use
    # files, and cycle-forming junction loops that must terminate.
    $subResults += Add-ScenarioResult -Suites $suites -ArtifactsDir $ArtifactsDir -Id 'special-files-and-ntfs-metadata' `
        -Scenario 'junctions, symlinks, reparse-point identity, ADS, extended-length paths, locked files, junction-cycle bounding' `
        -SuiteKeys @('specialfile') `
        -Evidence @('TestSpecialEntriesSurfaceInDegradedEnumeration', 'TestLongPathEnumeration', 'TestJunctionLoopBoundedByDepth')

    $failures = @($subResults | Where-Object status -eq 'FAIL').Count
    [pscustomobject]@{
        Status = if ($failures -gt 0) { 'FAIL' } else { 'PASS' }
        Reason = if ($failures -gt 0) { 'filesystem-validation-failed' } else { $null }
        Summary = "$(@($subResults | Where-Object status -eq 'PASS').Count) filesystem scenario batteries passed; $failures failed; $(@($subResults | Where-Object status -eq 'SKIPPED').Count) skipped"
        Artifacts = $artifacts
        SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-FilesystemValidationAvailability, Invoke-FilesystemValidationCapability, Get-FilesystemValidationDiagnostics
