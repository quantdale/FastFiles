[CmdletBinding()]
param(
    [string] $OutputDirectory = (Join-Path $PSScriptRoot 'generated'),
    [ValidateSet('human', 'json')]
    [string] $Format = 'human',
    [string] $ReleaseReportPath,
    [switch] $SkipCtest
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\ProjectStatus.psm1') -Force

$status = New-ProjectStatus -RepoRoot $repoRoot -OutputDirectory $OutputDirectory -ReleaseReportPath $ReleaseReportPath -SkipCtest:$SkipCtest
if ($Format -eq 'json') {
    $status | ConvertTo-Json -Depth 20
} else {
    Write-Host "Project status: $($status.git.sha) on $($status.git.branch)"
    Write-Host "Worktree dirty: $($status.git.dirty); CTest discovered: $($status.tests.discoveredCount)"
    Write-Host "OpenSpec unchecked tasks: $($status.openSpec.totalOpenTasks); release candidate: $($status.release.classification)"
    Write-Host "JSON: $($status.jsonPath)"
    Write-Host "Markdown: $($status.markdownPath)"
}
