[CmdletBinding()]
param(
    [string] $OutputDirectory = (Join-Path $PSScriptRoot 'generated'),
    [ValidateSet('human', 'json')]
    [string] $Format = 'human',
    [string] $ReleaseReportPath,
    [string] $VerificationRunResultPath,
    [switch] $SkipCtest
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\ProjectStatus.psm1') -Force
Import-Module (Join-Path $repoRoot 'verify\core\RepositoryState.psm1') -Force

$status = New-ProjectStatus -RepoRoot $repoRoot -OutputDirectory $OutputDirectory -ReleaseReportPath $ReleaseReportPath -VerificationRunResultPath $VerificationRunResultPath -SkipCtest:$SkipCtest
if ($VerificationRunResultPath) {
    $qualification = Resolve-VerificationRunQualification -RepoRoot $repoRoot -RunResultPath $VerificationRunResultPath
    $status.verification = Apply-VerificationQualification -Evidence $status.verification -Qualification $qualification
    if ($OutputDirectory) {
        $jsonPath = Join-Path $OutputDirectory 'project-status.json'
        $markdownPath = Join-Path $OutputDirectory 'project-status.md'
        $status | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $jsonPath -Encoding utf8NoBOM
        (New-ProjectStatusMarkdown -Status $status) | Set-Content -LiteralPath $markdownPath -Encoding utf8NoBOM
    }
}
if ($Format -eq 'json') {
    $status | ConvertTo-Json -Depth 20
} else {
    Write-Host "Project status: $($status.git.sha) on $($status.git.branch)"
    Write-Host "Worktree dirty: $($status.git.dirty); CTest discovered: $($status.tests.discoveredCount)"
    Write-Host "OpenSpec unchecked tasks: $($status.openSpec.totalOpenTasks); active local open: $($status.openSpec.activeLocalOpenTasks); release candidate: $($status.release.classification)"
    Write-Host "OpenSpec lifecycle model: $($status.openSpec.lifecycle.status)"
    $verificationNote = [string]$status.verification.status
    if ($status.verification.reason) { $verificationNote += " ($($status.verification.reason))" }
    if ($VerificationRunResultPath) { $verificationNote += "; qualification=$($status.verification.qualification)" }
    Write-Host "Selected verifier result: $verificationNote"
    Write-Host "JSON: $($status.jsonPath)"
    Write-Host "Markdown: $($status.markdownPath)"
}
if ($status.openSpec.status -eq 'FAIL' -or $status.verification.status -eq 'FAIL') { exit 1 }
