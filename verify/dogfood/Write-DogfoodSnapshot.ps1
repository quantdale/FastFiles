[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Root,
    [Parameter(Mandatory)] [string] $OutputPath,
    [switch] $HashFiles,
    [ValidateSet('human', 'json')] [string] $OutputFormat = 'human'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'DogfoodCommon.psm1') -Force

$rootFull = Assert-DogfoodRootSafe -Root $Root -RequireMarker
$snapshot = @(Get-DogfoodSnapshot -Root $rootFull -HashFiles:$HashFiles)
$outputFull = [IO.Path]::GetFullPath($OutputPath)
$parent = [IO.Path]::GetDirectoryName($outputFull)
if ($parent) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
$writer = [IO.StreamWriter]::new($outputFull, $false, [Text.UTF8Encoding]::new($false))
try {
    $header = [ordered]@{ schema = 1; kind = 'fastfiles-dogfood-snapshot'; hashFiles = [bool]$HashFiles; entryCount = $snapshot.Count }
    $writer.WriteLine(($header | ConvertTo-Json -Compress))
    foreach ($entry in $snapshot) { $writer.WriteLine(($entry | ConvertTo-Json -Compress)) }
} finally {
    $writer.Dispose()
}

$result = [pscustomobject]@{ status = 'PASS'; root = $rootFull; output = $outputFull; entries = $snapshot.Count; hashFiles = [bool]$HashFiles }
if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 4 } else { Write-Output ('Snapshot written: ' + $outputFull + ' (' + $snapshot.Count + ' entries)') }
