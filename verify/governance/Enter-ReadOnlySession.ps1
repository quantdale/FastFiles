[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $RunId = '',
    [switch] $CheckRemote
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\core\RepositoryState.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot '..\core\Governance.psm1') -Force -DisableNameChecking

$resolved = [IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($RunId)) {
    $RunId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
}

# Declare readonly mode for this process and write the sticky sentinel so child
# processes, hooks, and raw git invocations observe the same mode.
$env:FASTFILES_AGENT_MODE = 'readonly'
$sentinel = Join-Path $resolved 'verify\runs\agent-mode.readonly'
$sentinelParent = Split-Path -Parent $sentinel
if (-not (Test-Path -LiteralPath $sentinelParent)) { New-Item -ItemType Directory -Path $sentinelParent -Force | Out-Null }
Set-Content -LiteralPath $sentinel -Value $RunId -Encoding utf8NoBOM

$baselinePath = Join-Path $resolved "verify\runs\readonly\$RunId\baseline.json"
$baseline = New-ReadOnlyBaseline -RepoRoot $resolved -OutputPath $baselinePath

$result = [pscustomobject]@{
    status        = 'PASS'
    runId         = $RunId
    baselinePath  = $baselinePath
    headSha       = $baseline.headSha
    treeSha       = $baseline.treeSha
    stateDigest   = $baseline.stateDigest
    originMain    = $baseline.originMain
    checkRemote   = [bool]$CheckRemote
    mode          = (Get-AgentMode)
}
$result | ConvertTo-Json -Depth 10
exit 0
