[CmdletBinding()]
param(
    [string] $RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [ValidateSet('all', 'core', 'search', 'selection', 'error', 'drag', 'treemap', 'isolated')]
    [string] $Scenario = 'all',
    [string] $UiaExeRel = 'build\debug-diagnostic\src\ui\FastFiles.exe',
    [string] $Change = 'real-user-dogfooding-release-engineering',
    [ValidateSet('human', 'json')]
    [string] $Format = 'human'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoFull = [IO.Path]::GetFullPath($RepoRoot)
$verify = Join-Path $repoFull 'verify\verify.ps1'
if (-not (Test-Path -LiteralPath $verify -PathType Leaf)) {
    throw "USER_ACCEPTANCE_VERIFY_MISSING: '$verify'"
}

$arguments = @(
    '-NoProfile',
    '-NonInteractive',
    '-File', $verify,
    'run',
    '-Change', $Change,
    '-Capability', 'windows-ui-automation-validation',
    '-CapabilityOption', "UiaScenario=$Scenario",
    '-CapabilityOption', "UiaExeRel=$UiaExeRel",
    '-SkipAnalyze',
    '-Format', $Format
)
& (Get-Command pwsh -ErrorAction Stop).Source @arguments
exit $LASTEXITCODE
