[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $ExpectedManifest,
    [Parameter(Mandatory)] [string] $ActualRoot,
    [switch] $CompareMetadata,
    [ValidateSet('human', 'json')] [string] $OutputFormat = 'human'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'DogfoodCommon.psm1') -Force

$actualRootFull = Assert-DogfoodRootSafe -Root $ActualRoot -RequireMarker
$expected = @(Get-DogfoodManifestEntries -ManifestPath ([IO.Path]::GetFullPath($ExpectedManifest)))
$actual = @(Get-DogfoodSnapshot -Root $actualRootFull -HashFiles)
$comparison = Compare-DogfoodEntries -Expected $expected -Actual $actual -CompareMetadata:$CompareMetadata
$result = [pscustomobject]@{
    status = if ($comparison.passed) { 'PASS' } else { 'FAIL' }
    root = $actualRootFull
    expectedManifest = [IO.Path]::GetFullPath($ExpectedManifest)
    expectedCount = $comparison.expectedCount
    actualCount = $comparison.actualCount
    missing = $comparison.missing
    unexpected = $comparison.unexpected
    changed = $comparison.changed
}

if ($OutputFormat -eq 'json') {
    $result | ConvertTo-Json -Depth 6
} else {
    Write-Output ("$($result.status): expected=$($result.expectedCount), actual=$($result.actualCount), missing=$($result.missing.Count), unexpected=$($result.unexpected.Count), changed=$($result.changed.Count)")
    if ($result.missing.Count -gt 0) { Write-Output ('Missing: ' + ($result.missing -join '; ')) }
    if ($result.unexpected.Count -gt 0) { Write-Output ('Unexpected: ' + ($result.unexpected -join '; ')) }
    if ($result.changed.Count -gt 0) { Write-Output ('Changed: ' + ($result.changed -join '; ')) }
}
if ($result.status -ne 'PASS') { exit 1 }
