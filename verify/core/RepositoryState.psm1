<#
    Repository/source-state provenance for validation qualification.

    This module is the single authority for local Git identity used by exact-tree
    validation. It never resets, cleans, stashes, checks out, or otherwise mutates
    caller work. Ignored/generated files are intentionally excluded by Git's normal
    porcelain status; every non-ignored untracked path is treated as source input.
#>

function Invoke-RepositoryGit {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [switch] $AllowFailure
    )
    $lines = @(& git -C $RepoRoot @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    if (-not $AllowFailure -and $exitCode -ne 0) {
        $detail = ($lines -join ' ').Trim()
        if ([string]::IsNullOrWhiteSpace($detail)) { $detail = "git exited $exitCode" }
        throw "Git provenance command failed: $detail"
    }
    [pscustomobject]@{ exitCode = $exitCode; lines = @($lines) }
}

function Get-RepositoryIdentity {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $RepoRoot)
    $remote = Invoke-RepositoryGit -RepoRoot $RepoRoot -Arguments @('remote', 'get-url', 'origin') -AllowFailure
    if ($remote.exitCode -eq 0 -and $remote.lines.Count -gt 0) {
        $url = [string]$remote.lines[0]
        if ($url -match '(?i)(?:github\.com[:/])([^/\s]+/[^/\s]+?)(?:\.git)?$') { return $Matches[1] }
    }
    return '<local-repository>'
}

function Get-RepositoryStateDigest {
    param(
        [Parameter(Mandatory)] [string] $HeadSha,
        [Parameter(Mandatory)] [string] $TreeSha,
        [string[]] $StagedPaths,
        [string[]] $UnstagedPaths,
        [string[]] $UntrackedPaths
    )
    $canonical = [ordered]@{
        headSha = $HeadSha.ToLowerInvariant()
        treeSha = $TreeSha.ToLowerInvariant()
        staged = @($StagedPaths | Sort-Object -Unique)
        unstaged = @($UnstagedPaths | Sort-Object -Unique)
        untracked = @($UntrackedPaths | Sort-Object -Unique)
    } | ConvertTo-Json -Depth 5 -Compress
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($canonical)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

function Get-RepositorySourceState {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [ValidateSet('development', 'exact-tree')]
        [string] $QualificationMode = 'development'
    )
    $inside = Invoke-RepositoryGit -RepoRoot $RepoRoot -Arguments @('rev-parse', '--is-inside-work-tree') -AllowFailure
    if ($inside.exitCode -ne 0 -or ($inside.lines -join '').Trim() -ne 'true') { throw 'Repository provenance requires a Git working tree.' }
    $head = Invoke-RepositoryGit -RepoRoot $RepoRoot -Arguments @('rev-parse', '--verify', 'HEAD')
    $tree = Invoke-RepositoryGit -RepoRoot $RepoRoot -Arguments @('rev-parse', '--verify', 'HEAD^{tree}')
    $headSha = (($head.lines -join '').Trim()).ToLowerInvariant()
    $treeSha = (($tree.lines -join '').Trim()).ToLowerInvariant()
    if ($headSha -notmatch '^[0-9a-f]{40}$' -or $treeSha -notmatch '^[0-9a-f]{40}$') { throw 'Repository provenance could not resolve full HEAD/tree identities.' }
    $branchResult = Invoke-RepositoryGit -RepoRoot $RepoRoot -Arguments @('symbolic-ref', '--quiet', '--short', 'HEAD') -AllowFailure
    $detached = $branchResult.exitCode -ne 0
    $branch = if ($detached) { '<detached>' } else { ($branchResult.lines -join '').Trim() }
    $status = Invoke-RepositoryGit -RepoRoot $RepoRoot -Arguments @('status', '--porcelain=v1', '--untracked-files=all')
    $staged = [System.Collections.Generic.List[string]]::new()
    $unstaged = [System.Collections.Generic.List[string]]::new()
    $untracked = [System.Collections.Generic.List[string]]::new()
    foreach ($raw in @($status.lines)) {
        if ([string]::IsNullOrWhiteSpace($raw) -or $raw.Length -lt 3) { continue }
        $code = $raw.Substring(0, 2)
        $path = $raw.Substring(3).Replace('\', '/')
        if ($code -eq '??') { [void]$untracked.Add($path); continue }
        if ($code[0] -ne ' ' -and $code[0] -ne '?') { [void]$staged.Add($path) }
        if ($code[1] -ne ' ' -and $code[1] -ne '?') { [void]$unstaged.Add($path) }
    }
    $stagedPaths = @($staged | Sort-Object -Unique)
    $unstagedPaths = @($unstaged | Sort-Object -Unique)
    $untrackedPaths = @($untracked | Sort-Object -Unique)
    $trackedDirty = ($stagedPaths.Count + $unstagedPaths.Count) -gt 0
    $sourceClean = -not $trackedDirty -and $untrackedPaths.Count -eq 0
    $reasonCodes = [System.Collections.Generic.List[string]]::new()
    if ($stagedPaths.Count -gt 0) { [void]$reasonCodes.Add('staged-tracked-changes') }
    if ($unstagedPaths.Count -gt 0) { [void]$reasonCodes.Add('unstaged-tracked-changes') }
    if ($untrackedPaths.Count -gt 0) { [void]$reasonCodes.Add('untracked-source-input') }
    if ($detached) { [void]$reasonCodes.Add('detached-head') }
    [pscustomobject]@{
        repositoryId = Get-RepositoryIdentity -RepoRoot $RepoRoot
        qualificationMode = $QualificationMode
        headSha = $headSha
        treeSha = $treeSha
        branch = $branch
        detached = $detached
        trackedDirty = $trackedDirty
        sourceClean = $sourceClean
        exactTreeEligible = $sourceClean
        stagedPaths = $stagedPaths
        unstagedPaths = $unstagedPaths
        untrackedPaths = $untrackedPaths
        reasonCodes = @($reasonCodes | Sort-Object -Unique)
        stateDigest = Get-RepositoryStateDigest -HeadSha $headSha -TreeSha $treeSha -StagedPaths $stagedPaths -UnstagedPaths $unstagedPaths -UntrackedPaths $untrackedPaths
    }
}

function Compare-RepositorySourceState {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Before, [Parameter(Mandatory)] $After)
    $reasons = [System.Collections.Generic.List[string]]::new()
    if ([string]$Before.headSha -ne [string]$After.headSha) { [void]$reasons.Add('head-changed-during-validation') }
    if ([string]$Before.treeSha -ne [string]$After.treeSha) { [void]$reasons.Add('commit-tree-changed-during-validation') }
    if ([string]$Before.stateDigest -ne [string]$After.stateDigest) { [void]$reasons.Add('source-state-changed-during-validation') }
    [pscustomobject]@{ changed = $reasons.Count -gt 0; reasonCodes = @($reasons | Sort-Object -Unique) }
}

function New-RepositoryQualificationProvenance {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $StartState,
        [Parameter(Mandatory)] $EndState,
        [ValidateSet('development', 'exact-tree')] [string] $Mode = 'development'
    )
    $comparison = Compare-RepositorySourceState -Before $StartState -After $EndState
    $reasons = [System.Collections.Generic.List[string]]::new()
    foreach ($reason in @($StartState.reasonCodes)) { if ($reason -ne 'detached-head') { [void]$reasons.Add([string]$reason) } }
    foreach ($reason in @($EndState.reasonCodes)) { if ($reason -ne 'detached-head') { [void]$reasons.Add([string]$reason) } }
    foreach ($reason in @($comparison.reasonCodes)) { [void]$reasons.Add([string]$reason) }
    if ($Mode -ne 'exact-tree') { [void]$reasons.Add('development-mode-non-qualifying') }
    $qualified = $Mode -eq 'exact-tree' -and [bool]$StartState.sourceClean -and [bool]$EndState.sourceClean -and -not $comparison.changed -and [string]$StartState.headSha -eq [string]$EndState.headSha -and [string]$StartState.treeSha -eq [string]$EndState.treeSha
    [pscustomobject]@{
        mode = $Mode
        repositoryId = [string]$StartState.repositoryId
        headSha = [string]$StartState.headSha
        treeSha = [string]$StartState.treeSha
        branch = [string]$StartState.branch
        detached = [bool]$StartState.detached
        sourceCleanAtStart = [bool]$StartState.sourceClean
        sourceCleanAtFinish = [bool]$EndState.sourceClean
        changedDuringRun = [bool]$comparison.changed
        exactTreeQualified = [bool]$qualified
        startStateDigest = [string]$StartState.stateDigest
        finishStateDigest = [string]$EndState.stateDigest
        reasonCodes = @($reasons | Sort-Object -Unique)
    }
}

function Test-RepositoryQualificationBinding {
    [CmdletBinding()]
    param(
        [AllowNull()] $Provenance,
        [Parameter(Mandatory)] [string] $ExpectedHeadSha,
        [Parameter(Mandatory)] [string] $ExpectedTreeSha
    )
    $reasons = [System.Collections.Generic.List[string]]::new()
    if ($null -eq $Provenance) { [void]$reasons.Add('qualification-provenance-missing') }
    else {
        if ([string]$Provenance.mode -ne 'exact-tree') { [void]$reasons.Add('qualification-mode-not-exact-tree') }
        if (-not [bool]$Provenance.exactTreeQualified) { [void]$reasons.Add('result-not-exact-tree-qualified') }
        if ([bool]$Provenance.changedDuringRun) { [void]$reasons.Add('source-changed-during-validation') }
        if ([string]$Provenance.headSha -ne $ExpectedHeadSha.ToLowerInvariant()) { [void]$reasons.Add('qualification-head-sha-mismatch') }
        if ([string]$Provenance.treeSha -ne $ExpectedTreeSha.ToLowerInvariant()) { [void]$reasons.Add('qualification-tree-sha-mismatch') }
    }
    [pscustomobject]@{ valid = $reasons.Count -eq 0; reasonCodes = @($reasons | Sort-Object -Unique) }
}

function Resolve-VerificationRunQualification {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $RepoRoot, [Parameter(Mandatory)] [string] $RunResultPath)
    try {
        $candidate = if ([IO.Path]::IsPathRooted($RunResultPath)) { $RunResultPath } else { Join-Path $RepoRoot $RunResultPath }
        $path = [IO.Path]::GetFullPath($candidate)
    } catch { return [pscustomobject]@{ status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'qualification-result-path-invalid'; detail = $null; exactTreeQualified = $false; reasonCodes = @('qualification-result-path-invalid') } }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return [pscustomobject]@{ status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'qualification-result-not-found'; detail = $null; exactTreeQualified = $false; reasonCodes = @('qualification-result-not-found') } }
    try { $run = [IO.File]::ReadAllText($path) | ConvertFrom-Json } catch { return [pscustomobject]@{ status = 'FAIL'; classification = 'HARNESS_ERROR'; reason = 'qualification-result-invalid'; detail = $_.Exception.Message; exactTreeQualified = $false; reasonCodes = @('qualification-result-invalid') } }
    if ([string]$run.status -ne 'PASS') { return [pscustomobject]@{ status = [string]$run.status; classification = [string]$run.classification; reason = [string]$run.reason; detail = $null; exactTreeQualified = $false; reasonCodes = @('selected-run-not-pass'); sourceProvenance = $run.sourceProvenance } }
    $current = Get-RepositorySourceState -RepoRoot $RepoRoot -QualificationMode development
    $binding = Test-RepositoryQualificationBinding -Provenance $run.sourceProvenance -ExpectedHeadSha $current.headSha -ExpectedTreeSha $current.treeSha
    $bindingReasons = [System.Collections.Generic.List[string]]::new()
    foreach ($reason in @($binding.reasonCodes)) { [void]$bindingReasons.Add([string]$reason) }
    if (-not $current.sourceClean) { [void]$bindingReasons.Add('current-source-state-not-clean') }
    $reasons = @($bindingReasons | Sort-Object -Unique)
    if ($reasons.Count -gt 0) {
        $developmentOnly = @($reasons | Where-Object { $_ -in @('qualification-provenance-missing', 'qualification-mode-not-exact-tree', 'result-not-exact-tree-qualified') }).Count -gt 0 -and @($reasons | Where-Object { $_ -match 'mismatch|changed|current-source' }).Count -eq 0
        return [pscustomobject]@{
            status = if ($developmentOnly) { 'SKIPPED' } else { 'FAIL' }
            classification = if ($developmentOnly) { 'DEVELOPMENT_ONLY' } else { 'HARNESS_ERROR' }
            reason = if ($developmentOnly) { 'verification-result-development-only' } else { 'verification-run-provenance-mismatch' }
            detail = ($reasons -join ',')
            exactTreeQualified = $false
            reasonCodes = $reasons
            sourceProvenance = $run.sourceProvenance
        }
    }
    [pscustomobject]@{ status = 'PASS'; classification = 'PASS'; reason = $null; detail = $null; exactTreeQualified = $true; reasonCodes = @(); sourceProvenance = $run.sourceProvenance }
}

function Apply-VerificationQualification {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Evidence, [Parameter(Mandatory)] $Qualification)
    if ($Qualification.status -eq 'FAIL' -or ($Evidence.status -eq 'PASS' -and $Qualification.status -eq 'SKIPPED')) {
        $Evidence.status = [string]$Qualification.status
        $Evidence.classification = [string]$Qualification.classification
        $Evidence.reason = [string]$Qualification.reason
        if ($Evidence.PSObject.Properties['detail']) { $Evidence.detail = [string]$Qualification.detail } else { $Evidence | Add-Member -NotePropertyName detail -NotePropertyValue ([string]$Qualification.detail) }
    }
    $values = [ordered]@{
        qualification = if ($Qualification.exactTreeQualified) { 'exact-tree' } else { 'development-only' }
        exactTreeQualified = [bool]$Qualification.exactTreeQualified
        sourceProvenance = $Qualification.sourceProvenance
        provenanceReasonCodes = @($Qualification.reasonCodes)
    }
    foreach ($key in $values.Keys) {
        if ($Evidence.PSObject.Properties[$key]) { $Evidence.$key = $values[$key] } else { $Evidence | Add-Member -NotePropertyName $key -NotePropertyValue $values[$key] }
    }
    return $Evidence
}

Export-ModuleMember -Function Get-RepositorySourceState, Compare-RepositorySourceState, New-RepositoryQualificationProvenance,
    Test-RepositoryQualificationBinding, Resolve-VerificationRunQualification, Apply-VerificationQualification
