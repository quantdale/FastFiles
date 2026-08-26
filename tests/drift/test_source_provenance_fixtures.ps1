$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\RepositoryState.psm1') -Force

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "ok: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}
function Invoke-FixtureGit {
    param([Parameter(Mandatory)] [string] $Root, [Parameter(ValueFromRemainingArguments)] [string[]] $GitArgs)
    $output = @(& git -C $Root @GitArgs 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) { throw "fixture git command failed: git $($GitArgs -join ' '): $($output -join ' ')" }
    return @($output)
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-source-provenance-' + [guid]::NewGuid().ToString('N'))
$outsideRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-source-provenance-not-a-repo-' + [guid]::NewGuid().ToString('N'))
$collisionRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-source-provenance-collision-' + [guid]::NewGuid().ToString('N'))
$collisionResult = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-source-provenance-collision-result-' + [guid]::NewGuid().ToString('N') + '.json')
try {
    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $outsideRoot -Force | Out-Null
    Invoke-FixtureGit $fixtureRoot init | Out-Null
    Invoke-FixtureGit $fixtureRoot config user.name 'FastFiles Fixture' | Out-Null
    Invoke-FixtureGit $fixtureRoot config user.email 'fixture@example.invalid' | Out-Null
    Set-Content -LiteralPath (Join-Path $fixtureRoot '.gitignore') -Value @('build/', 'verify/status/generated/') -Encoding utf8NoBOM
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'committed-stale' -Encoding utf8NoBOM
    Invoke-FixtureGit $fixtureRoot add . | Out-Null
    Invoke-FixtureGit $fixtureRoot commit -m baseline | Out-Null

    $clean = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode exact-tree
    Check ($clean.sourceClean -and $clean.exactTreeEligible -and $clean.headSha -match '^[0-9a-f]{40}$' -and $clean.treeSha -match '^[0-9a-f]{40}$') 'clean HEAD resolves full commit/tree identities and is exact-tree eligible'
    Check ($clean.stagedPaths.Count -eq 0 -and $clean.unstagedPaths.Count -eq 0 -and $clean.untrackedPaths.Count -eq 0) 'clean HEAD has no source deltas'
    $cleanAgain = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode exact-tree
    Check ($clean.stateDigest -eq $cleanAgain.stateDigest) 'clean source-state digest is deterministic'

    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'unstaged-change' -Encoding utf8NoBOM
    $unstaged = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode development
    Check (-not $unstaged.sourceClean -and $unstaged.unstagedPaths -contains 'tracked.txt' -and $unstaged.reasonCodes -contains 'unstaged-tracked-changes') 'unstaged tracked change is non-qualifying'
    Invoke-FixtureGit $fixtureRoot checkout -- tracked.txt | Out-Null

    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'staged-change' -Encoding utf8NoBOM
    Invoke-FixtureGit $fixtureRoot add tracked.txt | Out-Null
    $staged = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode development
    Check (-not $staged.sourceClean -and $staged.stagedPaths -contains 'tracked.txt' -and $staged.reasonCodes -contains 'staged-tracked-changes') 'staged tracked change is non-qualifying'
    Invoke-FixtureGit $fixtureRoot reset HEAD tracked.txt | Out-Null
    Invoke-FixtureGit $fixtureRoot checkout -- tracked.txt | Out-Null

    Remove-Item -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Force
    $deleted = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode development
    Check (-not $deleted.sourceClean -and $deleted.unstagedPaths -contains 'tracked.txt') 'tracked deletion is non-qualifying'
    Invoke-FixtureGit $fixtureRoot checkout -- tracked.txt | Out-Null

    Set-Content -LiteralPath (Join-Path $fixtureRoot 'new-source.ps1') -Value 'Write-Output fixture' -Encoding utf8NoBOM
    $untracked = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode development
    Check (-not $untracked.sourceClean -and $untracked.untrackedPaths -contains 'new-source.ps1' -and $untracked.reasonCodes -contains 'untracked-source-input') 'non-ignored untracked source input is non-qualifying'
    Remove-Item -LiteralPath (Join-Path $fixtureRoot 'new-source.ps1') -Force

    New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'build') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'build\generated.obj') -Value 'generated' -Encoding utf8NoBOM
    New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'verify\status\generated') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'verify\status\generated\result.json') -Value '{}' -Encoding utf8NoBOM
    $ignoredOnly = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode exact-tree
    Check ($ignoredOnly.sourceClean -and $ignoredOnly.exactTreeEligible) 'ignored build/status artifacts do not invalidate source qualification'

    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'locally-corrected-but-uncommitted' -Encoding utf8NoBOM
    $dirtyCorrection = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode development
    Check ($dirtyCorrection.headSha -eq $clean.headSha -and $dirtyCorrection.treeSha -eq $clean.treeSha -and -not $dirtyCorrection.sourceClean) 'dirty correction keeps the old committed identity and cannot certify it'
    $dirtyProvenance = New-RepositoryQualificationProvenance -StartState $dirtyCorrection -EndState $dirtyCorrection -Mode development
    Check (-not $dirtyProvenance.exactTreeQualified -and $dirtyProvenance.reasonCodes -contains 'development-mode-non-qualifying') 'dirty development PASS cannot become exact-tree qualification evidence'
    $committedText = @(& git -C $fixtureRoot show 'HEAD:tracked.txt') -join "`n"
    Check ($committedText -match 'committed-stale' -and (Get-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Raw) -match 'locally-corrected') 'fixture reproduces divergent committed and working-tree content'
    Invoke-FixtureGit $fixtureRoot checkout -- tracked.txt | Out-Null

    $beforeMutation = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode exact-tree
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'mutated-during-run' -Encoding utf8NoBOM
    $afterMutation = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode exact-tree
    $midRun = Compare-RepositorySourceState -Before $beforeMutation -After $afterMutation
    Check ($midRun.changed -and $midRun.reasonCodes -contains 'source-state-changed-during-validation') 'mid-run source mutation is detected'
    $midRunProvenance = New-RepositoryQualificationProvenance -StartState $beforeMutation -EndState $afterMutation -Mode exact-tree
    Check (-not $midRunProvenance.exactTreeQualified -and $midRunProvenance.changedDuringRun) 'mid-run mutation fails exact-tree qualification'
    Invoke-FixtureGit $fixtureRoot checkout -- tracked.txt | Out-Null

    $qualifiedBefore = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode exact-tree
    $oldProvenance = New-RepositoryQualificationProvenance -StartState $qualifiedBefore -EndState $qualifiedBefore -Mode exact-tree
    Check ($oldProvenance.exactTreeQualified) 'clean unchanged state produces exact-tree qualification provenance'
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'second-commit' -Encoding utf8NoBOM
    Invoke-FixtureGit $fixtureRoot add tracked.txt | Out-Null
    Invoke-FixtureGit $fixtureRoot commit -m second | Out-Null
    $second = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode exact-tree
    $staleBinding = Test-RepositoryQualificationBinding -Provenance $oldProvenance -ExpectedHeadSha $second.headSha -ExpectedTreeSha $second.treeSha
    Check (-not $staleBinding.valid -and $staleBinding.reasonCodes -contains 'qualification-head-sha-mismatch' -and $staleBinding.reasonCodes -contains 'qualification-tree-sha-mismatch') 'result from prior commit cannot qualify a newer HEAD/tree'

    $secondProvenance = New-RepositoryQualificationProvenance -StartState $second -EndState $second -Mode exact-tree
    $validBinding = Test-RepositoryQualificationBinding -Provenance $secondProvenance -ExpectedHeadSha $second.headSha -ExpectedTreeSha $second.treeSha
    Check $validBinding.valid 'exact-tree result binds to its own candidate commit/tree'

    Invoke-FixtureGit $fixtureRoot checkout --detach HEAD | Out-Null
    $detached = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode exact-tree
    $detachedProvenance = New-RepositoryQualificationProvenance -StartState $detached -EndState $detached -Mode exact-tree
    Check ($detached.detached -and $detached.branch -eq '<detached>' -and $detached.sourceClean -and $detachedProvenance.exactTreeQualified) 'detached clean HEAD is supported by the source-provenance model'

    $missingRepoRejected = $false
    try { Get-RepositorySourceState -RepoRoot $outsideRoot -QualificationMode exact-tree | Out-Null } catch { $missingRepoRejected = $true }
    Check $missingRepoRejected 'missing Git metadata fails closed'

    $reasonOrder = @((New-RepositoryQualificationProvenance -StartState $dirtyCorrection -EndState $dirtyCorrection -Mode development).reasonCodes)
    $sortedOrder = @($reasonOrder | Sort-Object -Unique)
    Check (($reasonOrder -join '|') -eq ($sortedOrder -join '|')) 'provenance reason codes are deterministically ordered'
    $serialized = $secondProvenance | ConvertTo-Json -Depth 10
    Check ($serialized -notmatch [regex]::Escape([IO.Path]::GetFullPath($fixtureRoot))) 'provenance output does not leak absolute fixture paths'

    New-Item -ItemType Directory -Path $collisionRoot -Force | Out-Null
    $sentinel = Join-Path $collisionRoot 'do-not-delete.txt'
    Set-Content -LiteralPath $sentinel -Value 'sentinel' -Encoding utf8NoBOM
    $qualifier = Join-Path $repoRoot 'verify\qualification\Invoke-ExactTreeQualification.ps1'
    & (Get-Command pwsh).Source -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $qualifier -RepoRoot $repoRoot -WorktreePath $collisionRoot -OutputPath $collisionResult -Format human *> $null
    $collisionExit = $LASTEXITCODE
    Check ($collisionExit -ne 0 -and (Test-Path -LiteralPath $sentinel -PathType Leaf)) 'pre-existing qualification destination fails safely without deleting caller content'
} finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $outsideRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $collisionRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $collisionResult -Force -ErrorAction SilentlyContinue
}

if ($failures -gt 0) { exit 1 }
Write-Host 'All source-provenance fixtures passed.'
exit 0
