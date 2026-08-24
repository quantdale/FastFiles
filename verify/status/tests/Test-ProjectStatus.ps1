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

$repoRelative = ConvertTo-ProjectStatusSafePath -Path (Join-Path $repoRoot 'verify\status\generated\project-status.json') -RepoRoot $repoRoot
$external = ConvertTo-ProjectStatusSafePath -Path (Join-Path ([IO.Path]::GetTempPath()) 'private-user-path.txt') -RepoRoot $repoRoot
Check ($repoRelative -eq 'verify/status/generated/project-status.json') 'repository paths are projected relative to the root'
Check ($external -eq '<external-path>') 'external absolute paths are redacted'

$fixtureStatus = [pscustomobject]@{
    tests = [pscustomobject]@{ discoveredCount = 75; builds = @() }
    openSpec = [pscustomobject]@{ totalOpenTasks = 2; changesWithOpenTasks = 1; changes = @() }
    git = $clean
    generatedAtUtc = '2026-08-24T00:00:00Z'
    capabilities = [pscustomobject]@{ status = 'PASS'; loadedCount = 1; rejectedCount = 0 }
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

$liveGit = Invoke-ProjectStatusGit -RepoRoot $repoRoot
$frontier = Get-ProjectStatusOpenSpecFrontier -RepoRoot $repoRoot
Check ($liveGit.sha.Length -eq 40 -and $liveGit.branch.Length -gt 0) 'live Git evidence contains branch and SHA'
Check ($frontier.status -eq 'PASS' -and $null -ne $frontier.totalOpenTasks) 'live OpenSpec frontier parses all task ledgers'

if ($failures -gt 0) { exit 1 }
Write-Host 'All project-status tests passed.'
exit 0
