[CmdletBinding()]
param(
    [string] $OutputDirectory = (Join-Path $PSScriptRoot 'generated'),
    [string] $ReleaseReportPath,
    [ValidateSet('human', 'json')]
    [string] $Format = 'human',
    [switch] $SkipCtest
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\ProjectStatus.psm1') -Force

function ConvertTo-SafeReadinessText {
    param([AllowNull()] [string] $Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    $value = $Text
    $repoPrefix = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\')
    $value = $value.Replace($repoPrefix, '<repo>')
    if ($env:USERPROFILE) { $value = $value.Replace([IO.Path]::GetFullPath($env:USERPROFILE).TrimEnd('\'), '<user-profile>') }
    if ($env:TEMP) { $value = $value.Replace([IO.Path]::GetFullPath($env:TEMP).TrimEnd('\'), '<temp>') }
    $value = [regex]::Replace($value, '(?i)(?:[A-Z]:\\|\\\\)[^,;\s]+', '<external-path>')
    if ($value.Length -gt 600) { $value = $value.Substring(0, 600) + '…' }
    return $value
}

function Get-SafeReleaseGateProjection {
    param([AllowNull()] $Report)
    if (-not $Report) { return [pscustomobject]@{ status = 'SKIPPED'; reason = 'release-readiness-report-not-found'; gates = @() } }
    $gates = @($Report.gates | ForEach-Object {
        [pscustomobject]@{
            id = [string]$_.id
            status = [string]$_.status
            reason = ConvertTo-SafeReadinessText -Text ([string]$_.reason)
            detail = ConvertTo-SafeReadinessText -Text ([string]$_.detail)
        }
    })
    [pscustomobject]@{ status = [string]$Report.overall; reason = $null; gates = $gates }
}

$status = New-ProjectStatus -RepoRoot $repoRoot -OutputDirectory $OutputDirectory -ReleaseReportPath $ReleaseReportPath -SkipCtest:$SkipCtest
$report = $null
$releaseSource = $null
if ($ReleaseReportPath -and (Test-Path -LiteralPath $ReleaseReportPath -PathType Leaf)) {
    $releaseSource = '<external-release-report>'
    try { $report = Get-Content -LiteralPath $ReleaseReportPath -Raw | ConvertFrom-Json } catch { $report = $null }
} else {
    $defaultRelease = Join-Path $OutputDirectory 'release-readiness-composition.json'
    if (Test-Path -LiteralPath $defaultRelease -PathType Leaf) {
        $releaseSource = 'release-readiness-composition.json'
        try { $report = Get-Content -LiteralPath $defaultRelease -Raw | ConvertFrom-Json } catch { $report = $null }
    }
}
$releaseProjection = Get-SafeReleaseGateProjection -Report $report
$overall = if ($releaseProjection.status -eq 'FAIL') { 'FAIL' } elseif ($releaseProjection.status -eq 'REQUIRED-BUT-UNAVAILABLE') { 'REQUIRED-BUT-UNAVAILABLE' } elseif ($releaseProjection.status -eq 'SKIPPED') { 'SKIPPED' } else { 'PASS' }
$frontierReport = [pscustomobject]@{
    schemaVersion = 1
    kind = 'fastfiles-release-readiness-frontier'
    generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    status = $overall
    git = $status.git
    localEvidence = [pscustomobject]@{
        tests = $status.tests
        capabilities = $status.capabilities
        openSpec = $status.openSpec
        diffCheck = $status.git.diffCheck
    }
    releaseCandidate = [pscustomobject]@{
        classification = $status.release.classification
        status = $status.release.status
        source = $releaseSource
        gates = @($releaseProjection.gates)
    }
    frontier = $status.frontier
    externalPrerequisites = @(
        'Authorized production signer, pins, and signed privileged runtime',
        'Clean supported Windows host with elevated install/service validation',
        'Snapshot-restorable Windows guest/provider with guest toolchain and credentials injected at runtime',
        'Interactive DPI, monitor, High Contrast, drag/drop, and accessibility matrix'
    )
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$jsonPath = Join-Path $OutputDirectory 'release-readiness-frontier.json'
$markdownPath = Join-Path $OutputDirectory 'release-readiness-frontier.md'
$frontierReport | ConvertTo-Json -Depth 25 | Set-Content -LiteralPath $jsonPath -Encoding utf8NoBOM
$lines = [System.Collections.Generic.List[string]]::new()
[void]$lines.Add('# FastFiles release-readiness frontier')
[void]$lines.Add('')
[void]$lines.Add("Generated UTC: $($frontierReport.generatedAtUtc)")
[void]$lines.Add("Commit: ``$($frontierReport.git.sha)`` on ``$($frontierReport.git.branch)``")
[void]$lines.Add("Overall: **$($frontierReport.status)**; release candidate: **$($frontierReport.releaseCandidate.classification)**")
[void]$lines.Add('')
[void]$lines.Add('## Local evidence')
[void]$lines.Add('')
[void]$lines.Add("- CTest discovered: $($frontierReport.localEvidence.tests.discoveredCount)")
[void]$lines.Add("- OpenSpec unchecked tasks: $($frontierReport.localEvidence.openSpec.totalOpenTasks)")
[void]$lines.Add("- Capability registry: $($frontierReport.localEvidence.capabilities.status)")
[void]$lines.Add('')
[void]$lines.Add('## Release gates')
[void]$lines.Add('')
if (@($frontierReport.releaseCandidate.gates).Count -eq 0) { [void]$lines.Add('- No release composition evidence supplied.') } else { foreach ($gate in @($frontierReport.releaseCandidate.gates)) { [void]$lines.Add("- ``$($gate.id)``: $($gate.status)$(if ($gate.reason) { " — $($gate.reason)" })") } }
[void]$lines.Add('')
foreach ($bucket in @('locallyCertified', 'localOpen', 'environmentBlocked', 'providerBlocked', 'productionSigningBlocked', 'cleanHostElevatedBlocked')) {
    $title = switch ($bucket) { 'locallyCertified' { 'Locally certified' } 'localOpen' { 'Local open work' } 'environmentBlocked' { 'Environment-blocked' } 'providerBlocked' { 'Provider-blocked' } 'productionSigningBlocked' { 'Production-signing-blocked' } default { 'Clean-host/elevated-blocked' } }
    [void]$lines.Add("## $title")
    [void]$lines.Add('')
    $items = @($frontierReport.frontier.$bucket)
    if ($items.Count -eq 0) { [void]$lines.Add('- None recorded.') } else { foreach ($item in $items) { [void]$lines.Add("- ``$($item.change)/$($item.task)``: $(ConvertTo-SafeReadinessText -Text $item.reason) Next: $(ConvertTo-SafeReadinessText -Text $item.nextAction)") } }
    [void]$lines.Add('')
}
[void]$lines.Add('## Exact external prerequisites')
[void]$lines.Add('')
foreach ($item in $frontierReport.externalPrerequisites) { [void]$lines.Add("- $item") }
($lines -join [Environment]::NewLine) + [Environment]::NewLine | Set-Content -LiteralPath $markdownPath -Encoding utf8NoBOM
if ($Format -eq 'json') { $frontierReport | ConvertTo-Json -Depth 25 } else {
    Write-Host "Release-readiness frontier: $($frontierReport.status)"
    Write-Host "Local commit: $($frontierReport.git.sha)"
    Write-Host "OpenSpec unchecked tasks: $($frontierReport.localEvidence.openSpec.totalOpenTasks)"
    Write-Host "JSON: $jsonPath"
    Write-Host "Markdown: $markdownPath"
}
