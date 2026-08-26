[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $OutputPath,
    [string] $WorktreePath,
    [ValidateSet('human', 'json')]
    [string] $Format = 'human'
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $RepoRoot 'verify\core\RepositoryState.psm1') -Force
Import-Module (Join-Path $RepoRoot 'verify\core\Schema.psm1') -Force

if (-not $OutputPath) { $OutputPath = Join-Path $RepoRoot 'verify\status\generated\exact-tree-qualification.json' }

$originState = Get-RepositorySourceState -RepoRoot $RepoRoot -QualificationMode development
$targetSha = [string]$originState.headSha
$targetTree = [string]$originState.treeSha
$runId = [guid]::NewGuid().ToString('n')
$invocationId = [guid]::NewGuid().ToString('n')
$started = [DateTime]::UtcNow
$worktreeRoot = if ($WorktreePath) { [IO.Path]::GetFullPath($WorktreePath) } else { Join-Path ([IO.Path]::GetTempPath()) ("FastFiles-exact-tree-$runId") }
$qualificationBranch = "ff-exact-tree-$runId"
$statusOutput = Join-Path ([IO.Path]::GetTempPath()) ("FastFiles-exact-tree-status-$runId")
$worktreeAdded = $false
$branchCreated = $false
$steps = [System.Collections.Generic.List[object]]::new()
$startState = $null
$endState = $null

function Add-StepResult {
    param([string] $Id, [string] $Status, [string] $Reason, [string] $Detail)
    $script:steps.Add([pscustomobject]@{
        capabilityId = $Id
        status = $Status
        classification = if ($Status -eq 'PASS') { 'PASS' } else { 'ASSERTION_FAILURE' }
        reason = $Reason
        detail = $Detail
    })
}

function Invoke-QualificationScript {
    param([string] $Id, [string] $RelativePath, [string[]] $Arguments = @())
    $scriptPath = Join-Path $worktreeRoot $RelativePath
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
        Add-StepResult -Id $Id -Status 'FAIL' -Reason 'qualification-script-missing' -Detail $RelativePath
        return
    }
    & (Get-Command pwsh).Source -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $scriptPath @Arguments
    $exit = $LASTEXITCODE
    if ($exit -eq 0) { Add-StepResult -Id $Id -Status 'PASS' -Reason $null -Detail $RelativePath }
    else { Add-StepResult -Id $Id -Status 'FAIL' -Reason 'qualification-step-failed' -Detail "$RelativePath exited $exit" }
}

try {
    if (Test-Path -LiteralPath $worktreeRoot) { throw "Owned exact-tree destination already exists: $worktreeRoot" }
    $branchProbe = @(& git -C $RepoRoot show-ref --verify --quiet "refs/heads/$qualificationBranch" 2>&1)
    if ($LASTEXITCODE -eq 0) { throw "Owned qualification branch already exists: $qualificationBranch" }
    $addOutput = @(& git -C $RepoRoot worktree add -b $qualificationBranch $worktreeRoot $targetSha 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) { throw "Unable to materialize exact commit tree: $($addOutput -join ' ')" }
    $worktreeAdded = $true
    $branchCreated = $true

    $startState = Get-RepositorySourceState -RepoRoot $worktreeRoot -QualificationMode exact-tree
    if (-not $startState.sourceClean -or $startState.headSha -ne $targetSha -or $startState.treeSha -ne $targetTree) { throw 'Qualification worktree does not match the requested clean commit/tree.' }

    Invoke-QualificationScript -Id 'source-marker-policy' -RelativePath 'verify\check_source_markers.ps1'
    Invoke-QualificationScript -Id 'diagnostic-policy' -RelativePath 'verify\check_diagnostic_policy.ps1'
    Invoke-QualificationScript -Id 'documentation-truth' -RelativePath 'tests\drift\check_doc_drift.ps1'
    Invoke-QualificationScript -Id 'documentation-truth-fixtures' -RelativePath 'tests\drift\test_doc_truth_fixtures.ps1'
    Invoke-QualificationScript -Id 'source-provenance-fixtures' -RelativePath 'tests\drift\test_source_provenance_fixtures.ps1'
    Invoke-QualificationScript -Id 'project-status-contract' -RelativePath 'verify\status\tests\Test-ProjectStatus.ps1'
    Invoke-QualificationScript -Id 'project-status-generation' -RelativePath 'verify\status\Get-ProjectStatus.ps1' -Arguments @('-SkipCtest', '-OutputDirectory', $statusOutput, '-Format', 'human')
    Invoke-QualificationScript -Id 'release-frontier-generation' -RelativePath 'verify\status\New-ReleaseReadinessReport.ps1' -Arguments @('-SkipCtest', '-OutputDirectory', $statusOutput, '-Format', 'human')
    $endState = Get-RepositorySourceState -RepoRoot $worktreeRoot -QualificationMode exact-tree
} catch {
    Add-StepResult -Id 'exact-tree-materialization' -Status 'FAIL' -Reason 'exact-tree-materialization-failed' -Detail $_.Exception.Message
    if ($startState -and -not $endState) { try { $endState = Get-RepositorySourceState -RepoRoot $worktreeRoot -QualificationMode exact-tree } catch { } }
} finally {
    if (-not $startState) {
        $startState = [pscustomobject]@{
            repositoryId = [string]$originState.repositoryId; headSha = $targetSha; treeSha = $targetTree; branch = $qualificationBranch; detached = $false;
            sourceClean = $false; reasonCodes = @('exact-tree-materialization-failed'); stateDigest = ''; qualificationMode = 'exact-tree'
        }
    }
    if (-not $endState) { $endState = $startState }

    if ($worktreeAdded) {
        $listed = @(& git -C $RepoRoot worktree list --porcelain 2>$null | ForEach-Object { $_.ToString() })
        $expectedLine = 'worktree ' + ([IO.Path]::GetFullPath($worktreeRoot).Replace('\', '/'))
        $owned = @($listed | ForEach-Object { $_.Replace('\', '/') } | Where-Object { $_ -eq $expectedLine }).Count -eq 1
        if ($owned) {
            & git -C $RepoRoot worktree remove --force $worktreeRoot *> $null
            if ($LASTEXITCODE -ne 0) { Add-StepResult -Id 'owned-worktree-cleanup' -Status 'FAIL' -Reason 'owned-worktree-cleanup-failed' -Detail 'git worktree remove failed' }
        } else { Add-StepResult -Id 'owned-worktree-cleanup' -Status 'FAIL' -Reason 'owned-worktree-identity-mismatch' -Detail 'Refused to remove an unverified path.' }
    }
    if ($branchCreated) {
        $branchHead = ((& git -C $RepoRoot rev-parse --verify "refs/heads/$qualificationBranch" 2>$null) | Out-String).Trim()
        if ($LASTEXITCODE -eq 0 -and $branchHead -eq $targetSha) {
            & git -C $RepoRoot branch -D $qualificationBranch *> $null
            if ($LASTEXITCODE -ne 0) { Add-StepResult -Id 'owned-branch-cleanup' -Status 'FAIL' -Reason 'owned-branch-cleanup-failed' -Detail $qualificationBranch }
        } else { Add-StepResult -Id 'owned-branch-cleanup' -Status 'FAIL' -Reason 'owned-branch-identity-mismatch' -Detail 'Refused to delete a branch whose target changed.' }
    }
    Remove-Item -LiteralPath $statusOutput -Recurse -Force -ErrorAction SilentlyContinue
}

$provenance = New-RepositoryQualificationProvenance -StartState $startState -EndState $endState -Mode exact-tree
$failed = @($steps | Where-Object { $_.status -eq 'FAIL' })
if (-not $provenance.exactTreeQualified) {
    Add-StepResult -Id 'source-provenance-binding' -Status 'FAIL' -Reason 'source-provenance-not-exact-tree' -Detail ($provenance.reasonCodes -join ',')
    $failed = @($steps | Where-Object { $_.status -eq 'FAIL' })
}
$finished = [DateTime]::UtcNow
$status = if ($failed.Count -eq 0) { 'PASS' } else { 'FAIL' }
$primary = if ($failed.Count -gt 0) { [pscustomobject]@{ source = 'qualification'; scenarioId = [string]$failed[0].capabilityId; classification = [string]$failed[0].classification; reason = [string]$failed[0].reason; summary = [string]$failed[0].detail; declaredReason = $null } } else { $null }
$runResult = [pscustomobject]@{
    runId = $runId
    invocationId = $invocationId
    change = 'committed-tree-validation-provenance-and-clean-checkout-parity'
    status = $status
    classification = if ($status -eq 'PASS') { 'PASS' } else { 'ASSERTION_FAILURE' }
    reason = if ($primary) { $primary.reason } else { $null }
    summary = if ($status -eq 'PASS') { "Exact commit/tree qualified by $($steps.Count) fast repository-sensitive checks." } else { "$($failed.Count) exact-tree qualification check(s) failed." }
    startedAtUtc = $started.ToString('o')
    finishedAtUtc = $finished.ToString('o')
    durationMs = [math]::Round(($finished - $started).TotalMilliseconds, 0)
    primaryFailure = $primary
    secondaryFailures = @()
    candidateSha = $targetSha
    candidateTreeSha = $targetTree
    sourceProvenance = $provenance
    capabilities = @($steps | ForEach-Object { [pscustomobject]@{ capabilityId = $_.capabilityId; status = $_.status; classification = $_.classification; reason = $_.reason } })
}
$schema = Get-Content -LiteralPath (Join-Path $RepoRoot 'verify\schemas\run-result.schema.json') -Raw | ConvertFrom-Json
$schemaResult = Test-JsonSchema -Data $runResult -Schema $schema
if (-not $schemaResult.Valid) { throw "Exact-tree qualification result failed run-result schema validation: $($schemaResult.Errors -join '; ')" }
$outputDir = Split-Path $OutputPath -Parent
if ($outputDir) { New-Item -ItemType Directory -Path $outputDir -Force | Out-Null }
$runResult | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $OutputPath -Encoding utf8NoBOM
if ($Format -eq 'json') { $runResult | ConvertTo-Json -Depth 20 }
else {
    Write-Host "Exact-tree qualification: $status"
    Write-Host "Commit: $targetSha"
    Write-Host "Tree:   $targetTree"
    Write-Host "Bound:  $($provenance.exactTreeQualified)"
    foreach ($step in $steps) { Write-Host "- $($step.capabilityId): $($step.status)$(if ($step.reason) { " ($($step.reason))" })" }
    Write-Host "Result: $OutputPath"
}
if ($status -eq 'PASS') { exit 0 }
exit 1
