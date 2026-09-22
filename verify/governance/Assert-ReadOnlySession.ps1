[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $RunId = '',
    [string] $BaselinePath = '',
    [switch] $CheckRemote,
    [switch] $KeepSentinel
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\core\RepositoryState.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot '..\core\Governance.psm1') -Force -DisableNameChecking

$resolved = [IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
    if ([string]::IsNullOrWhiteSpace($RunId)) {
        $readonlyRoot = Join-Path $resolved 'verify\runs\readonly'
        if (Test-Path -LiteralPath $readonlyRoot) {
            $latest = Get-ChildItem -LiteralPath $readonlyRoot -Directory | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
            if ($latest) { $BaselinePath = Join-Path $latest.FullName 'baseline.json' }
        }
    } else {
        $BaselinePath = Join-Path $resolved "verify\runs\readonly\$RunId\baseline.json"
    }
}

if ([string]::IsNullOrWhiteSpace($BaselinePath) -or -not (Test-Path -LiteralPath $BaselinePath -PathType Leaf)) {
    Write-Host 'skip: no read-only baseline is available'
    exit 2
}

$comparison = Compare-ReadOnlyBaseline -RepoRoot $resolved -BaselinePath $BaselinePath -CheckRemote:$CheckRemote
$report = [pscustomobject]@{
    status     = if ($comparison.ok) { 'PASS' } else { 'FAIL' }
    baselinePath = $BaselinePath
    reasonCodes = $comparison.reasonCodes
    message    = $comparison.message
    startHead  = $comparison.before.headSha
    endHead    = $comparison.after.headSha
    startTree  = $comparison.before.treeSha
    endTree    = $comparison.after.treeSha
    startDigest = $comparison.before.stateDigest
    endDigest  = $comparison.after.stateDigest
}
$report | ConvertTo-Json -Depth 10

if (-not $KeepSentinel -and $comparison.ok) {
    $sentinel = Join-Path $resolved 'verify\runs\agent-mode.readonly'
    if (Test-Path -LiteralPath $sentinel) { Remove-Item -LiteralPath $sentinel -Force }
}

exit $comparison.exitCode
