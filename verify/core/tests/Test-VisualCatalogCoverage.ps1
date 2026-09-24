# residual-defect-closure-and-certification-completion 3.2: visual catalog
# registry contract. The comparator is pure and does not launch FastFiles or
# consume a prior run tree.
$ErrorActionPreference = 'Stop'
$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Import-Module (Join-Path $repoRoot 'verify\visual\VisualCapture.psm1') -Force -DisableNameChecking
$expected = @('home-default', 'browse-column-view', 'settings-general')
function New-VisualRecord {
    param([string] $ScenarioId, [string] $Status = 'PASS')
    return [pscustomobject]@{
        scenarioId = $ScenarioId
        status = $Status
        assertionResult = [pscustomobject]@{ pass = ($Status -eq 'PASS') }
        screenshotPath = if ($Status -eq 'PASS') { "$ScenarioId.png" } else { '' }
        imageHash = if ($Status -eq 'PASS') { ('a' * 64) } else { '' }
    }
}
$records = @(
    (New-VisualRecord 'home-default')
    (New-VisualRecord 'browse-column-view')
    (New-VisualRecord 'settings-general')
)
$complete = Compare-VisualScenarioCoverage -ExpectedScenarioIds $expected -Records $records
Check $complete.Ok 'complete expected registry passes'
$missing = Compare-VisualScenarioCoverage -ExpectedScenarioIds $expected -Records @($records[0], $records[1])
Check ((-not $missing.Ok) -and ($missing.Missing -contains 'settings-general')) 'missing scenario is reported explicitly'
$duplicate = Compare-VisualScenarioCoverage -ExpectedScenarioIds $expected -Records @($records[0], $records[0], $records[2])
Check ((-not $duplicate.Ok) -and ($duplicate.Duplicates -contains 'home-default')) 'duplicate scenario is rejected'
$unexpected = Compare-VisualScenarioCoverage -ExpectedScenarioIds $expected -Records @($records + (New-VisualRecord 'mystery'))
Check ((-not $unexpected.Ok) -and ($unexpected.Unexpected -contains 'mystery')) 'unexpected scenario is rejected'
$skipped = Compare-VisualScenarioCoverage -ExpectedScenarioIds $expected -Records @(
    (New-VisualRecord 'home-default' 'SKIPPED')
    $records[1]
    $records[2]
)
Check ((-not $skipped.Ok) -and ($skipped.NonPass -contains 'home-default')) 'expected skipped scenario cannot certify as complete'
$invalid = Compare-VisualScenarioCoverage -ExpectedScenarioIds $expected -Records @(
    ([pscustomobject]@{ scenarioId = 'home-default'; status = 'PASS'; assertionResult = [pscustomobject]@{ pass = $false }; screenshotPath = ''; imageHash = '' })
    $records[1]
    $records[2]
)
Check ((-not $invalid.Ok) -and ($invalid.InvalidPass -contains 'home-default')) 'PASS without assertion/capture is rejected'
$sentinel = Compare-VisualScenarioCoverage -ExpectedScenarioIds $expected -Records @(
    [pscustomobject]@{ scenarioId = 'catalog-lifecycle' }
    $records
)
Check $sentinel.Ok 'explicit lifecycle diagnostic sentinel is allowed without weakening scenario completeness'
$empty = Compare-VisualScenarioCoverage -ExpectedScenarioIds $expected -Records @()
Check ((-not $empty.Ok) -and ($empty.Missing.Count -eq 3)) 'empty run reports every missing core scenario'

if ($failures -gt 0) { exit 1 }
Write-Host 'All visual catalog coverage contract checks passed.'
exit 0
