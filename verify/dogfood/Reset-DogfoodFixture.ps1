[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Root,
    [ValidateSet('human', 'json')] [string] $OutputFormat = 'human'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'DogfoodCommon.psm1') -Force

$rootFull = Assert-DogfoodRootSafe -Root $Root -RequireMarker
Remove-DogfoodRoot -Root $rootFull
$result = [pscustomobject]@{ status = 'PASS'; root = $rootFull; recoverable = $false; note = 'Disposable fixture root removed after marker validation.' }
if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 4 } else { Write-Output ('Removed disposable fixture: ' + $rootFull) }
