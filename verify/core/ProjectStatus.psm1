<#
    Machine-generated repository status/frontier (phase8-release-readiness-convergence).

    The module deliberately keeps collection and classification separate. The pure
    seams are used by the PowerShell fixture tests; collection is allowed to report
    missing tools/evidence, never to invent a pass. Paths leaving the repository are
    sanitized before they can reach a committed Markdown projection.
#>

$script:ProjectStatusSchemaVersion = 2
$script:ProjectStatusOutcomes = @('PASS', 'FAIL', 'SKIPPED', 'BLOCKED_ENVIRONMENT', 'REQUIRED-BUT-UNAVAILABLE')
$script:ProjectStatusCategories = @('local', 'interactive', 'elevated', 'external-provider', 'production-signing', 'clean-host', 'administrative')
$script:ProjectStatusLifecycleStates = @('active', 'completed', 'superseded', 'external-evidence-only', 'ambiguous')
$script:ProjectStatusTaskDispositions = @('active', 'external', 'historical', 'superseded', 'mixed')
$script:ProjectStatusEvidenceKinds = @('repository-path', 'git-commit', 'hosted-run', 'note')

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

function Test-ProjectStatusJsonArray {
    param([AllowNull()] $Value)
    return ($Value -is [System.Collections.IEnumerable]) -and ($Value -isnot [string]) -and ($Value -isnot [System.Collections.IDictionary])
}

function Test-ProjectStatusJsonObject {
    param([AllowNull()] $Value)
    return ($Value -is [System.Collections.IDictionary]) -or ($Value -is [pscustomobject])
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

function Resolve-ProjectStatusRepositoryEvidencePath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [AllowNull()] [string] $RelativePath,
        [bool] $RequireTracked = $false
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        return [pscustomobject]@{ valid = $false; path = $null; fullPath = $null; reason = 'evidence-repository-path-missing' }
    }
    $normalized = $RelativePath.Replace('/', '\')
    if ([IO.Path]::IsPathRooted($normalized) -or $normalized.StartsWith('\') -or $normalized -match '(^|\\)\.\.(\\|$)' -or $normalized -match '^[A-Za-z]:') {
        return [pscustomobject]@{ valid = $false; path = $null; fullPath = $null; reason = 'evidence-repository-path-unsafe' }
    }
    $segments = @($normalized -split '\\' | Where-Object { $_ -ne '' })
    if ($segments.Count -eq 0 -or @($segments | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) {
        return [pscustomobject]@{ valid = $false; path = $null; fullPath = $null; reason = 'evidence-repository-path-unsafe' }
    }

    try {
        $rootFull = ([IO.Path]::GetFullPath($RepoRoot)).TrimEnd('\')
        $fullPath = [IO.Path]::GetFullPath((Join-Path $rootFull $normalized))
    } catch {
        return [pscustomobject]@{ valid = $false; path = $null; fullPath = $null; reason = 'evidence-repository-path-invalid' }
    }
    $rootPrefix = $rootFull + '\'
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        return [pscustomobject]@{ valid = $false; path = $null; fullPath = $null; reason = 'evidence-repository-path-unsafe' }
    }

    $current = $rootFull
    foreach ($segment in $segments) {
        $current = Join-Path $current $segment
        if (-not (Test-Path -LiteralPath $current)) {
            return [pscustomobject]@{ valid = $false; path = $null; fullPath = $null; reason = 'evidence-repository-path-not-found' }
        }
        try {
            $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                return [pscustomobject]@{ valid = $false; path = $null; fullPath = $null; reason = 'evidence-repository-path-reparse-point' }
            }
        } catch {
            return [pscustomobject]@{ valid = $false; path = $null; fullPath = $null; reason = 'evidence-repository-path-unreadable' }
        }
    }

    if ($RequireTracked) {
        $tracked = @(& git -C $rootFull ls-files --error-unmatch -- $normalized 2>$null)
        if ($LASTEXITCODE -ne 0 -or $tracked.Count -eq 0) {
            return [pscustomobject]@{ valid = $false; path = $null; fullPath = $null; reason = 'evidence-repository-path-untracked' }
        }
    }
    [pscustomobject]@{
        valid = $true
        path = $normalized.Replace('\', '/')
        fullPath = $fullPath
        reason = $null
    }
}

function New-ProjectStatusGitEvidenceContext {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $RepoRoot)

    $headLines = @(& git -C $RepoRoot rev-parse --verify HEAD 2>$null)
    if ($LASTEXITCODE -ne 0 -or $headLines.Count -eq 0) { return $null }
    $head = ($headLines -join '').Trim().ToLowerInvariant()
    $reachableLines = @(& git -C $RepoRoot rev-list $head 2>$null)
    if ($LASTEXITCODE -ne 0) {
        return [pscustomobject]@{ repoRoot = [IO.Path]::GetFullPath($RepoRoot); head = $head; reachable = $null }
    }
    $reachable = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in $reachableLines) {
        $candidate = $line.ToString().Trim()
        if ($candidate -match '^[0-9a-fA-F]{40}$') { [void]$reachable.Add($candidate) }
    }
    [pscustomobject]@{ repoRoot = [IO.Path]::GetFullPath($RepoRoot); head = $head; reachable = $reachable }
}

function Resolve-ProjectStatusGitCommitEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [AllowNull()] [string] $Sha,
        [AllowNull()] $GitContext
    )

    if ([string]::IsNullOrWhiteSpace($Sha) -or $Sha -notmatch '^[0-9a-fA-F]{40}$') {
        return [pscustomobject]@{ valid = $false; sha = $null; reason = 'evidence-git-commit-sha-not-full' }
    }
    $normalized = $Sha.ToLowerInvariant()
    $repoFull = [IO.Path]::GetFullPath($RepoRoot)
    $contextMatches = $null -ne $GitContext -and
        [String]::Equals([string]$GitContext.repoRoot, $repoFull, [StringComparison]::OrdinalIgnoreCase)
    if (-not $contextMatches) {
        $GitContext = New-ProjectStatusGitEvidenceContext -RepoRoot $RepoRoot
    }
    if ($null -ne $GitContext -and $null -ne $GitContext.reachable) {
        if ($GitContext.reachable.Contains($normalized)) {
            return [pscustomobject]@{ valid = $true; sha = $normalized; reason = $null }
        }
        $resolved = @(& git -C $RepoRoot rev-parse --verify "${normalized}^{commit}" 2>$null)
        if ($LASTEXITCODE -ne 0 -or $resolved.Count -eq 0 -or ($resolved -join '').Trim() -ne $normalized) {
            return [pscustomobject]@{ valid = $false; sha = $normalized; reason = 'evidence-git-commit-not-found' }
        }
        return [pscustomobject]@{ valid = $false; sha = $normalized; reason = 'evidence-git-commit-not-reachable' }
    }

    $resolved = @(& git -C $RepoRoot rev-parse --verify "${normalized}^{commit}" 2>$null)
    if ($LASTEXITCODE -ne 0 -or $resolved.Count -eq 0 -or ($resolved -join '').Trim() -ne $normalized) {
        return [pscustomobject]@{ valid = $false; sha = $normalized; reason = 'evidence-git-commit-not-found' }
    }
    $headLines = @(& git -C $RepoRoot rev-parse --verify HEAD 2>$null)
    if ($LASTEXITCODE -ne 0 -or $headLines.Count -eq 0) {
        return [pscustomobject]@{ valid = $false; sha = $normalized; reason = 'evidence-git-head-not-found' }
    }
    $head = ($headLines -join '').Trim()
    & git -C $RepoRoot merge-base --is-ancestor $normalized $head 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
        return [pscustomobject]@{ valid = $false; sha = $normalized; reason = 'evidence-git-commit-not-reachable' }
    }
    [pscustomobject]@{ valid = $true; sha = $normalized; reason = $null }
}

function Resolve-ProjectStatusEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [AllowNull()] $Evidence,
        [Parameter(Mandatory)] [string] $Context,
        [AllowNull()] $GitContext
    )

    $errors = [System.Collections.Generic.List[string]]::new()
    $kind = if (Test-ProjectStatusJsonObject -Value $Evidence) { [string]$Evidence.kind } else { '' }
    $projection = [ordered]@{
        kind = if ([string]::IsNullOrWhiteSpace($kind)) { 'invalid' } else { $kind }
        valid = $false
        authoritative = $false
        authority = 'invalid'
        source = '<invalid-evidence>'
    }
    if (-not (Test-ProjectStatusJsonObject -Value $Evidence)) {
        [void]$errors.Add("${Context}:evidence-object-required")
        return [pscustomobject]@{ valid = $false; authoritative = $false; value = [pscustomobject]$projection; errors = @($errors) }
    }
    if ($script:ProjectStatusEvidenceKinds -notcontains $kind) {
        [void]$errors.Add("${Context}:evidence-kind-invalid")
        return [pscustomobject]@{ valid = $false; authoritative = $false; value = [pscustomobject]$projection; errors = @($errors) }
    }

    switch ($kind) {
        'repository-path' {
            $resolvedPath = Resolve-ProjectStatusRepositoryEvidencePath -RepoRoot $RepoRoot -RelativePath ([string]$Evidence.path)
            if (-not $resolvedPath.valid) {
                [void]$errors.Add("${Context}:$($resolvedPath.reason)")
            } else {
                $projection.path = $resolvedPath.path
                $projection.source = $resolvedPath.path
                $projection.authority = 'repository-artifact'
                $projection.authoritative = $true
                $projection.valid = $true
            }
        }
        'git-commit' {
            $resolvedCommit = Resolve-ProjectStatusGitCommitEvidence -RepoRoot $RepoRoot -Sha ([string]$Evidence.sha) -GitContext $GitContext
            if (-not $resolvedCommit.valid) {
                [void]$errors.Add("${Context}:$($resolvedCommit.reason)")
            } else {
                $projection.sha = $resolvedCommit.sha
                $projection.source = $resolvedCommit.sha
                $projection.authority = 'git-commit'
                $projection.authoritative = $true
                $projection.valid = $true
            }
        }
        'hosted-run' {
            $runId = [string]$Evidence.runId
            if ([string]::IsNullOrWhiteSpace($runId) -or $runId -notmatch '^[0-9]+$') {
                [void]$errors.Add("${Context}:evidence-hosted-run-id-invalid")
            }
            $resolvedCommit = Resolve-ProjectStatusGitCommitEvidence -RepoRoot $RepoRoot -Sha ([string]$Evidence.sha) -GitContext $GitContext
            if (-not $resolvedCommit.valid) { [void]$errors.Add("${Context}:$($resolvedCommit.reason)") }
            $recordPath = Resolve-ProjectStatusRepositoryEvidencePath -RepoRoot $RepoRoot -RelativePath ([string]$Evidence.record) -RequireTracked:$true
            if (-not $recordPath.valid) {
                [void]$errors.Add("${Context}:hosted-run-record-$($recordPath.reason)")
            } elseif (-not (Test-Path -LiteralPath $recordPath.fullPath -PathType Leaf)) {
                [void]$errors.Add("${Context}:evidence-hosted-run-record-not-file")
            } else {
                try {
                    $recordText = [IO.File]::ReadAllText($recordPath.fullPath)
                    $runIdPattern = "(?<![0-9])$([regex]::Escape($runId))(?![0-9])"
                    if ($recordText -notmatch $runIdPattern) { [void]$errors.Add("${Context}:evidence-hosted-run-record-run-id-missing") }
                    if ($resolvedCommit.valid) {
                        $shaPattern = "(?<![0-9a-fA-F])$([regex]::Escape($resolvedCommit.sha))(?![0-9a-fA-F])"
                        if ($recordText -notmatch $shaPattern) { [void]$errors.Add("${Context}:evidence-hosted-run-record-sha-missing") }
                    }
                } catch {
                    [void]$errors.Add("${Context}:evidence-hosted-run-record-unreadable")
                }
            }
            if ($errors.Count -eq 0) {
                $projection.runId = $runId
                $projection.sha = $resolvedCommit.sha
                $projection.record = $recordPath.path
                $projection.source = $recordPath.path
                $projection.authority = 'hosted-run-binding'
                $projection.authoritative = $true
                $projection.valid = $true
            }
        }
        'note' {
            $note = [string]$Evidence.text
            if ([string]::IsNullOrWhiteSpace($note)) {
                [void]$errors.Add("${Context}:evidence-note-missing")
            } else {
                $projection.text = ConvertTo-ProjectStatusSafeText -Text $note
                $projection.source = 'note'
                $projection.authority = 'non-authoritative-note'
                $projection.authoritative = $false
                $projection.valid = $true
            }
        }
    }
    [pscustomobject]@{ valid = ($errors.Count -eq 0 -and $projection.valid); authoritative = ($errors.Count -eq 0 -and $projection.authoritative); value = [pscustomobject]$projection; errors = @($errors) }
}

function Test-ProjectStatusHasAuthoritativeEvidence {
    param([AllowEmptyCollection()] [object[]] $Evidence)
    return @($Evidence | Where-Object { $_.valid -eq $true -and $_.authoritative -eq $true }).Count -gt 0
}

function Get-ProjectStatusLifecycleModel {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $RepoRoot)

    $path = Join-Path $RepoRoot 'openspec\lifecycle.json'
    $structuralErrors = [System.Collections.Generic.List[string]]::new()
    $evidenceErrors = [System.Collections.Generic.List[string]]::new()
    $semanticErrors = [System.Collections.Generic.List[string]]::new()
    $entries = [System.Collections.Generic.List[object]]::new()
    $changesRoot = Join-Path $RepoRoot 'openspec\changes'
    $result = [ordered]@{
        status = 'PASS'
        schemaVersion = 2
        kind = 'fastfiles-openspec-lifecycle'
        source = (ConvertTo-ProjectStatusSafePath -Path $path -RepoRoot $RepoRoot)
        entries = @()
        errors = @()
        structuralErrors = @()
        evidenceErrors = @()
        semanticErrors = @()
        integrity = [pscustomobject]@{ status = 'PASS'; structuralErrors = @(); evidenceErrors = @(); semanticErrors = @() }
    }

    $raw = $null
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        [void]$structuralErrors.Add('openspec-lifecycle-metadata-missing')
    } else {
        try {
            $raw = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -ErrorAction Stop
        } catch {
            [void]$structuralErrors.Add('openspec-lifecycle-metadata-invalid-json')
        }
    }

    $knownChanges = @()
    if (Test-Path -LiteralPath $changesRoot -PathType Container) {
        $knownChanges = @(Get-ChildItem -LiteralPath $changesRoot -Directory | Sort-Object Name | Select-Object -ExpandProperty Name)
    } else {
        [void]$structuralErrors.Add('openspec-changes-directory-missing')
    }
    $knownChangeSet = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($known in $knownChanges) { [void]$knownChangeSet.Add($known) }
    $seenNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $entryByName = @{}
    # completion-integrity-hardening-and-release-certification 2.6: validate
    # all lifecycle commit evidence against one reachable-commit snapshot so
    # status remains truthful without spawning three Git processes per item.
    $gitEvidenceContext = $null
    $gitEvidenceContextInitialized = $false

    if ($null -ne $raw) {
        $rawSchemaVersion = 0
        $schemaVersionValue = $raw.schemaVersion
        $schemaVersionType = if ($null -eq $schemaVersionValue) { '' } else { $schemaVersionValue.GetType().Name }
        if ($schemaVersionType -in @('Byte', 'SByte', 'Int16', 'UInt16', 'Int32', 'UInt32', 'Int64', 'UInt64')) {
            try { $rawSchemaVersion = [int]$schemaVersionValue } catch { $rawSchemaVersion = 0 }
        }
        $result.schemaVersion = $rawSchemaVersion
        if ($rawSchemaVersion -ne 2) { [void]$structuralErrors.Add('openspec-lifecycle-schema-version-invalid') }
        if ([string]$raw.kind -ne 'fastfiles-openspec-lifecycle') { [void]$structuralErrors.Add('openspec-lifecycle-kind-invalid') }
        if ($null -eq $raw.changes) { [void]$structuralErrors.Add('openspec-lifecycle-changes-missing') }
        elseif (-not (Test-ProjectStatusJsonArray -Value $raw.changes)) { [void]$structuralErrors.Add('openspec-lifecycle-changes-not-array') }

        foreach ($entry in @($raw.changes)) {
            if (-not (Test-ProjectStatusJsonObject -Value $entry)) { [void]$structuralErrors.Add('openspec-lifecycle-change-not-object'); continue }
            $name = [string]$entry.name
            $state = [string]$entry.state
            if ([string]::IsNullOrWhiteSpace($name)) { [void]$structuralErrors.Add('openspec-lifecycle-change-name-missing'); continue }
            if (-not $seenNames.Add($name)) { [void]$structuralErrors.Add("openspec-lifecycle-change-duplicate:$name") }
            if (-not $knownChangeSet.Contains($name)) { [void]$structuralErrors.Add("openspec-lifecycle-change-unknown:$name") }
            if ($script:ProjectStatusLifecycleStates -notcontains $state) { [void]$structuralErrors.Add("openspec-lifecycle-state-invalid:$name") }

            $resolvedEvidence = [System.Collections.Generic.List[object]]::new()
            if (-not (Test-ProjectStatusJsonArray -Value $entry.evidence)) {
                [void]$structuralErrors.Add("openspec-lifecycle-evidence-not-array:$name")
            } else {
                $rawEvidence = @($entry.evidence)
                if ($rawEvidence.Count -eq 0) { [void]$structuralErrors.Add("openspec-lifecycle-evidence-missing:$name") }
                foreach ($evidence in $rawEvidence) {
                    if (-not $gitEvidenceContextInitialized -and
                        ([string]$evidence.kind -in @('git-commit', 'hosted-run'))) {
                        $gitEvidenceContext = New-ProjectStatusGitEvidenceContext -RepoRoot $RepoRoot
                        $gitEvidenceContextInitialized = $true
                    }
                    $resolved = Resolve-ProjectStatusEvidence -RepoRoot $RepoRoot -Evidence $evidence -Context "openspec-lifecycle/$name" -GitContext $gitEvidenceContext
                    if (-not $resolved.valid) { foreach ($error in @($resolved.errors)) { [void]$evidenceErrors.Add($error) } }
                    $resolvedEvidence.Add($resolved.value)
                }
            }

            $supersededBy = if ($null -eq $entry.supersededBy) { $null } else { [string]$entry.supersededBy }
            if ($state -eq 'superseded') {
                if ([string]::IsNullOrWhiteSpace($supersededBy)) { [void]$structuralErrors.Add("openspec-lifecycle-superseded-target-missing:$name") }
            } elseif (-not [string]::IsNullOrWhiteSpace($supersededBy)) {
                [void]$structuralErrors.Add("openspec-lifecycle-unexpected-superseded-target:$name")
            }

            $overrides = [System.Collections.Generic.List[object]]::new()
            $seenTasks = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            if (-not (Test-ProjectStatusJsonArray -Value $entry.taskOverrides)) {
                [void]$structuralErrors.Add("openspec-lifecycle-task-overrides-not-array:$name")
            } else {
                foreach ($override in @($entry.taskOverrides)) {
                    if (-not (Test-ProjectStatusJsonObject -Value $override)) { [void]$structuralErrors.Add("openspec-lifecycle-task-override-not-object:$name"); continue }
                    $taskId = [string]$override.task
                    if ($taskId -notmatch '^[0-9]+\.[0-9]+$') { [void]$structuralErrors.Add("openspec-lifecycle-task-id-invalid:$name/$taskId") }
                    if (-not $seenTasks.Add($taskId)) { [void]$structuralErrors.Add("openspec-lifecycle-task-duplicate:$name/$taskId") }
                    $disposition = [string]$override.disposition
                    if ($script:ProjectStatusTaskDispositions -notcontains $disposition) { [void]$structuralErrors.Add("openspec-lifecycle-disposition-invalid:$name/$taskId") }
                    if (-not (Test-ProjectStatusJsonArray -Value $override.categories)) { [void]$structuralErrors.Add("openspec-lifecycle-categories-not-array:$name/$taskId") }
                    $categories = @($override.categories | ForEach-Object { [string]$_ })
                    if ($categories.Count -eq 0) { [void]$structuralErrors.Add("openspec-lifecycle-categories-missing:$name/$taskId") }
                    foreach ($category in $categories) {
                        if ($script:ProjectStatusCategories -notcontains $category) { [void]$structuralErrors.Add("openspec-lifecycle-category-invalid:$name/$taskId/$category") }
                    }
                    $overrideStatus = [string]$override.status
                    if ($script:ProjectStatusOutcomes -notcontains $overrideStatus) { [void]$structuralErrors.Add("openspec-lifecycle-status-invalid:$name/$taskId") }
                    $overrideEvidence = [System.Collections.Generic.List[object]]::new()
                    if (-not (Test-ProjectStatusJsonArray -Value $override.evidence)) {
                        [void]$structuralErrors.Add("openspec-lifecycle-task-evidence-not-array:$name/$taskId")
                    } else {
                        $rawOverrideEvidence = @($override.evidence)
                        if ($rawOverrideEvidence.Count -eq 0) { [void]$structuralErrors.Add("openspec-lifecycle-task-evidence-missing:$name/$taskId") }
                        foreach ($evidence in $rawOverrideEvidence) {
                            if (-not $gitEvidenceContextInitialized -and
                                ([string]$evidence.kind -in @('git-commit', 'hosted-run'))) {
                                $gitEvidenceContext = New-ProjectStatusGitEvidenceContext -RepoRoot $RepoRoot
                                $gitEvidenceContextInitialized = $true
                            }
                            $resolved = Resolve-ProjectStatusEvidence -RepoRoot $RepoRoot -Evidence $evidence -Context "openspec-lifecycle/$name/$taskId" -GitContext $gitEvidenceContext
                            if (-not $resolved.valid) { foreach ($error in @($resolved.errors)) { [void]$evidenceErrors.Add($error) } }
                            $overrideEvidence.Add($resolved.value)
                        }
                    }
                    $overrides.Add([pscustomobject]@{
                        task = $taskId
                        disposition = $disposition
                        categories = @($categories)
                        status = $overrideStatus
                        evidence = @($overrideEvidence)
                    })
                }
            }

            $declaredTaskIds = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            $entryTasksPath = Join-Path (Join-Path $changesRoot $name) 'tasks.md'
            if (Test-Path -LiteralPath $entryTasksPath -PathType Leaf) {
                foreach ($taskLine in [IO.File]::ReadAllLines($entryTasksPath)) {
                    if ($taskLine -match '^\s*-\s+\[[ xX]\]\s+([0-9]+\.[0-9]+)\s+') { [void]$declaredTaskIds.Add($Matches[1]) }
                }
            }
            foreach ($override in @($overrides)) {
                if (-not $declaredTaskIds.Contains([string]$override.task)) { [void]$structuralErrors.Add("openspec-lifecycle-task-unknown:$name/$($override.task)") }
            }

            $entryModel = [pscustomobject]@{
                name = $name
                state = $state
                evidence = @($resolvedEvidence)
                supersededBy = $supersededBy
                taskOverrides = @($overrides)
            }
            $entries.Add($entryModel)
            $entryByName[$name] = $entryModel
        }

        # Every change with an unchecked task must opt into an explicit lifecycle.
        if (Test-Path -LiteralPath $changesRoot -PathType Container) {
            foreach ($tasksFile in @(Get-ChildItem -LiteralPath $changesRoot -Filter tasks.md -Recurse -File | Sort-Object FullName)) {
                $changeName = Split-Path (Split-Path $tasksFile.FullName -Parent) -Leaf
                $hasOpenTask = $false
                foreach ($line in [IO.File]::ReadAllLines($tasksFile.FullName)) {
                    if ($line -match '^\s*-\s+\[\s\]\s+[0-9]+\.[0-9]+\s+') { $hasOpenTask = $true; break }
                }
                if ($hasOpenTask -and -not $seenNames.Contains($changeName)) { [void]$structuralErrors.Add("openspec-lifecycle-entry-missing:$changeName") }
            }
        }
    }

    foreach ($entry in @($entries)) {
        $hasAuthoritative = Test-ProjectStatusHasAuthoritativeEvidence -Evidence @($entry.evidence)
        if ($entry.state -in @('completed', 'superseded', 'external-evidence-only') -and -not $hasAuthoritative) {
            [void]$semanticErrors.Add("openspec-lifecycle-authoritative-evidence-required:$($entry.name)")
        }
        if ($entry.state -eq 'superseded') {
            if ([string]::IsNullOrWhiteSpace([string]$entry.supersededBy)) { continue }
            if ($entry.name -ieq [string]$entry.supersededBy) { [void]$semanticErrors.Add("openspec-lifecycle-supersession-self:$($entry.name)"); continue }
            if (-not $entryByName.ContainsKey([string]$entry.supersededBy)) {
                [void]$semanticErrors.Add("openspec-lifecycle-superseded-target-unknown:$($entry.name)")
            } else {
                $target = $entryByName[[string]$entry.supersededBy]
                if ($target.state -in @('superseded', 'ambiguous')) { [void]$semanticErrors.Add("openspec-lifecycle-superseded-target-not-authoritative:$($entry.name)") }
            }
        }
        foreach ($override in @($entry.taskOverrides)) {
            $categories = @($override.categories)
            $hasLocal = $categories -contains 'local'
            $hasExternal = @($categories | Where-Object { $_ -ne 'local' }).Count -gt 0
            $hasAuthoritativeOverrideEvidence = Test-ProjectStatusHasAuthoritativeEvidence -Evidence @($override.evidence)
            switch ([string]$override.disposition) {
                'active' {
                    if ($entry.state -ne 'active') { [void]$semanticErrors.Add("openspec-lifecycle-active-task-parent-not-active:$($entry.name)/$($override.task)") }
                    if (-not $hasLocal) { [void]$semanticErrors.Add("openspec-lifecycle-active-task-local-category-required:$($entry.name)/$($override.task)") }
                    if ([string]$override.status -eq 'PASS') { [void]$semanticErrors.Add("openspec-lifecycle-active-unchecked-pass:$($entry.name)/$($override.task)") }
                }
                'external' {
                    if (-not $hasExternal) { [void]$semanticErrors.Add("openspec-lifecycle-external-task-category-required:$($entry.name)/$($override.task)") }
                    if ($hasLocal) { [void]$semanticErrors.Add("openspec-lifecycle-external-task-local-category-contradiction:$($entry.name)/$($override.task)") }
                }
                'historical' {
                    if ($entry.state -notin @('completed', 'external-evidence-only')) { [void]$semanticErrors.Add("openspec-lifecycle-historical-task-parent-invalid:$($entry.name)/$($override.task)") }
                }
                'superseded' {
                    if ($entry.state -ne 'superseded' -or [string]::IsNullOrWhiteSpace([string]$entry.supersededBy)) { [void]$semanticErrors.Add("openspec-lifecycle-superseded-task-parent-invalid:$($entry.name)/$($override.task)") }
                }
                'mixed' {
                    if (-not $hasLocal) { [void]$semanticErrors.Add("openspec-lifecycle-mixed-task-local-component-required:$($entry.name)/$($override.task)") }
                    if (-not $hasExternal) { [void]$semanticErrors.Add("openspec-lifecycle-mixed-task-external-component-required:$($entry.name)/$($override.task)") }
                }
            }
            if ([string]$override.disposition -ne 'active' -and -not $hasAuthoritativeOverrideEvidence) {
                [void]$semanticErrors.Add("openspec-lifecycle-task-authoritative-evidence-required:$($entry.name)/$($override.task)")
            }
            if ([string]$override.status -eq 'PASS' -and -not $hasAuthoritativeOverrideEvidence) {
                [void]$semanticErrors.Add("openspec-lifecycle-pass-task-evidence-required:$($entry.name)/$($override.task)")
            }
            if ([string]$override.status -eq 'BLOCKED_ENVIRONMENT' -and @($categories | Where-Object { $_ -in @('interactive', 'elevated', 'clean-host') }).Count -eq 0) {
                [void]$semanticErrors.Add("openspec-lifecycle-environment-status-category-mismatch:$($entry.name)/$($override.task)")
            }
            if ([string]$override.status -eq 'REQUIRED-BUT-UNAVAILABLE' -and -not $hasExternal) {
                [void]$semanticErrors.Add("openspec-lifecycle-required-status-category-mismatch:$($entry.name)/$($override.task)")
            }
        }
    }

    # A supersession target is a directed graph. Detect self-links above and all longer cycles here.
    foreach ($entry in @($entries | Where-Object { $_.state -eq 'superseded' -and -not [string]::IsNullOrWhiteSpace([string]$_.supersededBy) })) {
        $visited = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $current = [string]$entry.name
        while ($entryByName.ContainsKey($current) -and $entryByName[$current].state -eq 'superseded') {
            if (-not $visited.Add($current)) {
                [void]$semanticErrors.Add("openspec-lifecycle-supersession-cycle:$($entry.name)")
                break
            }
            $current = [string]$entryByName[$current].supersededBy
            if ([string]::IsNullOrWhiteSpace($current)) { break }
        }
    }

    $allErrors = @($structuralErrors + $evidenceErrors + $semanticErrors)
    $result.status = if ($allErrors.Count -eq 0) { 'PASS' } else { 'FAIL' }
    $result.entries = @($entries)
    $result.errors = @($allErrors)
    $result.structuralErrors = @($structuralErrors)
    $result.evidenceErrors = @($evidenceErrors)
    $result.semanticErrors = @($semanticErrors)
    $result.integrity = [pscustomobject]@{
        status = $result.status
        structuralErrors = @($structuralErrors)
        evidenceErrors = @($evidenceErrors)
        semanticErrors = @($semanticErrors)
    }
    return [pscustomobject]$result
}

function Get-ProjectStatusLifecycleEntry {
    param([Parameter(Mandatory)] $Model, [Parameter(Mandatory)] [string] $ChangeName)
    return @($Model.entries | Where-Object { $_.name -ieq $ChangeName } | Select-Object -First 1)[0]
}

function Get-ProjectStatusLifecycleOverride {
    param([AllowNull()] $Entry, [Parameter(Mandatory)] [string] $TaskId)
    if ($null -eq $Entry) { return $null }
    return @($Entry.taskOverrides | Where-Object { $_.task -eq $TaskId } | Select-Object -First 1)[0]
}

function Get-ProjectStatusTaskClassification {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $TaskText,
        [Parameter(Mandatory)] [string] $ChangeName,
        [Parameter(Mandatory)] [string] $TaskId,
        [Parameter(Mandatory)] [string] $SourcePath,
        [int] $SourceLine = 0,
        [AllowEmptyCollection()] [string[]] $ExplicitCategories = @(),
        [AllowNull()] [string] $ExplicitStatus
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
    $inferredCategories = [System.Collections.Generic.List[string]]::new()
    foreach ($rule in $rules) {
        if ($text -match $rule.Pattern) {
            [void]$inferredCategories.Add($rule.Name)
        }
    }
    $explicit = @($ExplicitCategories | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)
    $classificationSource = 'task-text'
    if ($explicit.Count -gt 0) {
        $classificationSource = 'explicit-lifecycle-metadata'
        foreach ($category in $explicit) { [void]$categories.Add($category) }
    } else {
        foreach ($category in $inferredCategories) { [void]$categories.Add($category) }
    }
    if ($categories.Count -eq 0) {
        [void]$categories.Add('local')
        [void]$reasons.Add('locally actionable unless evidence proves otherwise')
    } else {
        $reasonByCategory = @{
            'production-signing' = 'requires authorized production signing or trust evidence'
            'external-provider' = 'requires an isolated/provider execution target or lawful source image'
            'clean-host' = 'requires an uncontaminated supported Windows host'
            'interactive' = 'requires an attached interactive desktop or physical display configuration'
            'elevated' = 'requires an elevated or privileged validation context'
            'administrative' = 'requires external GitHub/remote or release administration'
            'local' = 'locally actionable unless evidence proves otherwise'
        }
        foreach ($category in @($categories | Select-Object -Unique)) {
            if ($reasonByCategory.ContainsKey($category)) { [void]$reasons.Add($reasonByCategory[$category]) }
        }
    }
    $primary = $categories[0]
    $status = if (-not [string]::IsNullOrWhiteSpace($ExplicitStatus)) { $ExplicitStatus }
        elseif ($categories -contains 'production-signing' -or $categories -contains 'external-provider' -or $categories -contains 'clean-host' -or $categories -contains 'administrative') { 'REQUIRED-BUT-UNAVAILABLE' }
        elseif ($categories -contains 'interactive' -or $categories -contains 'elevated') { 'BLOCKED_ENVIRONMENT' }
        else { 'SKIPPED' }
    $nextAction = switch ($primary) {
        'production-signing' { 'Supply authorized signer/pins, then run the task-specific release validation command.'; break }
        'external-provider' { 'Supply a lawful Windows guest/template and run the provider preflight before the task capability.'; break }
        'clean-host' { 'Run on a clean supported Windows host and attach the task-specific evidence.'; break }
        'interactive' { 'Run the semantic UIA/visual command in the required interactive desktop configuration.'; break }
        'elevated' { 'Run the documented command with explicit elevation on a disposable or authorized host.'; break }
        'administrative' { 'Resolve the authenticated remote/PR or release-administration prerequisite and record exact evidence.'; break }
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
        inferredCategories = @($inferredCategories)
        classificationSource = $classificationSource
        classificationConfidence = if ($classificationSource -eq 'explicit-lifecycle-metadata') { 'explicit' } elseif ($categories.Count -eq 1 -and $primary -eq 'local') { 'low' } else { 'high' }
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
        return [pscustomobject]@{ status = 'FAIL'; reason = 'openspec-changes-directory-missing'; totalOpenTasks = 0; activeLocalOpenTasks = 0; changesWithOpenTasks = 0; categories = [pscustomobject]@{}; lifecycle = [pscustomobject]@{ status = 'FAIL'; errors = @('openspec-changes-directory-missing') }; changes = @() }
    }
    $lifecycle = Get-ProjectStatusLifecycleModel -RepoRoot $RepoRoot
    foreach ($tasksFile in @(Get-ChildItem -LiteralPath $changesRoot -Filter tasks.md -Recurse -File | Sort-Object FullName)) {
        $changeName = Split-Path (Split-Path $tasksFile.FullName -Parent) -Leaf
        $lifecycleEntry = Get-ProjectStatusLifecycleEntry -Model $lifecycle -ChangeName $changeName
        $changeState = if ($lifecycle.status -eq 'PASS' -and $null -ne $lifecycleEntry) { [string]$lifecycleEntry.state } else { 'unknown' }
        $open = [System.Collections.Generic.List[object]]::new()
        $lineNumber = 0
        foreach ($line in [IO.File]::ReadAllLines($tasksFile.FullName)) {
            $lineNumber++
            if ($line -match '^\s*-\s+\[\s\]\s+([0-9]+\.[0-9]+)\s+(.*)$') {
                $taskId = $Matches[1]
                $taskText = $Matches[2]
                $safeSource = "openspec/changes/$changeName/tasks.md:$lineNumber"
                $override = Get-ProjectStatusLifecycleOverride -Entry $lifecycleEntry -TaskId $taskId
                $explicitCategories = if ($null -ne $override) { @($override.categories) } else { @() }
                $explicitStatus = if ($null -ne $override) { [string]$override.status } else { $null }
                $classification = Get-ProjectStatusTaskClassification -TaskText $taskText -ChangeName $changeName -TaskId $taskId -SourcePath $safeSource -SourceLine $lineNumber -ExplicitCategories $explicitCategories -ExplicitStatus $explicitStatus
                $disposition = $null
                if ($null -ne $override) {
                    $disposition = [string]$override.disposition
                } elseif ($lifecycle.status -ne 'PASS' -or $changeState -eq 'unknown') {
                    $disposition = 'mixed'
                } elseif ($changeState -eq 'active') {
                    $disposition = 'active'
                } elseif ($changeState -eq 'superseded') {
                    $disposition = 'superseded'
                } elseif ($changeState -eq 'completed') {
                    $disposition = if ($classification.primaryCategory -eq 'local') { 'historical' } else { 'external' }
                } elseif ($changeState -eq 'external-evidence-only') {
                    $disposition = if ($classification.primaryCategory -eq 'local') { 'historical' } else { 'external' }
                } else {
                    $disposition = 'mixed'
                }
                $evidence = if ($null -ne $override) { @($override.evidence) } elseif ($null -ne $lifecycleEntry) { @($lifecycleEntry.evidence) } else { @() }
                $classification | Add-Member -NotePropertyName lifecycleState -NotePropertyValue $changeState
                $classification | Add-Member -NotePropertyName taskDisposition -NotePropertyValue $disposition
                $classification | Add-Member -NotePropertyName lifecycleEvidence -NotePropertyValue @($evidence)
                $classification | Add-Member -NotePropertyName lifecycleSource -NotePropertyValue 'openspec/lifecycle.json'
                $open.Add($classification)
            }
        }
        if ($open.Count -gt 0) {
            $orderedOpen = @($open | Sort-Object @{ Expression = { [int](($_.task -split '\.')[0]) } }, @{ Expression = { [int](($_.task -split '\.')[1]) } }, sourceLine)
            $changes.Add([pscustomobject]@{
                name = $changeName
                tasksPath = "openspec/changes/$changeName/tasks.md"
                lifecycleState = $changeState
                lifecycleEvidence = if ($null -ne $lifecycleEntry) { @($lifecycleEntry.evidence) } else { @() }
                supersededBy = if ($null -ne $lifecycleEntry) { $lifecycleEntry.supersededBy } else { $null }
                openTaskCount = $orderedOpen.Count
                openTasks = @($orderedOpen)
            })
        }
    }
    $allTasks = @($changes | ForEach-Object { $_.openTasks })
    $categoryCounts = [ordered]@{
        local = @($allTasks | Where-Object primaryCategory -eq 'local').Count
        interactive = @($allTasks | Where-Object primaryCategory -eq 'interactive').Count
        elevated = @($allTasks | Where-Object primaryCategory -eq 'elevated').Count
        externalProvider = @($allTasks | Where-Object primaryCategory -eq 'external-provider').Count
        productionSigning = @($allTasks | Where-Object primaryCategory -eq 'production-signing').Count
        cleanHost = @($allTasks | Where-Object primaryCategory -eq 'clean-host').Count
        administrative = @($allTasks | Where-Object primaryCategory -eq 'administrative').Count
    }
    [pscustomobject]@{
        status = $lifecycle.status
        reason = if ($lifecycle.status -eq 'PASS') { $null } else { 'openspec-lifecycle-model-invalid' }
        totalOpenTasks = $allTasks.Count
        activeLocalOpenTasks = @($allTasks | Where-Object { $_.taskDisposition -eq 'active' -and $_.primaryCategory -eq 'local' }).Count
        changesWithOpenTasks = $changes.Count
        lifecycle = [pscustomobject]@{
            status = $lifecycle.status
            schemaVersion = $lifecycle.schemaVersion
            source = $lifecycle.source
            errors = @($lifecycle.errors)
            structuralErrors = @($lifecycle.structuralErrors)
            evidenceErrors = @($lifecycle.evidenceErrors)
            semanticErrors = @($lifecycle.semanticErrors)
            integrity = $lifecycle.integrity
            entries = @($lifecycle.entries)
            declaredChangeCount = @($lifecycle.entries).Count
        }
        categories = [pscustomobject]$categoryCounts
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

function Get-ProjectStatusVerificationEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [string] $RunResultPath
    )

    if ([string]::IsNullOrWhiteSpace($RunResultPath)) {
        return [pscustomobject]@{
            status = 'SKIPPED'; classification = 'NOT_SELECTED'; reason = 'explicit-verification-run-result-not-supplied'
            source = $null; runId = $null; invocationId = $null; capabilityCount = 0; passCount = 0; skipCount = 0; failCount = 0
        }
    }
    $resolved = $null
    try {
        $candidatePath = if ([IO.Path]::IsPathRooted($RunResultPath)) { $RunResultPath } else { Join-Path $RepoRoot $RunResultPath }
        $resolved = [IO.Path]::GetFullPath($candidatePath)
    } catch {
        return [pscustomobject]@{ status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'verification-run-result-path-invalid'; source = '<invalid-path>' }
    }
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        return [pscustomobject]@{ status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'verification-run-result-not-found'; source = (ConvertTo-ProjectStatusSafePath -Path $resolved -RepoRoot $RepoRoot) }
    }
    try {
        $run = [IO.File]::ReadAllText($resolved) | ConvertFrom-Json
        foreach ($name in @('runId', 'invocationId', 'status', 'classification', 'capabilities')) {
            if ($null -eq $run.PSObject.Properties[$name]) { throw "verification run result is missing '$name'" }
        }
        if ([string]$run.status -notin @('PASS', 'FAIL', 'SKIPPED')) { throw "verification run result has unsupported status '$($run.status)'" }
        $capabilities = @($run.capabilities | Where-Object { $null -ne $_ })
        $fails = @($capabilities | Where-Object { [string]$_.status -eq 'FAIL' })
        $passes = @($capabilities | Where-Object { [string]$_.status -eq 'PASS' })
        $skips = @($capabilities | Where-Object { [string]$_.status -eq 'SKIPPED' })
        $contradiction = $null
        if ($fails.Count -gt 0 -and [string]$run.status -ne 'FAIL') { $contradiction = 'run-result-status-does-not-reflect-failing-capability' }
        elseif ($fails.Count -eq 0 -and $passes.Count -eq 0 -and [string]$run.status -eq 'PASS') { $contradiction = 'run-result-pass-without-passing-capability' }
        elseif ($passes.Count -gt 0 -and [string]$run.status -eq 'SKIPPED') { $contradiction = 'run-result-skipped-with-passing-capability' }
        elseif ([string]$run.status -eq 'FAIL' -and $null -eq $run.PSObject.Properties['primaryFailure']) { $contradiction = 'run-result-fail-without-primary-failure' }
        $source = ConvertTo-ProjectStatusSafePath -Path $resolved -RepoRoot $RepoRoot
        if ($contradiction) {
            return [pscustomobject]@{
                status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'verification-run-result-contradiction'; source = $source; detail = $contradiction
                runId = [string]$run.runId; invocationId = [string]$run.invocationId
                capabilityCount = $capabilities.Count; passCount = $passes.Count; skipCount = $skips.Count; failCount = $fails.Count
            }
        }
        return [pscustomobject]@{
            status = [string]$run.status; classification = [string]$run.classification; reason = [string]$run.reason; source = $source
            runId = [string]$run.runId; invocationId = [string]$run.invocationId
            capabilityCount = $capabilities.Count; passCount = $passes.Count; skipCount = $skips.Count; failCount = $fails.Count
        }
    } catch {
        [pscustomobject]@{ status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'verification-run-result-invalid'; source = (ConvertTo-ProjectStatusSafePath -Path $resolved -RepoRoot $RepoRoot); detail = $_.Exception.Message }
    }
}

function Get-ProjectStatusReleaseEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [string] $OutputDirectory,
        [string] $ReleaseReportPath,
        [ValidateSet('PASS', 'FAIL')] [string] $LifecycleStatus = 'PASS'
    )

    if ($LifecycleStatus -eq 'FAIL') {
        return [pscustomobject]@{
            status = 'FAIL'
            classification = 'NON-PRODUCTION-OR-BLOCKED'
            reason = 'openspec-lifecycle-integrity-invalid'
            source = $null
            productionReady = $false
        }
    }

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
    $verificationNote = [string]$Status.verification.status
    if ($Status.verification.reason) { $verificationNote += " ($($Status.verification.reason))" }
    if ($Status.verification.source) { $verificationNote += " from ``$($Status.verification.source)``" }
    [void]$lines.Add("- Selected verifier result: $verificationNote")
    [void]$lines.Add('')
    [void]$lines.Add('## OpenSpec frontier')
    [void]$lines.Add('')
    [void]$lines.Add("Open unchecked tasks: **$($Status.openSpec.totalOpenTasks)** across **$($Status.openSpec.changesWithOpenTasks)** changes; active local open: **$($Status.openSpec.activeLocalOpenTasks)**.")
    [void]$lines.Add("Lifecycle model: **$($Status.openSpec.lifecycle.status)** from ``$($Status.openSpec.lifecycle.source)``$(if ($Status.openSpec.lifecycle.errors.Count -gt 0) { " ($($Status.openSpec.lifecycle.errors -join '; '))" })")
    [void]$lines.Add("Lifecycle integrity diagnostics: structural=$(@($Status.openSpec.lifecycle.structuralErrors).Count); evidence=$(@($Status.openSpec.lifecycle.evidenceErrors).Count); semantic=$(@($Status.openSpec.lifecycle.semanticErrors).Count).")
    foreach ($entry in @($Status.openSpec.lifecycle.entries)) {
        $evidenceSummary = @($entry.evidence | ForEach-Object { "$($_.kind):$($_.source)" }) -join ', '
        [void]$lines.Add("- Lifecycle ``$($entry.name)`` [$($entry.state)]: $evidenceSummary")
    }
    foreach ($change in @($Status.openSpec.changes)) {
        [void]$lines.Add("### ``$($change.name)`` [$($change.lifecycleState)] ($($change.openTaskCount) open)")
        foreach ($task in @($change.openTasks)) { [void]$lines.Add("- ``$($task.task)`` [$($task.taskDisposition); $($task.primaryCategory); $($task.status)] $($task.nextAction) (source: $($task.classificationSource))") }
    }
    [void]$lines.Add('')
    [void]$lines.Add('## Release-readiness frontier')
    [void]$lines.Add('')
    foreach ($bucket in @('locallyCertified', 'localOpen', 'environmentBlocked', 'providerBlocked', 'productionSigningBlocked', 'cleanHostElevatedBlocked', 'administrativeBlocked', 'mixedOpen', 'historicalOpen', 'supersededOpen', 'ambiguousOpen')) {
        $title = switch ($bucket) {
            'locallyCertified' { 'Locally certified' }
            'localOpen' { 'Local open work' }
            'environmentBlocked' { 'Environment-blocked' }
            'providerBlocked' { 'Provider-blocked' }
            'productionSigningBlocked' { 'Production-signing-blocked' }
            'cleanHostElevatedBlocked' { 'Clean-host/elevated-blocked' }
            'administrativeBlocked' { 'Administrative-blocked' }
            'mixedOpen' { 'Mixed local/external work' }
            'historicalOpen' { 'Historical open ledger items' }
            'supersededOpen' { 'Superseded open ledger items' }
            default { 'Lifecycle/model errors' }
        }
        [void]$lines.Add("### $title")
        $items = @($Status.frontier.$bucket)
        if ($items.Count -eq 0) { [void]$lines.Add('- None recorded.') } else { foreach ($item in $items) { [void]$lines.Add("- ``$($item.change)/$($item.task)`` [$($item.lifecycleState); $($item.taskDisposition)]: $($item.reason) Next: $($item.nextAction)") } }
    }
    [void]$lines.Add('')
    [void]$lines.Add("Release candidate: **$($Status.release.classification)** ($($Status.release.status)); production readiness is never inferred from unsigned preparation.")
    return ($lines -join [Environment]::NewLine) + [Environment]::NewLine
}

function Get-ProjectStatusFrontierProjection {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $OpenSpec)

    $allTasks = @($OpenSpec.changes | ForEach-Object { $_.openTasks })
    $frontier = [ordered]@{ status = $OpenSpec.status; locallyCertified = @(); localOpen = @(); environmentBlocked = @(); providerBlocked = @(); productionSigningBlocked = @(); cleanHostElevatedBlocked = @(); administrativeBlocked = @(); mixedOpen = @(); historicalOpen = @(); supersededOpen = @(); ambiguousOpen = @(); lifecycleErrors = @($OpenSpec.lifecycle.errors) }
    foreach ($task in $allTasks) {
        $item = [pscustomobject]@{
            change = $task.change
            task = $task.task
            category = $task.primaryCategory
            categories = @($task.categories)
            status = $task.status
            reason = ($task.reasons -join '; ')
            nextAction = $task.nextAction
            source = $task.source
            sourceLine = $task.sourceLine
            lifecycleState = $task.lifecycleState
            taskDisposition = $task.taskDisposition
            classificationSource = $task.classificationSource
            classificationConfidence = $task.classificationConfidence
            evidence = @($task.lifecycleEvidence)
        }
        if ($task.lifecycleState -in @('unknown', 'ambiguous') -or ($task.taskDisposition -eq 'mixed' -and $OpenSpec.status -ne 'PASS')) { $frontier.ambiguousOpen += $item }
        elseif ($task.taskDisposition -eq 'historical') { $frontier.historicalOpen += $item }
        elseif ($task.taskDisposition -eq 'superseded') { $frontier.supersededOpen += $item }
        elseif ($task.taskDisposition -eq 'mixed') { $frontier.mixedOpen += $item }
        elseif ($task.primaryCategory -eq 'production-signing') { $frontier.productionSigningBlocked += $item }
        elseif ($task.primaryCategory -eq 'external-provider') { $frontier.providerBlocked += $item }
        elseif ($task.primaryCategory -eq 'clean-host') { $frontier.cleanHostElevatedBlocked += $item }
        elseif ($task.primaryCategory -eq 'elevated') { $frontier.cleanHostElevatedBlocked += $item }
        elseif ($task.primaryCategory -eq 'interactive') { $frontier.environmentBlocked += $item }
        elseif ($task.primaryCategory -eq 'administrative') { $frontier.administrativeBlocked += $item }
        elseif ($task.status -eq 'PASS') { $frontier.locallyCertified += $item }
        elseif ($task.taskDisposition -eq 'active') { $frontier.localOpen += $item }
        else { $frontier.ambiguousOpen += $item }
    }
    return [pscustomobject]$frontier
}

function New-ProjectStatus {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [string] $OutputDirectory,
        [string] $ReleaseReportPath,
        [string] $VerificationRunResultPath,
        [switch] $SkipCtest
    )

    $git = Invoke-ProjectStatusGit -RepoRoot $RepoRoot
    $tests = if ($SkipCtest) { [pscustomobject]@{ status = 'SKIPPED'; discoveredCount = $null; builds = @() } } else { Get-ProjectStatusCtestEvidence -RepoRoot $RepoRoot }
    $capabilities = Get-ProjectStatusCapabilityEvidence -RepoRoot $RepoRoot
    $verification = Get-ProjectStatusVerificationEvidence -RepoRoot $RepoRoot -RunResultPath $VerificationRunResultPath
    $openSpec = Get-ProjectStatusOpenSpecFrontier -RepoRoot $RepoRoot
    $release = Get-ProjectStatusReleaseEvidence -RepoRoot $RepoRoot -OutputDirectory $OutputDirectory -ReleaseReportPath $ReleaseReportPath -LifecycleStatus $openSpec.status
    $frontier = Get-ProjectStatusFrontierProjection -OpenSpec $openSpec
    $status = [pscustomobject]@{
        schemaVersion = $script:ProjectStatusSchemaVersion
        kind = 'fastfiles-project-status'
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        environment = [pscustomobject]@{ computer = if ($env:COMPUTERNAME) { '<local-host>' } else { $null }; os = [Environment]::OSVersion.VersionString; architecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString(); powershell = $PSVersionTable.PSVersion.ToString() }
        git = $git
        tests = $tests
        capabilities = $capabilities
        verification = $verification
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

Export-ModuleMember -Function Get-ProjectStatusOutcome, ConvertTo-ProjectStatusSafePath, Get-ProjectStatusGitStateFromLines, Invoke-ProjectStatusGit, Resolve-ProjectStatusRepositoryEvidencePath, Resolve-ProjectStatusGitCommitEvidence, Resolve-ProjectStatusEvidence, Get-ProjectStatusLifecycleModel, Get-ProjectStatusTaskClassification, Get-ProjectStatusOpenSpecFrontier, Get-ProjectStatusFrontierProjection, Get-ProjectStatusJUnitResult, Get-ProjectStatusCtestEvidence, Get-ProjectStatusCapabilityEvidence, Get-ProjectStatusVerificationEvidence, Get-ProjectStatusReleaseEvidence, Test-ProjectStatusClaimConsistency, New-ProjectStatusMarkdown, New-ProjectStatus
