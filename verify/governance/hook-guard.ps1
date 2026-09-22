[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('pre-commit', 'pre-push')]
    [string] $Operation
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\Governance.psm1') -Force -DisableNameChecking

function Write-StderrSafe {
    param([string] $Message)
    [Console]::Error.WriteLine($Message)
}

if (-not (Test-AgentModeReadOnly)) {
    exit 0
}

$mode = Get-AgentMode
$operationName = if ($Operation -eq 'pre-commit') { 'git-commit' } else { 'git-push' }
Write-StderrSafe "readonly-mode-violation:$operationName (mode=$mode)"
exit 1
