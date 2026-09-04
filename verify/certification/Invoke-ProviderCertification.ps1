[CmdletBinding()]
param(
    [ValidateSet('real', 'fake')]
    [string] $Provider = 'real',

    [string] $OutputPath,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

# Forward to Invoke-VisionProviderCertification.ps1
$targetScript = Join-Path $PSScriptRoot "Invoke-VisionProviderCertification.ps1"
$params = @{
    Provider = $Provider
    RepoRoot = $RepoRoot
}
if ($OutputPath) { $params['OutputPath'] = $OutputPath }
if ($RequireExactTree) { $params['RequireExactTree'] = $true }

& $targetScript @params
