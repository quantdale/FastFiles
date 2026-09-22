[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Path,
    [switch] $ReadOnlyContext,
    [switch] $FailOnHijack,
    [ValidateSet('json', 'human')]
    [string] $Format = 'human'
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\core\Governance.psm1') -Force -DisableNameChecking

$authority = Resolve-CampaignDocAuthority -Path $Path -ReadOnlyContext:$ReadOnlyContext
if ($Format -eq 'json') {
    $authority | ConvertTo-Json -Depth 8
} else {
    Write-Host "path: $($authority.path)"
    Write-Host "exists: $($authority.exists)"
    Write-Host "status: $($authority.statusHeader)"
    Write-Host "classification: $($authority.classification)"
    Write-Host "active: $($authority.active)"
    Write-Host "hijack-attempt: $($authority.hijackAttempt)"
    foreach ($imperative in @($authority.imperatives)) {
        Write-Host "imperative: $imperative"
    }
}

if ($FailOnHijack -and $authority.hijackAttempt) { exit 1 }
exit 0
