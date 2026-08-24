<#
    Machine-generated repository status/frontier (phase8-release-readiness-convergence).

    The module deliberately keeps collection and classification separate. The pure
    seams are used by the PowerShell fixture tests; collection is allowed to report
    missing tools/evidence, never to invent a pass. Paths leaving the repository are
    sanitized before they can reach a committed Markdown projection.
#>

$script:ProjectStatusSchemaVersion = 1
$script:ProjectStatusOutcomes = @('PASS', 'FAIL', 'SKIPPED', 'BLOCKED_ENVIRONMENT', 'REQUIRED-BUT-UNAVAILABLE')
$script:ProjectStatusCategories = @('local', 'interactive', 'elevated', 'external-provider', 'production-signing', 'clean-host')

function Get-ProjectStatusOutcome {
    param(
        [bool] $Pass,
        [bool] $Available = $true,
        [bool] $Required = $false,
        [string] $UnavailableReason = 'evidence-unavailable'
    )

    if ($Pass) { return 'PASS' }
    if (-not $Available) {
        if ($Required) { return 'REQUIRED-BUT-UNAVAILABLE' }
        return 'SKIPPED'
    }
    return 'FAIL'
}

function ConvertTo-ProjectStatusSafePath {
    [CmdletBinding()]
    param(
        [AllowNull()] [string] $Path,
        [Parameter(Mandatory)] [string] $RepoRoot
    )

    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    $repoFull = ([IO.Path]::GetFullPath($RepoRoot)).TrimEnd('\') + '\'
    try { $pathFull = [IO.Path]::GetFullPath($Path) } catch { return '<redacted-path>' }
    if ($pathFull.StartsWith($repoFull, [StringComparison]::OrdinalIgnoreCase)) {
        return $pathFull.Substring($repoFull.Length).Replace('\', '/')
    }
    return '<external-path>'
}

function ConvertTo-ProjectStatusSafeText {
    param([AllowNull()] [string] $Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return $Text }
    $value = $Text
    if ($env:USERPROFILE) {
        try { $value = $value.Replace([IO.Path]::GetFullPath($env:USERPROFILE).TrimEnd('\'), '<user-profile>') } catch { }
    }
    return [regex]::Replace($value, '(?i)(?:[A-Z]:\\|\\\\)[^\s,;)\]]+', '<external-path>')
}

function Get-ProjectStatusGitStateFromLines {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Branch,
        [Parameter(Mandatory)] [string] $Sha,
        [AllowEmptyCollection()] [string[]] $StatusLines = @(),
        [bool] $DiffCheckPassed = $true
    )

    [pscustomobject]@{
        branch = $Branch
        sha = $Sha
        dirty = @($StatusLines).Count -gt 0
        changedPaths = @($StatusLines)
        diffCheck = if ($DiffCheckPassed) { 'PASS' } else { 'FAIL' }
    }
}

function Invoke-ProjectStatusGit {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $RepoRoot)

    $sha = ((& git -C $RepoRoot rev-parse HEAD 2>&1) | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Unable to resolve Git HEAD: $sha" }
    $branch = ((& git -C $RepoRoot branch --show-current 2>&1) | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Unable to resolve the current Git branch.' }
    $statusLines = @(git -C $RepoRoot status --porcelain=v1 2>&1)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect Git status.' }
    & git -C $RepoRoot diff --check *> $null
    $diffPassed = $LASTEXITCODE -eq 0
    return Get-ProjectStatusGitStateFromLines -Branch $branch -Sha $sha -StatusLines $statusLines -DiffCheckPassed $diffPassed
}

function Get-ProjectStatusTaskClassification {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $TaskText,
        [Parameter(Mandatory)] [string] $ChangeName,
        [Parameter(Mandatory)] [string] $TaskId,
        [Parameter(Mandatory)] [string] $SourcePath,
        [int] $SourceLine = 0
    )

    $text = $TaskText.ToLowerInvariant()
    $categories = [System.Collections.Generic.List[string]]::new()
    $reasons = [System.Collections.Generic.List[string]]::new()
    $rules = @(
        @{ Name = 'production-signing'; Pattern = 'signer|signing|certificate|thumbprint|pin|authenticode|trust.store|production release'; Reason = 'requires authorized production signing or trust evidence' },
        @{ Name = 'external-provider'; Pattern = 'provider|hyper.v|hyper-v|virtual machine|guest|vhdx|vhd|windows sandbox|winrm|snapshot|install media|iso|cloud runner|self.hosted runner'; Reason = 'requires an isolated/provider execution target or lawful source image' },
        @{ Name = 'clean-host'; Pattern = 'clean.host|clean supported|fresh installation|supported windows environment'; Reason = 'requires an uncontaminated supported Windows host' },
        @{ Name = 'interactive'; Pattern = 'interactive|uia|visual|dpi|monitor|high contrast|foreground|desktop|drag|screenshot'; Reason = 'requires an attached interactive desktop or physical display configuration' },
        @{ Name = 'elevated'; Pattern = 'elevat|administrator|admin token|privilege|service install|raw mft|usn|scm'; Reason = 'requires an elevated or privileged validation context' }
    )
    foreach ($rule in $rules) {
        if ($text -match $rule.Pattern) {
            [void]$categories.Add($rule.Name)
            [void]$reasons.Add($rule.Reason)
        }
    }
    if ($categories.Count -eq 0) {
        [void]$categories.Add('local')
        [void]$reasons.Add('locally actionable unless evidence proves otherwise')
    }
    $primary = $categories[0]
    $status = if ($categories -contains 'production-signing' -or $categories -contains 'external-provider' -or $categories -contains 'clean-host') {
        'REQUIRED-BUT-UNAVAILABLE'
    } elseif ($categories -contains 'interactive' -or $categories -contains 'elevated') {
        'BLOCKED_ENVIRONMENT'
    } else {
        'SKIPPED'
    }
    $nextAction = switch ($primary) {
        'production-signing' { 'Supply authorized signer/pins, then run the task-specific release validation command.'; break }
        'external-provider' { 'Supply a lawful Windows guest/template and run the provider preflight before the task capability.'; break }
        'clean-host' { 'Run on a clean supported Windows host and attach the task-specific evidence.'; break }
        'interactive' { 'Run the semantic UIA/visual command in the required interactive desktop configuration.'; break }
        'elevated' { 'Run the documented command with explicit elevation on a disposable or authorized host.'; break }
        default { 'Implement the task, run focused validation, and record evidence before checking it off.'; break }
    }
    [pscustomobject]@{
        change = $ChangeName
        task = $TaskId
        text = (ConvertTo-ProjectStatusSafeText -Text $TaskText.Trim())
        source = $SourcePath
        sourceLine = $SourceLine
        categories = @($categories)
        primaryCategory = $primary
        classificationConfidence = if ($categories.Count -eq 1 -and $primary -eq 'local') { 'low' } else { 'high' }
        status = $status
        reasons = @($reasons | Select-Object -Unique)
        nextAction = $nextAction
    }
}

function Get-ProjectStatusOpenSpecFrontier {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $RepoRoot)

    $changesRoot = Join-Path $RepoRoot 'openspec\changes'
    $changes = [System.Collections.Generic.List[object]]::new()
    if (-not (Test-Path -LiteralPath $changesRoot -PathType Container)) {
        return [pscustomobject]@{ status = 'FAIL'; reason = 'openspec-changes-directory-missing'; totalOpenTasks = 0; changes = @() }
    }
    foreach ($tasksFile in @(Get-ChildItem -LiteralPath $changesRoot -Filter tasks.md -Recurse -File | Sort-Object FullName)) {
        $changeName = Split-Path (Split-Path $tasksFile.FullName -Parent) -Leaf
        $open = [System.Collections.Generic.List[object]]::new()
        $lineNumber = 0
        foreach ($line in [IO.File]::ReadAllLines($tasksFile.FullName)) {
            $lineNumber++
            if ($line -match '^\s*-\s+\[\s\]\s+([0-9]+\.[0-9]+)\s+(.*)$') {
                $taskId = $Matches[1]
                $taskText = $Matches[2]
                $safeSource = "openspec/changes/$changeName/tasks.md:$lineNumber"
                $open.Add((Get-ProjectStatusTaskClassification -TaskText $taskText -ChangeName $changeName -TaskId $taskId -SourcePath $safeSource -SourceLine $lineNumber))
            }
        }
        if ($open.Count -gt 0) {
            $changes.Add([pscustomobject]@{
                name = $changeName
                tasksPath = "openspec/changes/$changeName/tasks.md"
                openTaskCount = $open.Count
                openTasks = @($open)
            })
        }
    }
    $allTasks = @($changes | ForEach-Object { $_.openTasks })
    [pscustomobject]@{
        status = 'PASS'
        totalOpenTasks = $allTasks.Count
        changesWithOpenTasks = $changes.Count
        categories = [pscustomobject]@{ local = @($allTasks | Where-Object primaryCategory -eq 'local').Count; interactive = @($allTasks | Where-Object primaryCategory -eq 'interactive').Count; elevated = @($allTasks | Where-Object primaryCategory -eq 'elevated').Count; externalProvider = @($allTasks | Where-Object primaryCategory -eq 'external-provider').Count; productionSigning = @($allTasks | Where-Object primaryCategory -eq 'production-signing').Count; cleanHost = @($allTasks | Where-Object primaryCategory -eq 'clean-host').Count }
        changes = @($changes)
    }
}

function Get-ProjectStatusCtestEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [string[]] $BuildNames = @('debug', 'release-test')
    )

    $ctest = Get-Command ctest -ErrorAction SilentlyContinue
    $builds = [System.Collections.Generic.List[object]]::new()
    foreach ($buildName in $BuildNames) {
        $buildRoot = Join-Path $RepoRoot "build\$buildName"
        $testFile = Join-Path $buildRoot 'CTestTestfile.cmake'
        if (-not (Test-Path -LiteralPath $testFile -PathType Leaf)) {
            $builds.Add([pscustomobject]@{ name = $buildName; status = 'SKIPPED'; reason = 'ctest-build-not-configured'; discovered = $null; lastResult = $null })
            continue
        }
        if (-not $ctest) {
            $builds.Add([pscustomobject]@{ name = $buildName; status = 'REQUIRED-BUT-UNAVAILABLE'; reason = 'ctest-not-found'; discovered = $null; lastResult = $null })
            continue
        }
        Push-Location $buildRoot
        try {
            $listOutput = @(& $ctest.Source -N 2>&1 | ForEach-Object { $_.ToString() })
            $listExit = $LASTEXITCODE
        } finally { Pop-Location }
        $total = $null
        foreach ($line in $listOutput) { if ($line -match 'Total Tests:\s*([0-9]+)') { $total = [int]$Matches[1] } }
        $lastLog = Join-Path $buildRoot 'Testing\Temporary\LastTest.log'
        $lastFailed = Join-Path $buildRoot 'Testing\Temporary\LastTestsFailed.log'
        $lastResult = $null
        $junitPath = Join-Path $RepoRoot "verify\status\generated\ctest-$buildName.xml"
        if (Test-Path -LiteralPath $junitPath -PathType Leaf) {
            $lastResult = Get-ProjectStatusJUnitResult -Path $junitPath -Source "verify/status/generated/ctest-$buildName.xml"
        }
        if (-not $lastResult -and (Test-Path -LiteralPath $lastLog -PathType Leaf)) {
            $logText = [IO.File]::ReadAllText($lastLog)
            $lastInfo = Get-Item -LiteralPath $lastLog
            $failedInfo = if (Test-Path -LiteralPath $lastFailed -PathType Leaf) { Get-Item -LiteralPath $lastFailed } else { $null }
            $currentFailureLog = $failedInfo -and $failedInfo.LastWriteTimeUtc -gt $lastInfo.LastWriteTimeUtc
            $failed = if ($currentFailureLog) { ([IO.File]::ReadAllLines($lastFailed)).Count } else { ([regex]::Matches($logText, '(?im)^\s*(?:\d+/\d+\s+)?Test\s+#\d+.*Failed')).Count }
            $passed = ([regex]::Matches($logText, '(?im)^\s*(?:\d+/\d+\s+)?Test\s+#\d+.*Passed')).Count
            if ($currentFailureLog -or $failed -gt 0) {
                $lastResult = [pscustomobject]@{ status = 'FAIL'; passed = $passed; failed = $failed; source = "build/$buildName/Testing/Temporary/LastTest.log" }
            } elseif ($passed -gt 0) {
                $lastResult = [pscustomobject]@{ status = 'PASS'; passed = $passed; failed = 0; source = "build/$buildName/Testing/Temporary/LastTest.log" }
            } elseif (-not $currentFailureLog) {
                # CTest's compact LastTest.log contains only start/end markers on
                # this host. A newer log than LastTestsFailed.log is still useful
                # evidence that the stale failure list is not the latest result;
                # do not report that historical list as a current failure.
                $lastResult = [pscustomobject]@{ status = 'PASS'; passed = $null; failed = 0; source = "build/$buildName/Testing/Temporary/LastTest.log"; evidence = 'CTest emitted no per-test lines; stale LastTestsFailed.log was older than LastTest.log.' }
            }
        }
        $builds.Add([pscustomobject]@{ name = $buildName; status = if ($listExit -eq 0) { 'PASS' } else { 'FAIL' }; reason = if ($listExit -eq 0) { $null } else { 'ctest-list-failed' }; discovered = $total; lastResult = $lastResult })
    }
    $discovered = @($builds | Where-Object { $null -ne $_.discovered } | Select-Object -ExpandProperty discovered | Sort-Object -Descending | Select-Object -First 1)
    [pscustomobject]@{ status = if (@($builds | Where-Object status -eq 'FAIL').Count -gt 0) { 'FAIL' } elseif (@($builds | Where-Object status -eq 'PASS').Count -gt 0) { 'PASS' } else { 'SKIPPED' }; discoveredCount = if ($discovered.Count -gt 0) { $discovered[0] } else { $null }; builds = @($builds) }
}

function Get-ProjectStatusJUnitResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Path,
        [string] $Source = 'verify/status/generated/ctest-junit.xml'
    )

    try {
        [xml]$document = [IO.File]::ReadAllText($Path)
        $suites = @($document.SelectNodes('//testsuite'))
        if ($suites.Count -eq 0) { return $null }
        $tests = 0; $failures = 0; $skipped = 0
        foreach ($suite in $suites) {
            $tests += [int]$suite.tests
            $failures += [int]$suite.failures
            $skipped += [int]$suite.skipped
        }
        [pscustomobject]@{
            status = if ($failures -gt 0) { 'FAIL' } elseif ($tests -gt 0) { 'PASS' } else { 'SKIPPED' }
            passed = $tests - $failures - $skipped
            failed = $failures
            skipped = $skipped
            source = $Source
        }
    } catch { return $null }
}

function Get-ProjectStatusCapabilityEvidence {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $RepoRoot)

    $capRoot = Join-Path $RepoRoot 'verify\capabilities'
    $manifests = @(Get-ChildItem -LiteralPath $capRoot -Filter capability.json -Recurse -File -ErrorAction SilentlyContinue)
    $loaded = [System.Collections.Generic.List[object]]::new()
    $rejected = [System.Collections.Generic.List[object]]::new()
    foreach ($file in $manifests) {
        try {
            $manifest = [IO.File]::ReadAllText($file.FullName) | ConvertFrom-Json
            $modulePath = Join-Path $file.Directory.FullName $manifest.entryModule
            if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) { throw "entry-module-missing:$($manifest.entryModule)" }
            $loaded.Add([pscustomobject]@{ id = $manifest.id; interfaceVersion = $manifest.interfaceVersion; tier = $manifest.tier; status = 'PASS'; reason = $null })
        } catch {
            $rejected.Add([pscustomobject]@{ id = $file.BaseName; status = 'FAIL'; reason = $_.Exception.Message })
        }
    }
    [pscustomobject]@{ status = if ($rejected.Count -eq 0) { 'PASS' } else { 'FAIL' }; loadedCount = $loaded.Count; rejectedCount = $rejected.Count; loaded = @($loaded); rejected = @($rejected) }
}

function Get-ProjectStatusReleaseEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [string] $OutputDirectory,
        [string] $ReleaseReportPath
    )

    $candidates = [System.Collections.Generic.List[string]]::new()
    if ($ReleaseReportPath) { $candidates.Add($ReleaseReportPath) }
    if ($OutputDirectory) { $candidates.Add((Join-Path $OutputDirectory 'release-readiness-composition.json')) }
    $candidates.Add((Join-Path $RepoRoot 'verify\status\release-readiness-composition.json'))
    $report = $null
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            try { $report = [IO.File]::ReadAllText($candidate) | ConvertFrom-Json; break } catch { }
        }
    }
    if (-not $report) {
        return [pscustomobject]@{ status = 'SKIPPED'; classification = 'NOT_RUN'; reason = 'release-readiness-report-not-found'; source = $null; productionReady = $false }
    }
    $overall = [string]$report.overall
    $productionGatePass = @($report.gates | Where-Object { $_.id -eq 'production-release-artifact-readiness' -and $_.status -eq 'PASS' }).Count -gt 0
    $classification = if ($overall -eq 'PASS' -and $productionGatePass) { 'PRODUCTION-READY' } else { 'NON-PRODUCTION-OR-BLOCKED' }
    [pscustomobject]@{ status = if ($overall -eq 'FAIL') { 'FAIL' } elseif ($overall -eq 'REQUIRED-BUT-UNAVAILABLE') { 'REQUIRED-BUT-UNAVAILABLE' } else { 'PASS' }; classification = $classification; reason = $null; source = 'release-readiness-composition.json'; productionReady = ($classification -eq 'PRODUCTION-READY'); overall = $overall }
}

function Test-ProjectStatusClaimConsistency {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Markdown,
        [Parameter(Mandatory)] $Status
    )
    $errors = [System.Collections.Generic.List[string]]::new()
    if ($Markdown -match '(?im)\b[0-9]+\s+CTest tests?\b') {
        $claimed = [int]$Matches[0].Split(' ')[0]
        if ($null -ne $Status.tests.discoveredCount -and $claimed -ne [int]$Status.tests.discoveredCount) { $errors.Add("CTest count $claimed differs from generated $($Status.tests.discoveredCount); regenerate with verify.ps1 status.") }
    }
    if ($Markdown -match '(?im)\b[0-9]+\s+open OpenSpec tasks?\b') {
        $claimed = [int]$Matches[0].Split(' ')[0]
        if ($claimed -ne [int]$Status.openSpec.totalOpenTasks) { $errors.Add("OpenSpec count $claimed differs from generated $($Status.openSpec.totalOpenTasks); regenerate with verify.ps1 status.") }
    }
    return [pscustomobject]@{ valid = $errors.Count -eq 0; errors = @($errors) }
}

function New-ProjectStatusMarkdown {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Status)

    $lines = [System.Collections.Generic.List[string]]::new()
    [void]$lines.Add('# FastFiles generated project status')
    [void]$lines.Add('')
    [void]$lines.Add("Generated UTC: $($Status.generatedAtUtc)  ")
    [void]$lines.Add("Commit: ``$($Status.git.sha)`` on ``$($Status.git.branch)``; worktree=$([string]$Status.git.dirty); diff-check=$($Status.git.diffCheck)")
    [void]$lines.Add('')
    [void]$lines.Add('## Local evidence')
    [void]$lines.Add('')
    [void]$lines.Add("- CTest discovered: $($Status.tests.discoveredCount)")
    foreach ($build in @($Status.tests.builds)) { [void]$lines.Add("- ``$($build.name)``: $($build.status)$(if ($build.reason) { " ($($build.reason))" })") }
    [void]$lines.Add("- Capability registry: $($Status.capabilities.status) ($($Status.capabilities.loadedCount) loaded, $($Status.capabilities.rejectedCount) rejected)")
    [void]$lines.Add('')
    [void]$lines.Add('## OpenSpec frontier')
    [void]$lines.Add('')
    [void]$lines.Add("Open unchecked tasks: **$($Status.openSpec.totalOpenTasks)** across **$($Status.openSpec.changesWithOpenTasks)** changes.")
    foreach ($change in @($Status.openSpec.changes)) {
        [void]$lines.Add("### ``$($change.name)`` ($($change.openTaskCount) open)")
        foreach ($task in @($change.openTasks)) { [void]$lines.Add("- ``$($task.task)`` [$($task.primaryCategory); $($task.status)] $($task.nextAction)") }
    }
    [void]$lines.Add('')
    [void]$lines.Add('## Release-readiness frontier')
    [void]$lines.Add('')
    foreach ($bucket in @('locallyCertified', 'localOpen', 'environmentBlocked', 'providerBlocked', 'productionSigningBlocked', 'cleanHostElevatedBlocked')) {
        $title = switch ($bucket) { 'locallyCertified' { 'Locally certified' } 'localOpen' { 'Local open work' } 'environmentBlocked' { 'Environment-blocked' } 'providerBlocked' { 'Provider-blocked' } 'productionSigningBlocked' { 'Production-signing-blocked' } default { 'Clean-host/elevated-blocked' } }
        [void]$lines.Add("### $title")
        $items = @($Status.frontier.$bucket)
        if ($items.Count -eq 0) { [void]$lines.Add('- None recorded.') } else { foreach ($item in $items) { [void]$lines.Add("- ``$($item.change)/$($item.task)``: $($item.reason) Next: $($item.nextAction)") } }
    }
    [void]$lines.Add('')
    [void]$lines.Add("Release candidate: **$($Status.release.classification)** ($($Status.release.status)); production readiness is never inferred from unsigned preparation.")
    return ($lines -join [Environment]::NewLine) + [Environment]::NewLine
}

function New-ProjectStatus {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [string] $OutputDirectory,
        [string] $ReleaseReportPath,
        [switch] $SkipCtest
    )

    $git = Invoke-ProjectStatusGit -RepoRoot $RepoRoot
    $tests = if ($SkipCtest) { [pscustomobject]@{ status = 'SKIPPED'; discoveredCount = $null; builds = @() } } else { Get-ProjectStatusCtestEvidence -RepoRoot $RepoRoot }
    $capabilities = Get-ProjectStatusCapabilityEvidence -RepoRoot $RepoRoot
    $openSpec = Get-ProjectStatusOpenSpecFrontier -RepoRoot $RepoRoot
    $release = Get-ProjectStatusReleaseEvidence -RepoRoot $RepoRoot -OutputDirectory $OutputDirectory -ReleaseReportPath $ReleaseReportPath
    $allTasks = @($openSpec.changes | ForEach-Object { $_.openTasks })
    $frontier = [ordered]@{ locallyCertified = @(); localOpen = @(); environmentBlocked = @(); providerBlocked = @(); productionSigningBlocked = @(); cleanHostElevatedBlocked = @() }
    foreach ($task in $allTasks) {
        $item = [pscustomobject]@{ change = $task.change; task = $task.task; category = $task.primaryCategory; status = $task.status; reason = ($task.reasons -join '; '); nextAction = $task.nextAction }
        if ($task.primaryCategory -eq 'production-signing') { $frontier.productionSigningBlocked += $item }
        elseif ($task.primaryCategory -eq 'external-provider') { $frontier.providerBlocked += $item }
        elseif ($task.primaryCategory -eq 'clean-host') { $frontier.cleanHostElevatedBlocked += $item }
        elseif ($task.primaryCategory -eq 'elevated') { $frontier.cleanHostElevatedBlocked += $item }
        elseif ($task.primaryCategory -eq 'interactive') { $frontier.environmentBlocked += $item }
        elseif ($task.status -eq 'PASS') { $frontier.locallyCertified += $item }
        else { $frontier.localOpen += $item }
    }
    $status = [pscustomobject]@{
        schemaVersion = $script:ProjectStatusSchemaVersion
        kind = 'fastfiles-project-status'
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        environment = [pscustomobject]@{ computer = if ($env:COMPUTERNAME) { '<local-host>' } else { $null }; os = [Environment]::OSVersion.VersionString; architecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString(); powershell = $PSVersionTable.PSVersion.ToString() }
        git = $git
        tests = $tests
        capabilities = $capabilities
        openSpec = $openSpec
        release = $release
        frontier = [pscustomobject]$frontier
    }
    if ($OutputDirectory) {
        New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
        $jsonPath = Join-Path $OutputDirectory 'project-status.json'
        $markdownPath = Join-Path $OutputDirectory 'project-status.md'
        $status | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $jsonPath -Encoding utf8NoBOM
        (New-ProjectStatusMarkdown -Status $status) | Set-Content -LiteralPath $markdownPath -Encoding utf8NoBOM
        $status | Add-Member -NotePropertyName jsonPath -NotePropertyValue (ConvertTo-ProjectStatusSafePath -Path $jsonPath -RepoRoot $RepoRoot)
        $status | Add-Member -NotePropertyName markdownPath -NotePropertyValue (ConvertTo-ProjectStatusSafePath -Path $markdownPath -RepoRoot $RepoRoot)
    }
    return $status
}

Export-ModuleMember -Function Get-ProjectStatusOutcome, ConvertTo-ProjectStatusSafePath, Get-ProjectStatusGitStateFromLines, Invoke-ProjectStatusGit, Get-ProjectStatusTaskClassification, Get-ProjectStatusOpenSpecFrontier, Get-ProjectStatusJUnitResult, Get-ProjectStatusCtestEvidence, Get-ProjectStatusCapabilityEvidence, Get-ProjectStatusReleaseEvidence, Test-ProjectStatusClaimConsistency, New-ProjectStatusMarkdown, New-ProjectStatus
